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
 * This is a version of the previous example modified to use an indexed color
 * texture instead of a raw one. The idea behind indexed color images is
 * remarkably simple: by limiting the maximum number of unique colors in an
 * image and storing their values separately in a "palette" or CLUT (color
 * lookup table), it is possible to reduce the size of the image data by
 * replacing each pixel color with an index into the palette.
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
#include "common/gpu.h"
#include "ps1/gpucmd.h"
#include "ps1/registers.h"

// We need to add a new entry to this structure to store the CLUT attribute,
// another 16-bit field which will contain the coordinates of our texture's
// palette within VRAM.
typedef struct {
	uint8_t  u, v;
	uint16_t width, height;
	uint16_t page, clut;
} TextureInfo_;

static void uploadIndexedTexture_(
	TextureInfo_  *info,
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

	// Determine how large the palette is and by which factor the image is
	// squished horizontally in VRAM from the color depth.
	int numColors    = (colorDepth == GP0_COLOR_8BPP) ? 256 : 16;
	int widthDivider = (colorDepth == GP0_COLOR_8BPP) ?   1 :  2;

	// Make sure the palette is aligned correctly within VRAM and does not
	// exceed its bounds.
	assert(!(paletteX % 16) && ((paletteX + numColors) <= 1024));

	// Upload the image and palette data separately, then flush any previously
	// used texture from the GPU's internal cache.
	sendVRAMData(image, imageX, imageY, width >> widthDivider, height);
	waitForGPUDMADone();
	sendVRAMData(palette, paletteX, paletteY, numColors, 1);
	waitForGPUDMADone();
	GPU_GP0 = gp0_flushCache();

	// Update the texture page and CLUT attributes to match the VRAM locations
	// of the image and palette respectively.
	info->page = gp0_page(
		imageX /  64,
		imageY / 256,
		GP0_BLEND_SEMITRANS,
		colorDepth
	);
	info->clut = gp0_clut(paletteX / 16, paletteY);

	// UV coordinate calculation is slightly more complex than before. The GPU
	// expects coordinates to be in texture pixels rather than VRAM pixels, so
	// the U coordinate has to be multiplied by the previously computed divider.
	info->u      = (uint8_t)  ((imageX %  64) << widthDivider);
	info->v      = (uint8_t)  (imageY  % 256);
	info->width  = (uint16_t) width;
	info->height = (uint16_t) height;
}

#define SCREEN_HRES   GP1_HRES_320
#define SCREEN_VRES   GP1_VRES_256
#define SCREEN_WIDTH  320
#define SCREEN_HEIGHT 240

#define TEXTURE_WIDTH       32
#define TEXTURE_HEIGHT      32
#define TEXTURE_COLOR_DEPTH GP0_COLOR_4BPP

// The Python script will generate two separate files containing the image and
// palette data respectively, so we're going to embed both into the executable.
extern const uint8_t textureData[], paletteData[];

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

	// Load the texture, placing the image next to the two framebuffers in VRAM
	// and the palette below the image.
	TextureInfo_ texture;

	uploadIndexedTexture_(
		&texture,
		textureData,
		paletteData,
		SCREEN_WIDTH * 2,
		0,
		SCREEN_WIDTH * 2,
		TEXTURE_HEIGHT,
		TEXTURE_WIDTH,
		TEXTURE_HEIGHT,
		TEXTURE_COLOR_DEPTH
	);

	int x = 0, velocityX = 1;
	int y = 0, velocityY = 1;

	GPUDMAChain dmaChains[2];
	bool        usingSecondFrame = false;

	for (;;) {
		int bufferX = usingSecondFrame ? SCREEN_WIDTH : 0;
		int bufferY = 0;

		GPUDMAChain *chain = &dmaChains[usingSecondFrame];
		usingSecondFrame   = !usingSecondFrame;

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

		// Draw the sprite, almost identically to how we did it in the previous
		// example. Notice how the CLUT attribute is being passed to the GPU.
		ptr    = allocateGP0Packet(chain, 5);
		ptr[0] = gp0_setPage(texture.page, false, false);
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
		sendGPULinkedList(chain->data);
	}

	return 0;
}
