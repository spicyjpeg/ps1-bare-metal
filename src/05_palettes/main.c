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
 * This is a version of the previous example modified to use an indexed color
 * texture instead of a raw one. The idea behind indexed color images is
 * remarkably simple: by limiting the maximum number of unique colors in an
 * image and storing their values separately, it is possible to reduce the size
 * of the image data by replacing each pixel with an index to its color into the
 * so-called CLUT (color lookup table) or palette.
 *
 * The PS1's GPU supports two indexed color formats: 4 bits per pixel (up to 16
 * colors) and 8 bits per pixel (up to 256 colors). 4bpp and 8bpp textures are
 * stored in VRAM "squished" horizontally, taking up half or a quarter of the
 * size of an equivalent 16bpp texture respectively. Palettes are simply 16x1 or
 * 256x1 16bpp images that can be placed anywhere in VRAM, with some minimal
 * restrictions on alignment (their X coordinate must be a multiple of 16). This
 * example shows how to upload a palette to VRAM alongside the image and set the
 * appropriate GP0 attributes in order to let the GPU find and use it.
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

static void waitForDMADone(void) {
	while (DMA_CHCR(DMA_GPU) & DMA_CHCR_ENABLE)
		__asm__ volatile("");
}

static void waitForVSync(void) {
	while (!(IRQ_STAT & (1 << IRQ_VSYNC)))
		__asm__ volatile("");

	IRQ_STAT = ~(1 << IRQ_VSYNC);
}

static void sendLinkedList(const void *data) {
	waitForDMADone();
	assert(!((uint32_t) data % 4));

	DMA_MADR(DMA_GPU) = (uint32_t) data;
	DMA_CHCR(DMA_GPU) = DMA_CHCR_WRITE | DMA_CHCR_MODE_LIST | DMA_CHCR_ENABLE;
}

#define DMA_MAX_CHUNK_SIZE 16

static void sendVRAMData(const void *data, int x, int y, int width, int height) {
	waitForDMADone();
	assert(!((uint32_t) data % 4));

	size_t length = (width * height) / 2;
	size_t chunkSize, numChunks;

	if (length < DMA_MAX_CHUNK_SIZE) {
		chunkSize = length;
		numChunks = 1;
	} else {
		chunkSize = DMA_MAX_CHUNK_SIZE;
		numChunks = length / DMA_MAX_CHUNK_SIZE;

		assert(!(length % DMA_MAX_CHUNK_SIZE));
	}

	waitForGP0Ready();
	GPU_GP0 = gp0_vramWrite();
	GPU_GP0 = gp0_xy(x, y);
	GPU_GP0 = gp0_xy(width, height);

	DMA_MADR(DMA_GPU) = (uint32_t) data;
	DMA_BCR (DMA_GPU) = chunkSize | (numChunks << 16);
	DMA_CHCR(DMA_GPU) = DMA_CHCR_WRITE | DMA_CHCR_MODE_SLICE | DMA_CHCR_ENABLE;
}

#define CHAIN_BUFFER_SIZE 1024

typedef struct {
	uint32_t data[CHAIN_BUFFER_SIZE];
	uint32_t *nextPacket;
} DMAChain;

static uint32_t *allocatePacket(DMAChain *chain, int numCommands) {
	uint32_t *ptr      = chain->nextPacket;
	chain->nextPacket += numCommands + 1;

	*ptr = gp0_tag(numCommands, chain->nextPacket);
	assert(chain->nextPacket < &(chain->data)[CHAIN_BUFFER_SIZE]);

	return &ptr[1];
}

// We need to add a new entry to this structure to store the CLUT attribute,
// another 16-bit field which will contain the coordinates of our texture's
// palette within VRAM.
typedef struct {
	uint8_t  u, v;
	uint16_t width, height;
	uint16_t page, clut;
} TextureInfo;

