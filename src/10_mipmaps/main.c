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
 * So far we have only seen how to render simple untextured 3D models. While it
 * would be trivial to add a texture to one of our examples by storing UV
 * coordinates alongside vertices and copying them over to each GP0 packet, in
 * an actual game with complex models we'd actually want to do something more
 * refined: swap the texture out for a smaller copy for faces that are far away
 * or otherwise too small for the full-resolution version. This helps with both
 * performance and quality, as the GPU's nearest-neighbor sampling would
 * otherwise skip pixels and drop details when scaling large textures down.
 *
 * This is a common practice in 3D graphics known as mipmapping. While modern
 * platforms handle it natively in hardware, on the PS1 we must prepare the
 * downscaled texture variants (mip levels) manually and implement the switching
 * in software. Luckily, the GTE provides some help by allowing us to carry out
 * the most computationally expensive steps of the mipmap lookup process very
 * quickly. In this example we'll use a texture with four different mip levels,
 * packed ahead of time into a single spritesheet.
 */

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include "common/gpu.h"
#include "common/gte.h"
#include "ps1/gpucmd.h"
#include "ps1/gte.h"
#include "ps1/registers.h"

// As with the font example, we're going to use a table to pick each mipmap out
// of our spritesheet by UV offset.
typedef struct {
	uint8_t x0, y0, x1, y1;
} MipmapInfo;

static const MipmapInfo mipmaps[] = {
	{ .x0 = 64, .y0 = 48, .x1 = 64 +  7, .y1 = 48 +  7 }, // 8x8   (2^3)
	{ .x0 = 64, .y0 = 32, .x1 = 64 + 15, .y1 = 32 + 15 }, // 16x16 (2^4)
	{ .x0 = 64, .y0 =  0, .x1 = 64 + 31, .y1 =  0 + 31 }, // 32x32 (2^5)
	{ .x0 =  0, .y0 =  0, .x1 =  0 + 63, .y1 =  0 + 63 }  // 64x64 (2^6)
};

#define NUM_MIPMAPS      (sizeof(mipmaps) / sizeof(MipmapInfo))
#define FIRST_MIPMAP_EXP 3

// We need a fast but accurate heuristic for determining which mip level to use
// for each polygon drawn. We can use the area estimated by the backface culling
// pass: since our mipmaps are square and power-of-2 sized, computing its square
// root will give us the side length, and calculating the base-2 logarithm of
// that (rounded up to the nearest integer exponent) will in turn give us an
// index into our mipmap table.
static inline unsigned int ceilLog2_(int x) {
	assert(x > 0);

	// The GTE implements fast binary "count leading zeroes" functionality,
	// which is mathematically equivalent to (31 - floor(log2(x))). With some
	// adjustments, we can use it to quickly compute ceil(log2(x)) instead.
	gte_setDataReg(GTE_LZCS, x - 1);
	gte_loadDelay();
	return 32 - gte_getDataReg(GTE_LZCR);
}

static inline const MipmapInfo *getBestMipmap(int area) {
	// We can skip the square root by moving it out of the logarithm, at which
	// point it'll become a simple division by 2. Observe how the calculation
	// works for all our mipmap sizes:
	//     log2(sqrt( 8 *  8)) == log2( 8 *  8) / 2 == 3
	//     log2(sqrt(16 * 16)) == log2(16 * 16) / 2 == 4
	//     log2(sqrt(32 * 32)) == log2(32 * 32) / 2 == 5
	//     log2(sqrt(64 * 64)) == log2(64 * 64) / 2 == 6
	int index = ceilLog2_(area) / 2 - FIRST_MIPMAP_EXP;

	// Clamp the resulting index to the bounds of our table, in order to handle
	// faces that are too small or large.
	if (index < 0)
		index = 0;
	else if (index >= (int) NUM_MIPMAPS)
		index = NUM_MIPMAPS - 1;

	return &mipmaps[index];
}

