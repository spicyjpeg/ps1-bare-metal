/*
 * ps1-bare-metal - Copyright (C) 2023-2026 spicyjpeg
 * SPDX-License-Identifier: MIT
 */

/*
 * In the last example we relied on the interrupt service routine provided by
 * the PS1's BIOS kernel to dispatch hardware interrupts to our code. While the
 * fallback hook API is simple to use, it also leaves a significant amount of
 * performance on the table: the kernel performs several completely unnecessary
 * steps prior to handing control over to our code. This is not an issue in most
 * cases, but may become one when handling frequent or timing-sensitive IRQs.
 *
 * In order to speed up IRQ handling, we can leverage the fact the ISR resides
 * in the first 64 KB of RAM (reserved for use by the kernel) rather than in
 * ROM. We can "hijack" it by overwriting it with a custom ISR implementation
 * that does the bare minimum required to run our handler; doing so drastically
 * reduces interrupt overhead and additionally gives us the option to purge the
 * kernel and reclaim its memory region for our own use. The main drawback is
 * that any tools that rely on injecting themselves into its ISR, such as
 * debuggers, cheat engines or some kernel TTY drivers, will also stop working
 * as soon as we take over.
 *
 * The new ISR must be written in assembly, so the bulk of this example's code
 * is in isr.s. While thoroughly explained and commented, you may find it
 * difficult to follow without at least basic prior knowledge of MIPS assembly.
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include "common/cache.h"
#include "common/font.h"
#include "common/gpu.h"
#include "common/sio0.h"
#include "ps1/cop0.h"
#include "ps1/delay.h"
#include "ps1/gpucmd.h"
#include "ps1/registers.h"

// We'll reuse our vsync IRQ handler as-is from the last example. The only
// meaningful change is that interruptHandler() will now be called from isr.s
// and can thus no longer be static.
static unsigned int vsyncCounter = 0;

void interruptHandler(void) {
	if (INTC_REG & (1 << IRQ_VSYNC)) {
		INTC_REG = ~(1 << IRQ_VSYNC);

		__atomic_signal_fence(__ATOMIC_SEQ_CST);
		vsyncCounter++;
		__atomic_signal_fence(__ATOMIC_SEQ_CST);
	}
}

static inline unsigned int getVSyncCounter(void) {
	// This macro is defined in ps1/cop0.h and equivalent to the code in the
	// last example. It starts a critical section, inserts the appropriate
	// memory barriers and restores the COP0 status register automatically once
	// the function returns.
	COP0_CRITICAL_SECTION();

	return vsyncCounter;
}

static void waitForISRVSync(void) {
	unsigned int counter = getVSyncCounter();

	while (getVSyncCounter() == counter)
		__asm__ volatile("");
}

// Unlike most other CPU architectures, which use "vector tables" or similar
// mechanisms to locate the ISR, MIPS simply jumps to 0x80000080 in RAM when any
// exception occurs (interrupts are considered a type of exception). The layout
// of the BIOS kernel's memory map leaves space for up to 8 instructions at that
// address, so both the stock ISR and our replacement are simple stubs that
// redirect execution to another function to do most of the work.
#define ISR_STUB_ADDR ((uint32_t *) (RAM_BASE | 0x80))
#define ISR_STUB_SIZE 8

void isrStub(void);

static void installCustomISR(void) {
	// Enter a critical section to prevent execution of the code we are about to
	// modify, deferring COP0 configuration to when we return.
	COP0_CRITICAL_SECTION_THEN_SET(COP0_STATUS_IEc | COP0_STATUS_Im2);

	// Overwrite the kernel's stub with a copy of our own version and flush the
	// CPU's instruction cache to clear any leftovers of BIOS code. Cache
	// flushing must be carried out in assembly but is out of the scope of this
	// example - see the implementation in common/cache.s for more details.
	__builtin_memcpy(ISR_STUB_ADDR, isrStub, ISR_STUB_SIZE * 4);
	flushCache();
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

	// Note that initKernelTTY() is unsafe to use in this example due to some
	// TTY drivers buffering output and relying on the kernel-provided ISR to
	// send it in the background.
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
	installCustomISR();

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

	INTC_REG   = ~(1 << IRQ_VSYNC);
	INTC_MASK |=   1 << IRQ_VSYNC;

	GPUDMAChain  dmaChains[2];
	bool         usingSecondFrame = false;
	unsigned int frameCounter     = 0;

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

		if (pollController(0) | pollController(1))
			delayMicroseconds(100000);

		char buffer[64];

		snprintf(
			buffer,
			sizeof(buffer),
			"Main loop counter:\t%u\n"
			"Interrupt counter:\t%u",
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
