/*
 * ps1-bare-metal - Copyright (C) 2023-2026 spicyjpeg
 * SPDX-License-Identifier: MIT
 */

/*
 * Up until now we treated the PS1's interrupt controller as a peripheral to
 * manually poll to check if a certain hardware event happened. While this is a
 * simple and efficient approach that works well for most devices, some of them
 * may occasionally fire interrupts much faster than we can reasonably poll. If
 * we wanted for example to stream data from the CD-ROM in the background while
 * our main loop runs, we'd have to check for incoming IRQs no less than 150
 * times per second. Doing so is not trivial when our main loop only ticks at 60
 * fps, and even less so if we are targeting 20-30 fps as is common on the PS1.
 *
 * There are pure software solutions to this issue (coroutines come to mind),
 * but we can seek some hardware help as well. The interrupt controller has the
 * ability to generate an exception when certain IRQs occur, forcing the CPU out
 * of our code's control flow and into an interrupt service routine (ISR), which
 * can handle the IRQ shortly after it occurs then resume execution of the
 * interrupted function. By default the ISR is provided by the BIOS kernel and
 * used to dispatch interrupts to its built-in drivers, but we may also request
 * it to dispatch to a custom handler through its APIs.
 *
 * This example uses the stubs defined in ps1/kernel.s to invoke BIOS functions.
 * BIOS API documentation can be found here:
 *    https://psx-spx.consoledev.net/kernelbios
 */

#include <setjmp.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include "common/font.h"
#include "common/gpu.h"
#include "common/sio0.h"
#include "ps1/cop0.h"
#include "ps1/delay.h"
#include "ps1/gpucmd.h"
#include "ps1/kernel.h"
#include "ps1/registers.h"

// We are going to set up our handler to run when vertical blanking starts and
// increment a global frame counter we can then read from our main loop. This
// will give us a reliable timing source that is not affected by frame rate or
// main loop slowdowns.
static int vsyncCounter = 0;

static void interruptHandler(void) {
	// Check for and clear the vsync IRQ flag in the interrupt controller. Most
	// peripherals require additional acknowledgement through their registers
	// (e.g. by writing to HCLRCTL for the CD-ROM) after clearing INTC_REG, with
	// vsync and timers being the only exceptions.
	if (INTC_REG & (1 << IRQ_VSYNC)) {
		INTC_REG = ~(1 << IRQ_VSYNC);

		// Increment the counter. As the ISR runs completely outside of the
		// program's regular flow, behaving instead in a similar way to a
		// concurrent thread, two memory barriers are needed to prevent the
		// compiler from optimizing access in ways that would break the counter:
		// one prior to reading its current value and one after writing back the
		// incremented one.
		// NOTE: outdated tutorials and code examples often suggest declaring
		// ISR-accessed variables as volatile to prevent optimization. Using
		// volatile to denote concurrent access is no longer valid in modern
		// C/C++ and may lead to incorrect behavior.
		__atomic_signal_fence(__ATOMIC_SEQ_CST);
		vsyncCounter++;
		__atomic_signal_fence(__ATOMIC_SEQ_CST);
	}

	// Request the kernel to quit the ISR and resume execution of any code
	// interrupted by it. Returning from the handler without calling this API
	// would crash the kernel.
	kernel_returnFromException();
}

static inline int getVSyncCounter(void) {
	// Temporarily defer any CPU exceptions caused by interrupts by clearing the
	// interrupt enable flag in MIPS coprocessor 0's status register. This is
	// known as a "critical section" and acts as a lock on vsyncCounter,
	// stopping the ISR from firing and manipulating it while we access it. In
	// this specific case we could get away without one - a single variable read
	// is inherently atomic and not interruptible - but it's good practice to
	// always use one when accessing variables modified by our handler.
	// NOTE: observe how the ISR may still fire between the following two lines,
	// but wouldn't break the code as long as it does not manipulate the status
	// register (which it would have no reason to). This is also why we are not
	// clearing INTC_MASK (see main()) to disable interrupts: toggling interrupt
	// sources from an ISR is a relatively common practice and would corrupt the
	// register if the main loop tried to manipulate it concurrently.
	uint32_t status = cop0_getReg(COP0_STATUS);
	cop0_setReg(COP0_STATUS, status & ~COP0_STATUS_IEc);

	// Fetch the counter. The barriers ensure the read will be performed after
	// disabling interrupts but before restoring the status register.
	__atomic_signal_fence(__ATOMIC_SEQ_CST);
	int counter = vsyncCounter;
	__atomic_signal_fence(__ATOMIC_SEQ_CST);

	// Terminate the critical section by restoring the initial COP0 status. If
	// interrupts were previously enabled, this will take care of reinstating
	// them and firing the ISR to catch up if an IRQ occurred in the meantime.
	cop0_setReg(COP0_STATUS, status);
	return counter;
}

// The ISR will take over immediately when the vsync flag gets set in INTC_REG,
// preempting any loop that polls it and letting our handler clear it first. We
// can thus no longer use our old waitForVSync() implementation and need a new
// one that cooperates with our handler in its place. Since we already have a
// counter we update each time the IRQ occurs, we can simply poll that instead.
static void waitForISRVSync(void) {
	int currentCount = getVSyncCounter();

	while (getVSyncCounter() == currentCount)
		__asm__ volatile("");
}