// We'll use a procedurally generated model of a large subdivided plane to
// better demonstrate the effect (though mipmapping will work just fine on any
// other model, procedural or not). For simplicity's sake we'll assume each face
// has the same UVs and is not blended with a color.
#define PLANE_FACE_SIZE   128
#define NUM_PLANE_COLUMNS  10
#define NUM_PLANE_ROWS     10

#define PLANE_WIDTH        (NUM_PLANE_COLUMNS * PLANE_FACE_SIZE)
#define PLANE_HEIGHT       (NUM_PLANE_ROWS    * PLANE_FACE_SIZE)
#define NUM_PLANE_VERTICES ((NUM_PLANE_COLUMNS + 1) * (NUM_PLANE_ROWS + 1))
#define NUM_PLANE_FACES    (NUM_PLANE_COLUMNS       * NUM_PLANE_ROWS)

typedef struct {
	uint8_t vertices[4];
} Face;

static GTEVector16 planeVertices[NUM_PLANE_VERTICES];
static Face        planeFaces   [NUM_PLANE_FACES];

static void generatePlaneModel(void) {
	// Generate the vertex grid front-to-back.
	for (int i = 0; i <= NUM_PLANE_ROWS; i++) {
		for (int j = 0; j <= NUM_PLANE_COLUMNS; j++) {
			GTEVector16 *vertex =
				&planeVertices[(NUM_PLANE_COLUMNS + 1) * i + j];

			vertex->x = (int16_t) (PLANE_FACE_SIZE * j - PLANE_WIDTH  / 2);
			vertex->y = 0;
			vertex->z = (int16_t) (PLANE_FACE_SIZE * i - PLANE_HEIGHT / 2);
		}
	}

	// Join each 2x2 group of adjacent vertices into a face.
	for (int i = 0; i < NUM_PLANE_ROWS; i++) {
		int topRow    = (NUM_PLANE_COLUMNS + 1) * (i + 1);
		int bottomRow = (NUM_PLANE_COLUMNS + 1) * i;

		for (int j = 0; j < NUM_PLANE_COLUMNS; j++) {
			Face *face = &planeFaces[NUM_PLANE_COLUMNS * i + j];

			face->vertices[0] = topRow    + j;
			face->vertices[1] = topRow    + j + 1;
			face->vertices[2] = bottomRow + j;
			face->vertices[3] = bottomRow + j + 1;
		}
	}
}

#define SCREEN_HRES   GP1_HRES_320
#define SCREEN_VRES   GP1_VRES_256
#define SCREEN_WIDTH  320
#define SCREEN_HEIGHT 240

