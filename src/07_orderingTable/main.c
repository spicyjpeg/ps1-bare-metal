/*
 * ps1-bare-metal - (C) 2023-2026 spicyjpeg
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

#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include "common/gpu.h"
#include "ps1/gpucmd.h"
#include "ps1/registers.h"

// We are going to store the ordering table for each frame as part of the DMA
// chain structure. We'll have 32 different Z indices at our disposal.
#define DMA_MAX_CHUNK_SIZE       16
#define GPU_ORDERING_TABLE_SIZE_ 32

typedef struct {
	uint32_t data[GPU_CHAIN_BUFFER_SIZE];
	uint32_t orderingTable[GPU_ORDERING_TABLE_SIZE_];
	uint32_t *nextPacket;
} GPUOrderedDMAChain_;

static void clearOrderingTable_(uint32_t *table, size_t numEntries) {
	// Set up the OTC DMA channel to write a new empty ordering table to RAM.
	// The table is always reversed and generated "backwards" (the last item in
	// the table is the first one that will be written), so we must give DMA a
	// pointer to the end of the table rather than its beginning.
	DMA_MADR(DMA_OTC) = (uintptr_t) &table[numEntries - 1];
	DMA_BCR (DMA_OTC) = numEntries;
	DMA_CHCR(DMA_OTC) = 0
		| DMA_CHCR_READ
		| DMA_CHCR_REVERSE
		| DMA_CHCR_MODE_BURST
		| DMA_CHCR_ENABLE
		| DMA_CHCR_TRIGGER;

	// Wait for DMA to finish generating the table.
	while (DMA_CHCR(DMA_OTC) & DMA_CHCR_ENABLE)
		__asm__ volatile("");
}

// As we're using an ordering table, allocateGP0Packet() now takes the packet's
// Z index (i.e. the index of the "bucket" to link it to) as an argument. The
// table is reversed, so packets with higher Z values will be drawn first and
// between two packets with the same Z index the most recently added one will
// take precedence.
static uint32_t *allocateOrderedGP0Packet_(
	GPUOrderedDMAChain_ *chain,
	unsigned int        zIndex,
	size_t              numCommands
) {
	// Ensure both the packet length and index are within valid range.
	assert(numCommands <= DMA_MAX_CHUNK_SIZE);
	assert(zIndex      <  GPU_ORDERING_TABLE_SIZE_);

	uint32_t *ptr      = chain->nextPacket;
	chain->nextPacket += numCommands + 1;

	// Splice the new packet into the ordering table by:
	// - taking the address the ordering table entry currently points to;
	// - replacing that address with a pointer to the packet;
	// - linking the packet to the old address.
	*ptr = gp0_tag(numCommands, (void *) chain->orderingTable[zIndex]);
	chain->orderingTable[zIndex] = gp0_tag(0, ptr);

	assert(chain->nextPacket < &(chain->data)[GPU_CHAIN_BUFFER_SIZE]);

	return &ptr[1];
}

#define SCREEN_HRES   GP1_HRES_320
#define SCREEN_VRES   GP1_VRES_256
#define SCREEN_WIDTH  320
#define SCREEN_HEIGHT 240

int main(int argc, const char **argv) {
	(void) argc;
	(void) argv;

	initSerialIO(115200);
	setupGPU(
		getCurrentVideoMode(),
		SCREEN_HRES,
		SCREEN_VRES,
		SCREEN_WIDTH,
		SCREEN_HEIGHT
	);

	GPUOrderedDMAChain_ dmaChains[2];
	bool                usingSecondFrame = false;
	int                 frameCounter     = 0;

	for (;;) {
		int bufferX = usingSecondFrame ? SCREEN_WIDTH : 0;
		int bufferY = 0;

		GPUOrderedDMAChain_ *chain = &dmaChains[usingSecondFrame];
		usingSecondFrame           = !usingSecondFrame;

		uint32_t *ptr;

		GPU_GP1 = gp1_fbOffset(bufferX, bufferY);

		// Reset the ordering table to a blank state.
		clearOrderingTable_(chain->orderingTable, GPU_ORDERING_TABLE_SIZE_);
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

			ptr    = allocateOrderedGP0Packet_(chain, zIndex, 3);
			ptr[0] = color | gp0_rectangle(false, false, false);
			ptr[1] = gp0_xy(x, y);
			ptr[2] = gp0_xy(32, 32);

			x += 16;
			y += 10;
		}

		// Place the framebuffer offset and screen clearing commands last, as
		// the "furthest away" items in the table. Since the ordering table is
		// reversed (see the allocateGP0Packet() note), this ensures they'll be
		// executed first.
		ptr    =
			allocateOrderedGP0Packet_(chain, GPU_ORDERING_TABLE_SIZE_ - 1, 3);
		ptr[0] = gp0_rgb(64, 64, 64) | gp0_vramFill();
		ptr[1] = gp0_xy(bufferX, bufferY);
		ptr[2] = gp0_xy(SCREEN_WIDTH, SCREEN_HEIGHT);

		ptr    =
			allocateOrderedGP0Packet_(chain, GPU_ORDERING_TABLE_SIZE_ - 1, 4);
		ptr[0] = gp0_setPage(0, true, false);
		ptr[1] = gp0_fbOffset1(bufferX, bufferY);
		ptr[2] = gp0_fbOffset2(
			bufferX + SCREEN_WIDTH  - 1,
			bufferY + SCREEN_HEIGHT - 1
		);
		ptr[3] = gp0_fbOrigin(bufferX, bufferY);

		// Give DMA a pointer to the first (last) entry in the table. There is
		// no need to terminate the table manually as the OTC DMA channel
		// already inserts a terminator packet.
		waitForGP0Ready();
		waitForVSync();
		sendGPULinkedList(&(chain->orderingTable)[GPU_ORDERING_TABLE_SIZE_ - 1]);
	}

	return 0;
}
