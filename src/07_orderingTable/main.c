/*
 * ps1-bare-metal - (C) 2023-2025 spicyjpeg
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
 */

/*
 * We have seen pretty much every core feature of the PS1's GPU at this point.
 * However, an important piece of functionality is still missing: we do not know
 * how to control the order the GPU processes our linked list packets in. This
 * may not sound particularly useful for 2D graphics but is crucial for 3D, as
 * we'll have to sort our polygons by distance to make sure items closest to the
 * camera are drawn last (the GPU has no depth buffer to help with this).
 *
 * Fortunately, linked lists lend themselves well to manipulation and sorting.
 * The DMA unit, which we've only used for its GPU channel so far, includes
 * another channel known as OTC, which can quickly generate a series of empty
 * (header-only) GPU DMA packets linked to each other and write them to RAM.
 * These packets will form what's known as an ordering table, a chain of dummy
 * packets whose purpose is to serve as "anchor points" for other packets to be
 * linked to. By having an ordering table with N items it is thus possible to
 * have N different "buckets" to sort packets into, with the ordering table
 * linking all buckets together and making sure the GPU draws them in order.
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include "gpu.h"
#include "ps1/gpucmd.h"
#include "ps1/registers.h"

#define SCREEN_WIDTH  320
#define SCREEN_HEIGHT 240

int main(int argc, const char **argv) {
	initSerialIO(115200);

	if ((GPU_GP1 & GP1_STAT_FB_MODE_BITMASK) == GP1_STAT_FB_MODE_PAL) {
		puts("Using PAL mode");
		setupGPU(GP1_MODE_PAL, SCREEN_WIDTH, SCREEN_HEIGHT);
	} else {
		puts("Using NTSC mode");
		setupGPU(GP1_MODE_NTSC, SCREEN_WIDTH, SCREEN_HEIGHT);
	}

	DMA_DPCR |= 0
		| DMA_DPCR_CH_ENABLE(DMA_GPU)
		| DMA_DPCR_CH_ENABLE(DMA_OTC);

	GPU_GP1 = gp1_dmaRequestMode(GP1_DREQ_GP0_WRITE);
	GPU_GP1 = gp1_dispBlank(false);

	DMAChain dmaChains[2];
	bool     usingSecondFrame = false;
	int      frameCounter     = 0;

	for (;;) {
		int bufferX = usingSecondFrame ? SCREEN_WIDTH : 0;
		int bufferY = 0;

		DMAChain *chain  = &dmaChains[usingSecondFrame];
		usingSecondFrame = !usingSecondFrame;

		uint32_t *ptr;

		GPU_GP1 = gp1_fbOffset(bufferX, bufferY);

		// Reset the ordering table to a blank state.
		clearOrderingTable(chain->orderingTable, ORDERING_TABLE_SIZE);
		chain->nextPacket = chain->data;

		// Draw 16 stacked squares, animating their Z indices. The packets are
		// always allocated in the same order (top left to bottom right square),
		// but the table will reorder them as they are sent to the GPU.
		int x = 16, y = 24;

		int frontSquareIndex = (frameCounter++ / 10) % 16;

		for (int i = 0; i < 16; i++) {
			uint32_t color = gp0_rgb(i * 15, i * 15, 0);
			int      zIndex;

			if (i < frontSquareIndex)
				zIndex = frontSquareIndex - i;
			else
				zIndex = i - frontSquareIndex;

			ptr    = allocatePacket(chain, zIndex, 3);
			ptr[0] = color | gp0_rectangle(false, false, false);
			ptr[1] = gp0_xy(x, y);
			ptr[2] = gp0_xy(32, 32);

			x += 16;
			y += 10;
		}

		// Place the framebuffer offset and screen clearing commands last, as
		// the "furthest away" items in the table. Since the ordering table is
		// reversed (see the allocatePacket() note), this ensures they'll be
		// executed first.
		ptr    = allocatePacket(chain, ORDERING_TABLE_SIZE - 1, 3);
		ptr[0] = gp0_rgb(64, 64, 64) | gp0_vramFill();
		ptr[1] = gp0_xy(bufferX, bufferY);
		ptr[2] = gp0_xy(SCREEN_WIDTH, SCREEN_HEIGHT);

		ptr    = allocatePacket(chain, ORDERING_TABLE_SIZE - 1, 4);
		ptr[0] = gp0_texpage(0, true, false);
		ptr[1] = gp0_fbOffset1(bufferX, bufferY);
		ptr[2] = gp0_fbOffset2(
			bufferX + SCREEN_WIDTH  - 1,
			bufferY + SCREEN_HEIGHT - 2
		);
		ptr[3] = gp0_fbOrigin(bufferX, bufferY);

		// Give DMA a pointer to the first (last) entry in the table. There is
		// no need to terminate the table manually as the OTC DMA channel
		// already inserts a terminator packet.
		waitForGP0Ready();
		waitForVSync();
		sendLinkedList(&(chain->orderingTable)[ORDERING_TABLE_SIZE - 1]);
	}

	return 0;
}
