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

#pragma once

#include <stdint.h>
#include "ps1/gpucmd.h"
#include "ps1/registers.h"

// In order for GTE Z averaging to work properly, GPU_ORDERING_TABLE_SIZE should
// be set to either a relatively high value (1024 or more) or a multiple of 12
// (i.e. both 3 and 4). Higher values will take up more memory but are required
// to render more complex scenes with wide depth ranges correctly.
#define GPU_CHAIN_BUFFER_SIZE   2048
#define GPU_ORDERING_TABLE_SIZE  240

typedef struct {
	uint32_t data[GPU_CHAIN_BUFFER_SIZE];
	uint32_t *nextPacket;
} GPUDMAChain;

typedef struct {
	uint32_t data[GPU_CHAIN_BUFFER_SIZE];
	uint32_t orderingTable[GPU_ORDERING_TABLE_SIZE];
	uint32_t *nextPacket;
} GPUOrderedDMAChain;

typedef struct {
	uint8_t  u, v;
	uint16_t width, height;
	uint16_t page, clut;
} TextureInfo;

#ifdef __cplusplus
extern "C" {
#endif

static inline GP1VideoMode getCurrentVideoMode(void) {
	return ((GPU_GP1 & GP1_STAT_FB_MODE_BITMASK) == GP1_STAT_FB_MODE_PAL)
		? GP1_MODE_PAL
		: GP1_MODE_NTSC;
}

void setupGPU(
	GP1VideoMode     mode,
	GP1HorizontalRes horizontalRes,
	GP1VerticalRes   verticalRes,
	int              width,
	int              height
);
void waitForGP0Ready(void);
void waitForGPUDMADone(void);
void waitForVSync(void);

void sendGPULinkedList(const void *data);
void sendVRAMData(
	const void *data,
	int        x,
	int        y,
	int        width,
	int        height
);
void receiveVRAMData(
	void *data,
	int  x,
	int  y,
	int  width,
	int  height
);

void clearOrderingTable(uint32_t *table, int numEntries);
uint32_t *allocateGP0Packet(GPUDMAChain *chain, int numCommands);
uint32_t *allocateOrderedGP0Packet(
	GPUOrderedDMAChain *chain,
	int                zIndex,
	int                numCommands
);

void uploadTexture(
	TextureInfo *info,
	const void  *data,
	int         x,
	int         y,
	int         width,
	int         height
);
void uploadIndexedTexture(
	TextureInfo   *info,
	const void    *image,
	const void    *palette,
	int           imageX,
	int           imageY,
	int           paletteX,
	int           paletteY,
	int           width,
	int           height,
	GP0ColorDepth colorDepth
);

#ifdef __cplusplus
}
#endif
