/*
 * ps1-bare-metal - (C) 2023 spicyjpeg
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES WITH
 * REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY
 * AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY SPECIAL, DIRECT,
 * INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM
 * LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR
 * OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 * PERFORMANCE OF THIS SOFTWARE.
 *
 *
 * In the previous two examples we saw how to control the GPU and draw graphics
 * by writing commands directly to the GP0 and GP1 registers. While this
 * approach is simple and easy to understand, it also limits performance: the
 * CPU always has to wait for the GPU to go idle before being able to send it a
 * new command, otherwise the GPU may miss it; similarly, the GPU can only
 * process commands while the CPU is actively feeding it.
 *
 * To get around these limitations the GPU (along with most of the PS1's other
 * peripherals) can instead be given access to RAM and read GP0 commands from it
 * automatically, without needing the CPU to feed it. This is accomplished
 * through a middleman peripheral known as the direct memory access (DMA)
 * controller, and is the key to high-performance graphics on the PS1. We'll see
 * how to allocate a buffer in RAM, fill it with commands and then set up the
 * GPU's DMA channel to read from it in the background while we're preparing the
 * next frame's command buffer.
 */

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include "ps1/gpucmd.h"
#include "ps1/registers.h"

static void setupGPU(GP1VideoMode mode, int width, int height) {
	int x = 0x760;
	int y = (mode == GP1_MODE_PAL) ? 0xa3 : 0x88;

	GP1HorizontalRes horizontalRes = GP1_HRES_320;
	GP1VerticalRes   verticalRes   = GP1_VRES_256;

	int offsetX = (width  * gp1_clockMultiplierH(horizontalRes)) / 2;
	int offsetY = (height / gp1_clockDividerV(verticalRes))      / 2;

	GPU_GP1 = gp1_resetGPU();
	GPU_GP1 = gp1_fbRangeH(x - offsetX, x + offsetX);
	GPU_GP1 = gp1_fbRangeV(y - offsetY, y + offsetY);
	GPU_GP1 = gp1_fbMode(
		horizontalRes, verticalRes, mode, false, GP1_COLOR_16BPP
	);
}

static void waitForGP0Ready(void) {
	while (!(GPU_GP1 & GP1_STAT_CMD_READY))
		__asm__ volatile("");
}

static void waitForVSync(void) {
	while (!(IRQ_STAT & (1 << IRQ_VSYNC)))
		__asm__ volatile("");

	IRQ_STAT = ~(1 << IRQ_VSYNC);
}

static void sendLinkedList(const void *data) {
	// Wait until the GPU's DMA unit has finished sending data and is ready.
	while (DMA_CHCR(DMA_GPU) & DMA_CHCR_ENABLE)
		__asm__ volatile("");

	// Make sure the pointer is aligned to 32 bits (4 bytes). The DMA engine is
	// not capable of reading unaligned data.
	assert(!((uint32_t) data % 4));

	// Give DMA a pointer to the beginning of the data and tell it to send it in
	// linked list mode. The DMA unit will start parsing a chain of "packets"
	// from RAM, with each packet being made up of a 32-bit header followed by
	// zero or more 32-bit commands to be sent to the GP0 register.
	DMA_MADR(DMA_GPU) = (uint32_t) data;
	DMA_CHCR(DMA_GPU) = DMA_CHCR_WRITE | DMA_CHCR_MODE_LIST | DMA_CHCR_ENABLE;
}

// Define a structure we'll allocate our linked list packets into. We are going
// to use a fixed-size buffer and keep a pointer to the beginning of its free
// area, incrementing it whenever we allocate a new packet.
#define CHAIN_BUFFER_SIZE 1024

typedef struct {
	uint32_t data[CHAIN_BUFFER_SIZE];
	uint32_t *nextPacket;
} DMAChain;

static uint32_t *allocatePacket(DMAChain *chain, int numCommands) {
	// Grab the current pointer to the next packet then increment it to allocate
	// a new packet. We have to allocate an extra word for the packet's header,
	// which will contain the number of GP0 commands the packet is made up of as
	// well as a pointer to the next packet (or a special "terminator" value to
	// tell the DMA unit to stop).
	uint32_t *ptr      = chain->nextPacket;
	chain->nextPacket += numCommands + 1;

	// Write the header and set its pointer to point to the next packet that
	// will be allocated in the buffer.
	*ptr = gp0_tag(numCommands, chain->nextPacket);

	// Make sure we haven't yet run out of space for future packets or a linked
	// list terminator, then return a pointer to the packet's first GP0 command.
	assert(chain->nextPacket < &(chain->data)[CHAIN_BUFFER_SIZE]);

	return &ptr[1];
}

