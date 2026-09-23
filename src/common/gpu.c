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

#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "common/gpu.h"
#include "ps1/gpucmd.h"
#include "ps1/registers.h"

#define DMA_MAX_CHUNK_SIZE 16

void setupGPU(
	GP1VideoMode     mode,
	GP1HorizontalRes horizontalRes,
	GP1VerticalRes   verticalRes,
	unsigned int     width,
	unsigned int     height
) {
	int x = 0x760;
	int y = (mode == GP1_MODE_PAL) ? 0xa3 : 0x88;

	int offsetX = (width  * gp1_clockMultiplierH(horizontalRes)) / 2;
	int offsetY = (height / gp1_clockDividerV(verticalRes))      / 2;

	GPU_GP1 = gp1_resetGPU();
	GPU_GP1 = gp1_fbRangeH(x - offsetX, x + offsetX);
	GPU_GP1 = gp1_fbRangeV(y - offsetY, y + offsetY);
	GPU_GP1 = gp1_fbMode(
		horizontalRes,
		verticalRes,
		mode,
		verticalRes == GP1_VRES_512,
		GP1_COLOR_16BPP
	);
	GPU_GP1 = gp1_dispBlank(false);

	IRQ_MASK &= ~(1 << IRQ_VSYNC);

	DMA_DPCR         |= 0
		| DMA_DPCR_CH_ENABLE(DMA_GPU)
		| DMA_DPCR_CH_ENABLE(DMA_OTC);
	DMA_CHCR(DMA_GPU) = 0;
	DMA_CHCR(DMA_OTC) = 0;
}

void waitForGP0Ready(void) {
	while (!(GPU_STAT & GPU_STAT_WRITE_READY))
		__asm__ volatile("");
}

void waitForGPUDMADone(void) {
	while (DMA_CHCR(DMA_GPU) & DMA_CHCR_ENABLE)
		__asm__ volatile("");
	while (!(GPU_STAT & GPU_STAT_WRITE_READY))
		__asm__ volatile("");
}

void waitForVSync(void) {
	while (!(IRQ_STAT & (1 << IRQ_VSYNC)))
		__asm__ volatile("");

	IRQ_STAT = ~(1 << IRQ_VSYNC);
}

void sendGPULinkedList(const void *data) {
	waitForGPUDMADone();
	assert(!((uintptr_t) data % 4));

	GPU_GP1 = gp1_dmaRequestMode(GP1_DREQ_GP0_WRITE);

	DMA_MADR(DMA_GPU) = (uintptr_t) data;
	DMA_CHCR(DMA_GPU) = 0
		| DMA_CHCR_WRITE
		| DMA_CHCR_MODE_LIST
		| DMA_CHCR_ENABLE;
}

void sendVRAMData(
	const void   *data,
	unsigned int x,
	unsigned int y,
	unsigned int width,
	unsigned int height
) {
	waitForGPUDMADone();
	assert(!((uintptr_t) data % 4));

	size_t length = (width * height + 1) / 2;
	size_t chunkSize, numChunks;

	if (length < DMA_MAX_CHUNK_SIZE) {
		chunkSize = length;
		numChunks = 1;
	} else {
		chunkSize = DMA_MAX_CHUNK_SIZE;
		numChunks = length / DMA_MAX_CHUNK_SIZE;

		assert(!(length % DMA_MAX_CHUNK_SIZE));
	}

	GPU_GP1 = gp1_dmaRequestMode(GP1_DREQ_NONE);

	waitForGP0Ready();
	GPU_GP0 = gp0_vramWrite();
	GPU_GP0 = gp0_xy(x, y);
	GPU_GP0 = gp0_xy(width, height);

	GPU_GP1 = gp1_dmaRequestMode(GP1_DREQ_GP0_WRITE);

	DMA_MADR(DMA_GPU) = (uintptr_t) data;
	DMA_BCR (DMA_GPU) = chunkSize | (numChunks << 16);
	DMA_CHCR(DMA_GPU) = 0
		| DMA_CHCR_WRITE
		| DMA_CHCR_MODE_SLICE
		| DMA_CHCR_ENABLE;
}