// The BIOS kernel provides three ways to dispatch interrupts to our handlers:
// events, priority chains and the fallback hook. We'll use the latter in this
// example, providing the kernel with an execution context to issue a C
// longjmp() to if no other handler catches the IRQ first. The standard way to
// initialize a jmp_buf is using setjmp(), but for this purpose it's simpler and
// safer to set one up manually with its own isolated stack and $gp pointer (see
// the linker script for more details). You may have to enlarge the stack if
// your handler uses large local arrays or structures.
static uint8_t isrStack[1024];

extern uint8_t _gp[];

static const jmp_buf isrContext = {{
	.ra = (uintptr_t) interruptHandler,
	.sp = (uintptr_t) &isrStack[sizeof(isrStack) - 16],
	.gp = (uintptr_t) _gp
}};

static void initKernelInterrupts(void) {
	// Start a new critical section, as required when calling the BIOS kernel's
	// interrupt setup APIs.
	uint32_t status = cop0_getReg(COP0_STATUS);
	cop0_setReg(COP0_STATUS, status & ~COP0_STATUS_IEc);

	// Configure the kernel to use our fallback handler and prevent its own
	// built-in SIO0, timer and vsync handlers from acknowledging any interrupts
	// and terminating the ISR early.
	kernel_hookEntryInt(isrContext);
	kernel_changeClearPad(false);

	for (int i = KERNEL_RCNT_TIMER0; i <= KERNEL_RCNT_VSYNC; i++)
		kernel_changeClearRCnt(i, false);

	// Leave the critical section with COP0 interrupts enabled regardless of
	// whether they were before and with the IRQ output from the interrupt
	// controller unmasked (enabled) at the COP0 side.
	cop0_setReg(COP0_STATUS, status | COP0_STATUS_IEc | COP0_STATUS_Im2);
}

#define SCREEN_HRES   GP1_HRES_320
#define SCREEN_VRES   GP1_VRES_256
#define SCREEN_WIDTH  320
#define SCREEN_HEIGHT 240

#define FONT_WIDTH       96
#define FONT_HEIGHT      56
#define FONT_COLOR_DEPTH GP0_COLOR_4BPP

extern const uint8_t fontTexture[], fontPalette[];

int main(int argc, const char **argv) {
	(void) argc;
	(void) argv;

	initSIO1TTY(115200);
	initPCSXTTY();
	setupGPU(
		getCurrentVideoMode(),
		SCREEN_HRES,
		SCREEN_VRES,
		SCREEN_WIDTH,
		SCREEN_HEIGHT
	);
	initSIO0();
	initKernelInterrupts();

	TextureInfo font;

	uploadIndexedTexture(
		&font,
		fontTexture,
		fontPalette,
		SCREEN_WIDTH * 2,
		0,
		SCREEN_WIDTH * 2,
		FONT_HEIGHT,
		FONT_WIDTH,
		FONT_HEIGHT,
		FONT_COLOR_DEPTH
	);

	// Our implementation of setupGPU() masks the vsync IRQ in the interrupt
	// controller, so we must clear any leftover IRQ and unmask it afterwards in
	// order for the ISR to actually start running in the background.
	INTC_REG   = ~(1 << IRQ_VSYNC);
	INTC_MASK |=   1 << IRQ_VSYNC;

	GPUDMAChain dmaChains[2];
	bool        usingSecondFrame = false;
	int         frameCounter     = 0;

	for (;;) {
		int bufferX = usingSecondFrame ? SCREEN_WIDTH : 0;
		int bufferY = 0;

		GPUDMAChain *chain = &dmaChains[usingSecondFrame];
		usingSecondFrame   = !usingSecondFrame;
		frameCounter++;

		uint32_t *ptr;

		GPU_GP1 = gp1_fbOffset(bufferX, bufferY);

		chain->nextPacket = chain->data;

		ptr    = allocateGP0Packet(chain, 4);
		ptr[0] = gp0_setPage(0, true, false);
		ptr[1] = gp0_fbOffset1(bufferX, bufferY);
		ptr[2] = gp0_fbOffset2(
			bufferX + SCREEN_WIDTH  - 1,
			bufferY + SCREEN_HEIGHT - 1
		);
		ptr[3] = gp0_fbOrigin(bufferX, bufferY);

		ptr    = allocateGP0Packet(chain, 3);
		ptr[0] = gp0_rgb(64, 64, 64) | gp0_vramFill();
		ptr[1] = gp0_xy(bufferX, bufferY);
		ptr[2] = gp0_xy(SCREEN_WIDTH, SCREEN_HEIGHT);

		// Display the ISR-updated counter alongside the main loop's own frame
		// counter. Simulate a main loop slowdown when any button is held down
		// to demonstrate how the former is not affected.
		if (pollController(0) | pollController(1))
			delayMicroseconds(100000);

		char buffer[64];

		snprintf(
			buffer,
			sizeof(buffer),
			"Main loop counter:\t%d\n"
			"Interrupt counter:\t%d",
			frameCounter,
			getVSyncCounter()
		);
		printString(chain, &font, 16, 32, buffer);
		printString(
			chain,
			&font,
			16,
			64,
			"Hold down any button to introduce lag."
		);

		*(chain->nextPacket) = gp0_endTag(0);

		waitForGP0Ready();
		waitForISRVSync();
		sendGPULinkedList(chain->data);
	}

	return 0;
}