#define SCREEN_WIDTH  320
#define SCREEN_HEIGHT 240

int main(int argc, const char **argv) {
	initSerialIO(115200);

	if ((GPU_GP1 & GP1_STAT_MODE_BITMASK) == GP1_STAT_MODE_PAL) {
		puts("Using PAL mode");
		setupGPU(GP1_MODE_PAL, SCREEN_WIDTH, SCREEN_HEIGHT);
	} else {
		puts("Using NTSC mode");
		setupGPU(GP1_MODE_NTSC, SCREEN_WIDTH, SCREEN_HEIGHT);
	}

	// Enable the GPU's DMA channel, tell the GPU to fetch GP0 commands from DMA
	// whenever available and switch on the display output.
	DMA_DPCR |= DMA_DPCR_ENABLE << (DMA_GPU * 4);

	GPU_GP1 = gp1_dmaRequestMode(GP1_DREQ_GP0_WRITE);
	GPU_GP1 = gp1_dispBlank(false);

	int x = 0, velocityX = 1;
	int y = 0, velocityY = 1;

	// Allocate a double command buffer in order to let the GPU keep fetching
	// commands while the CPU is filling up the other buffer. See the previous
	// example for more details on double buffering.
	DMAChain dmaChains[2];
	bool     usingSecondFrame = false;

	for (;;) {
		int bufferX = usingSecondFrame ? SCREEN_WIDTH : 0;
		int bufferY = 0;

		DMAChain *chain  = &dmaChains[usingSecondFrame];
		usingSecondFrame = !usingSecondFrame;

		uint32_t *ptr;

		// Display the frame that was just drawn by the GPU (if any). We are
		// going to overwrite its respective DMA chain afterwards, as the GPU no
		// longer needs it.
		GPU_GP1 = gp1_fbOffset(bufferX, bufferY);

		// Reset the pointer to the next packet to the beginning of the buffer.
		chain->nextPacket = chain->data;

		// Create a new DMA packet for each GP0 command we're sending. Splitting
		// up each command like this will make sure the DMA channel won't try to
		// send them too quickly and end up overflowing the GPU's internal
		// command processor.
		ptr    = allocatePacket(chain, 4);
		ptr[0] = gp0_texpage(0, true, false);
		ptr[1] = gp0_fbOffset1(bufferX, bufferY);
		ptr[2] = gp0_fbOffset2(
			bufferX + SCREEN_WIDTH - 1, bufferY + SCREEN_HEIGHT - 2
		);
		ptr[3] = gp0_fbOrigin(bufferX, bufferY);

		ptr    = allocatePacket(chain, 3);
		ptr[0] = gp0_rgb(64, 64, 64) | gp0_vramFill();
		ptr[1] = gp0_xy(bufferX, bufferY);
		ptr[2] = gp0_xy(SCREEN_WIDTH, SCREEN_HEIGHT);

		ptr    = allocatePacket(chain, 3);
		ptr[0] = gp0_rgb(255, 255, 0) | gp0_rectangle(false, false, false);
		ptr[1] = gp0_xy(x, y);
		ptr[2] = gp0_xy(32, 32);

		// Terminate the linked list and tell the DMA engine to stop reading by
		// appending a terminator header to it.
		*(chain->nextPacket) = gp0_endTag(0);

		x += velocityX;
		y += velocityY;

		if ((x <= 0) || (x >= (SCREEN_WIDTH - 32)))
			velocityX = -velocityX;
		if ((y <= 0) || (y >= (SCREEN_HEIGHT - 32)))
			velocityY = -velocityY;

		// Wait for the previous frame to be displayed, then start sending the
		// newly built DMA chain in the background while the next iteration of
		// the main loop is going to run.
		waitForGP0Ready();
		waitForVSync();
		sendLinkedList(chain->data);
	}

	return 0;
}