void receiveVRAMData(
	void         *data,
	unsigned int x,
	unsigned int y,
	unsigned int width,
	unsigned int height
) {
	waitForGPUDMADone();
	assert(!((uintptr_t) data % 4));

	size_t length = (width * height + 1) / 2;
	size_t chunkSize, numChunks;

	if (length < DMA_MAX_CHUNK_SIZE) {
		chunkSize = length;
		numChunks = 1;
	} else {
		chunkSize = DMA_MAX_CHUNK_SIZE;
		numChunks = length / DMA_MAX_CHUNK_SIZE;

		assert(!(length % DMA_MAX_CHUNK_SIZE));
	}

	GPU_GP1 = gp1_dmaRequestMode(GP1_DREQ_NONE);

	waitForGP0Ready();
	GPU_GP0 = gp0_vramRead();
	GPU_GP0 = gp0_xy(x, y);
	GPU_GP0 = gp0_xy(width, height);

	GPU_GP1 = gp1_dmaRequestMode(GP1_DREQ_GP0_READ);

	DMA_MADR(DMA_GPU) = (uintptr_t) data;
	DMA_BCR (DMA_GPU) = chunkSize | (numChunks << 16);
	DMA_CHCR(DMA_GPU) = 0
		| DMA_CHCR_READ
		| DMA_CHCR_MODE_SLICE
		| DMA_CHCR_ENABLE;
}

void clearOrderingTable(uint32_t *table, size_t numEntries) {
	DMA_MADR(DMA_OTC) = (uintptr_t) &table[numEntries - 1];
	DMA_BCR (DMA_OTC) = numEntries;
	DMA_CHCR(DMA_OTC) = 0
		| DMA_CHCR_READ
		| DMA_CHCR_REVERSE
		| DMA_CHCR_MODE_BURST
		| DMA_CHCR_ENABLE
		| DMA_CHCR_TRIGGER;

	while (DMA_CHCR(DMA_OTC) & DMA_CHCR_ENABLE)
		__asm__ volatile("");
}

uint32_t *allocateGP0Packet(GPUDMAChain *chain, size_t numCommands) {
	assert(numCommands <= DMA_MAX_CHUNK_SIZE);

	uint32_t *ptr      = chain->nextPacket;
	chain->nextPacket += numCommands + 1;

	*ptr = gp0_tag(numCommands, chain->nextPacket);
	assert(chain->nextPacket < &(chain->data)[GPU_CHAIN_BUFFER_SIZE]);

	return &ptr[1];
}

uint32_t *allocateOrderedGP0Packet(
	GPUOrderedDMAChain *chain,
	unsigned int       zIndex,
	size_t             numCommands
) {
	assert(numCommands <= DMA_MAX_CHUNK_SIZE);
	assert(zIndex      <  GPU_ORDERING_TABLE_SIZE);

	uint32_t *ptr      = chain->nextPacket;
	chain->nextPacket += numCommands + 1;

	*ptr = gp0_tag(numCommands, (void *) chain->orderingTable[zIndex]);
	chain->orderingTable[zIndex] = gp0_tag(0, ptr);

	assert(chain->nextPacket < &(chain->data)[GPU_CHAIN_BUFFER_SIZE]);

	return &ptr[1];
}

void uploadTexture(
	TextureInfo  *info,
	const void   *data,
	unsigned int x,
	unsigned int y,
	unsigned int width,
	unsigned int height
) {
	assert((width <= 256) && (height <= 256));

	sendVRAMData(data, x, y, width, height);
	waitForGPUDMADone();
	GPU_GP0 = gp0_flushCache();

	info->page   = gp0_page(
		x /  64,
		y / 256,
		GP0_BLEND_SEMITRANS,
		GP0_COLOR_16BPP
	);
	info->clut   = 0;
	info->u      = (uint8_t)  (x %  64);
	info->v      = (uint8_t)  (y % 256);
	info->width  = (uint16_t) width;
	info->height = (uint16_t) height;
}

void uploadIndexedTexture(
	TextureInfo   *info,
	const void    *image,
	const void    *palette,
	unsigned int  imageX,
	unsigned int  imageY,
	unsigned int  paletteX,
	unsigned int  paletteY,
	unsigned int  width,
	unsigned int  height,
	GP0ColorDepth colorDepth
) {
	assert((width <= 256) && (height <= 256));

	int numColors    = (colorDepth == GP0_COLOR_8BPP) ? 256 : 16;
	int widthDivider = (colorDepth == GP0_COLOR_8BPP) ?   1 :  2;

	assert(!(paletteX % 16) && ((paletteX + numColors) <= 1024));

	sendVRAMData(image, imageX, imageY, width >> widthDivider, height);
	waitForGPUDMADone();
	sendVRAMData(palette, paletteX, paletteY, numColors, 1);
	waitForGPUDMADone();
	GPU_GP0 = gp0_flushCache();

	info->page   = gp0_page(
		imageX /  64,
		imageY / 256,
		GP0_BLEND_SEMITRANS,
		colorDepth
	);
	info->clut   = gp0_clut(paletteX / 16, paletteY);
	info->u      = (uint8_t)  ((imageX %  64) << widthDivider);
	info->v      = (uint8_t)  (imageY  % 256);
	info->width  = (uint16_t) width;
	info->height = (uint16_t) height;
}