#define TEXTURE_WIDTH       96
#define TEXTURE_HEIGHT      64
#define TEXTURE_COLOR_DEPTH GP0_COLOR_8BPP

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
	setupGTE(SCREEN_WIDTH, SCREEN_HEIGHT);

	TextureInfo texture;

	uploadIndexedTexture(
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

	// We are not going to rotate the plane, so the rotation matrix only needs
	// to be initialized once.
	generatePlaneModel();
	gte_setRotationMatrix(
		GTE_UNIT,        0,        0,
		       0, GTE_UNIT,        0,
		       0,        0, GTE_UNIT
	);

	GPUOrderedDMAChain dmaChains[2];
	bool               usingSecondFrame = false;
	int                frameCounter     = 0;

	for (;;) {
		int bufferX = usingSecondFrame ? SCREEN_WIDTH : 0;
		int bufferY = 0;

		GPUOrderedDMAChain *chain = &dmaChains[usingSecondFrame];
		usingSecondFrame          = !usingSecondFrame;
		frameCounter++;

		uint32_t *ptr;

		GPU_GP1 = gp1_fbOffset(bufferX, bufferY);

		clearOrderingTable(chain->orderingTable, GPU_ORDERING_TABLE_SIZE);
		chain->nextPacket = chain->data;

		// Animate the plane's distance from the camera, wrapping after one face
		// to simulate an endless plane.
		gte_setControlReg(GTE_TRX, 0);
		gte_setControlReg(GTE_TRY, PLANE_FACE_SIZE);
		gte_setControlReg(
			GTE_TRZ,
			PLANE_HEIGHT / 2 - frameCounter % PLANE_FACE_SIZE
		);

		for (int i = 0; i < NUM_PLANE_FACES; i++) {
			const Face *face = &planeFaces[i];

			gte_loadV0(&planeVertices[face->vertices[0]]);
			gte_loadV1(&planeVertices[face->vertices[1]]);
			gte_loadV2(&planeVertices[face->vertices[2]]);
			gte_command(GTE_CMD_RTPT | GTE_SF);

			// The value returned here is double the area of the quad's first
			// triangle, so we can treat it as a rough estimation of the area of
			// the entire quad.
			gte_command(GTE_CMD_NCLIP);
			int area = (int) gte_getDataReg(GTE_MAC0);

			if (area <= 0)
				continue;

			uint32_t xy0 = gte_getDataReg(GTE_SXY0);

			gte_loadV0(&planeVertices[face->vertices[3]]);
			gte_command(GTE_CMD_RTPS | GTE_SF);

			gte_command(GTE_CMD_AVSZ4 | GTE_SF);
			int zIndex = (int) gte_getDataReg(GTE_OTZ);

			if ((zIndex < 0) || (zIndex >= GPU_ORDERING_TABLE_SIZE))
				continue;

			// Pick a mip level for the face and add its offset to the
			// spritesheet's base UV coordinates. An optimized implementation
			// would mutate the mipmap UV table in-place ahead of time (or align
			// the spritesheet to texture page boundaries) to remove the need
			// for per-face UV calculations.
			const MipmapInfo *mipmap = getBestMipmap(area);

			int u0 = texture.u + mipmap->x0;
			int v0 = texture.v + mipmap->y0;
			int u1 = texture.u + mipmap->x1;
			int v1 = texture.v + mipmap->y1;

			ptr    = allocateOrderedGP0Packet(chain, zIndex, 9);
			ptr[0] = gp0_quad(true, false);
			ptr[1] = xy0;
			ptr[2] = gp0_uv(u0, v0, texture.clut);
			gte_storeDataReg(GTE_SXY0, 3 * 4, ptr);
			ptr[4] = gp0_uv(u1, v0, texture.page);
			gte_storeDataReg(GTE_SXY1, 5 * 4, ptr);
			ptr[6] = gp0_uv(u0, v1, 0);
			gte_storeDataReg(GTE_SXY2, 7 * 4, ptr);
			ptr[8] = gp0_uv(u1, v1, 0);
		}

		ptr    = allocateOrderedGP0Packet(chain, GPU_ORDERING_TABLE_SIZE - 1, 3);
		ptr[0] = gp0_rgb(64, 64, 64) | gp0_vramFill();
		ptr[1] = gp0_xy(bufferX, bufferY);
		ptr[2] = gp0_xy(SCREEN_WIDTH, SCREEN_HEIGHT);

		ptr    = allocateOrderedGP0Packet(chain, GPU_ORDERING_TABLE_SIZE - 1, 4);
		ptr[0] = gp0_setPage(0, false, false);
		ptr[1] = gp0_fbOffset1(bufferX, bufferY);
		ptr[2] = gp0_fbOffset2(
			bufferX + SCREEN_WIDTH  - 1,
			bufferY + SCREEN_HEIGHT - 1
		);
		ptr[3] = gp0_fbOrigin(bufferX, bufferY);

		waitForGP0Ready();
		waitForVSync();
		sendGPULinkedList(&(chain->orderingTable)[GPU_ORDERING_TABLE_SIZE - 1]);
	}

	return 0;
}