static void uploadIndexedTexture(
	TextureInfo *info, const void *image, const void *palette, int x, int y,
	int paletteX, int paletteY, int width, int height, GP0ColorDepth colorDepth
) {
	assert((width <= 256) && (height <= 256));

	// Determine how large the palette is and by which factor the image is
	// squished horizontally in VRAM from the color depth.
	int numColors    = (colorDepth == GP0_COLOR_8BPP) ? 256 : 16;
	int widthDivider = (colorDepth == GP0_COLOR_8BPP) ?   2 :  4;

	// Make sure the palette is aligned correctly within VRAM and does not
	// exceed its bounds.
	assert(!(paletteX % 16) && ((paletteX + numColors) <= 1024));

	// Upload the image and palette data separately.
	sendVRAMData(image, x, y, width / widthDivider, height);
	waitForDMADone();
	sendVRAMData(palette, paletteX, paletteY, numColors, 1);
	waitForDMADone();

	// Update the texpage and CLUT attributes to match the location of the image
	// and palette as well as the color depth.
	info->page = gp0_page(
		x / 64, y / 256, GP0_BLEND_SEMITRANS, colorDepth
	);
	info->clut = gp0_clut(paletteX / 16, paletteY);

	// UV coordinate calculation is slightly more complex than before. The GPU
	// expects coordinates to be in texture pixels rather than VRAM pixels, so
	// the U coordinate has to be multiplied by the previously computed divider.
	info->u      = (uint8_t)  ((x % 64) * widthDivider);
	info->v      = (uint8_t)  (y % 256);
	info->width  = (uint16_t) width;
	info->height = (uint16_t) height;
}

#define SCREEN_WIDTH        320
#define SCREEN_HEIGHT       240
#define TEXTURE_WIDTH       32
#define TEXTURE_HEIGHT      32
#define TEXTURE_COLOR_DEPTH GP0_COLOR_4BPP

// The Python script will generate two separate files containing the image and
// palette data respectively, so we're going to embed both into the executable.
extern const uint8_t textureData[], paletteData[];

int main(int argc, const char **argv) {
	initSerialIO(115200);

	if ((GPU_GP1 & GP1_STAT_FB_MODE_BITMASK) == GP1_STAT_FB_MODE_PAL) {
		puts("Using PAL mode");
		setupGPU(GP1_MODE_PAL, SCREEN_WIDTH, SCREEN_HEIGHT);
	} else {
		puts("Using NTSC mode");
		setupGPU(GP1_MODE_NTSC, SCREEN_WIDTH, SCREEN_HEIGHT);
	}

	DMA_DPCR |= DMA_DPCR_ENABLE << (DMA_GPU * 4);

	GPU_GP1 = gp1_dmaRequestMode(GP1_DREQ_GP0_WRITE);
	GPU_GP1 = gp1_dispBlank(false);

	// Load the texture, placing the image next to the two framebuffers in VRAM
	// and the palette below the image.
	TextureInfo texture;

	uploadIndexedTexture(
		&texture, textureData, paletteData, SCREEN_WIDTH * 2, 0,
		SCREEN_WIDTH * 2, TEXTURE_HEIGHT, TEXTURE_WIDTH, TEXTURE_HEIGHT,
		TEXTURE_COLOR_DEPTH
	);

	int x = 0, velocityX = 1;
	int y = 0, velocityY = 1;

	DMAChain dmaChains[2];
	bool     usingSecondFrame = false;

	for (;;) {
		int bufferX = usingSecondFrame ? SCREEN_WIDTH : 0;
		int bufferY = 0;

		DMAChain *chain  = &dmaChains[usingSecondFrame];
		usingSecondFrame = !usingSecondFrame;

		uint32_t *ptr;

		GPU_GP1 = gp1_fbOffset(bufferX, bufferY);

		chain->nextPacket = chain->data;

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

		// Draw the sprite, almost identically to how we did it in the previous
		// example. Notice how the CLUT attribute is being passed to the GPU.
		ptr    = allocatePacket(chain, 5);
		ptr[0] = gp0_texpage(texture.page, false, false);
		ptr[1] = gp0_rectangle(true, true, false);
		ptr[2] = gp0_xy(x, y);
		ptr[3] = gp0_uv(texture.u, texture.v, texture.clut);
		ptr[4] = gp0_xy(texture.width, texture.height);

		*(chain->nextPacket) = gp0_endTag(0);

		x += velocityX;
		y += velocityY;

		if ((x <= 0) || (x >= (SCREEN_WIDTH - texture.width)))
			velocityX = -velocityX;
		if ((y <= 0) || (y >= (SCREEN_HEIGHT - texture.height)))
			velocityY = -velocityY;

		waitForGP0Ready();
		waitForVSync();
		sendLinkedList(chain->data);
	}

	return 0;
}
