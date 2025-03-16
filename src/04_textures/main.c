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
 * The last example showed how to take advantage of the PS1's DMA engine to send
 * commands to the GPU efficiently in the background. While that is by far the
 * most common use case for DMA, the GPU's DMA channel has another crucial role:
 * it allows for fast data transfers from and to VRAM, which are useful for
 * uploading image data to be used as a texture when drawing.
 *
 * This example shows how to upload raw 16bpp RGB image data embedded into the
 * executable to an arbitrary location within the 1024x512 VRAM buffer, store
 * its coordinates into memory and recall them later in order to draw a textured
 * sprite on screen. The process unfortunately involves dealing with a number of
 * GPU idiosyncracies, such as its lack of support for textures larger than
 * 256x256 or its requirement for all textures to be arranged into a grid of
 * 64x256 regions of VRAM known as "texture pages" (a texture may span up to
 * four pages horizontally but only one vertically, so it can't e.g. cross the
 * Y=256 boundary). With those details out of the way, however, using textures
 * boils down to simply performing a DMA transfer and setting the appropriate
 * fields in GP0 commands.
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
		horizontalRes,
		verticalRes,
		mode,
		false,
		GP1_COLOR_16BPP
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
	DMA_CHCR(DMA_GPU) = 0
		| DMA_CHCR_WRITE
		| DMA_CHCR_MODE_LIST
		| DMA_CHCR_ENABLE;
}

#define DMA_MAX_CHUNK_SIZE 16

static void sendVRAMData(
	const void *data,
	int        x,
	int        y,
	int        width,
	int        height
) {
	waitForDMADone();
	assert(!((uint32_t) data % 4));

	// Calculate how many 32-bit words will be sent from the width and height of
	// the texture. If more than 16 words have to be sent, configure DMA to
	// split the transfer into 16-word chunks in order to make sure the GPU will
	// not miss any data.
	size_t length = (width * height) / 2;
	size_t chunkSize, numChunks;

	if (length < DMA_MAX_CHUNK_SIZE) {
		chunkSize = length;
		numChunks = 1;
	} else {
		chunkSize = DMA_MAX_CHUNK_SIZE;
		numChunks = length / DMA_MAX_CHUNK_SIZE;

		// Make sure the length is an exact multiple of 16 words, as otherwise
		// the last chunk would be dropped (the DMA unit does not support
		// "incomplete" chunks). Note that this will impose limitations on the
		// size of VRAM uploads.
		assert(!(length % DMA_MAX_CHUNK_SIZE));
	}

	// Put the GPU into VRAM upload mode by sending the appropriate GP0 command
	// and our coordinates.
	waitForGP0Ready();
	GPU_GP0 = gp0_vramWrite();
	GPU_GP0 = gp0_xy(x, y);
	GPU_GP0 = gp0_xy(width, height);

	// Give DMA a pointer to the beginning of the data and tell it to send it in
	// slice (chunked) mode.
	DMA_MADR(DMA_GPU) = (uint32_t) data;
	DMA_BCR (DMA_GPU) = chunkSize | (numChunks << 16);
	DMA_CHCR(DMA_GPU) = 0
		| DMA_CHCR_WRITE
		| DMA_CHCR_MODE_SLICE
		| DMA_CHCR_ENABLE;
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

// Once our texture has been uploaded to VRAM, we are going to save the metadata
// required to use it for drawing into this structure.
typedef struct {
	uint8_t  u, v;
	uint16_t width, height;
	uint16_t page;
} TextureInfo;

static void uploadTexture(
	TextureInfo *info,
	const void  *data,
	int         x,
	int         y,
	int         width,
	int         height
) {
	// Make sure the texture's size is valid. The GPU does not support textures
	// larger than 256x256 pixels.
	assert((width <= 256) && (height <= 256));

	// Upload the texture to VRAM and wait for the process to complete.
	sendVRAMData(data, x, y, width, height);
	waitForDMADone();

	// Update the "texpage" attribute, a 16-bit field telling the GPU several
	// details about the texture such as which 64x256 page it can be found in,
	// its color depth and how semitransparent pixels shall be blended.
	info->page = gp0_page(
		x /  64,
		y / 256,
		GP0_BLEND_SEMITRANS,
		GP0_COLOR_16BPP
	);

	// Calculate the texture's UV coordinates, i.e. its X/Y coordinates relative
	// to the top left corner of the texture page.
	info->u      = (uint8_t)  (x %  64);
	info->v      = (uint8_t)  (y % 256);
	info->width  = (uint16_t) width;
	info->height = (uint16_t) height;
}

#define SCREEN_WIDTH   320
#define SCREEN_HEIGHT  240
#define TEXTURE_WIDTH   32
#define TEXTURE_HEIGHT  32

// We're going to convert our texture into raw binary data using a Python script
// and embed it into this extern array through CMake. See CMakeLists.txt for
// more details.
extern const uint8_t textureData[];

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

	// Load the texture, placing it next to the two framebuffers in VRAM.
	TextureInfo texture;

	uploadTexture(
		&texture,
		textureData,
		SCREEN_WIDTH * 2,
		0,
		TEXTURE_WIDTH,
		TEXTURE_HEIGHT
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
			bufferX + SCREEN_WIDTH -  1,
			bufferY + SCREEN_HEIGHT - 2
		);
		ptr[3] = gp0_fbOrigin(bufferX, bufferY);

		ptr    = allocatePacket(chain, 3);
		ptr[0] = gp0_rgb(64, 64, 64) | gp0_vramFill();
		ptr[1] = gp0_xy(bufferX, bufferY);
		ptr[2] = gp0_xy(SCREEN_WIDTH, SCREEN_HEIGHT);

		// Use the texture we uploaded to draw a sprite (textured rectangle).
		// Two separate commands have to be sent: a texpage command to apply our
		// texpage attribute and disable dithering, followed by the actual
		// rectangle drawing command.
		ptr    = allocatePacket(chain, 5);
		ptr[0] = gp0_texpage(texture.page, false, false);
		ptr[1] = gp0_rectangle(true, true, false);
		ptr[2] = gp0_xy(x, y);
		ptr[3] = gp0_uv(texture.u, texture.v, 0);
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
