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
 * We have introduced the GTE and used its perspective projection commands to
 * render a 3D model. Let's now focus on another of its main facilities,
 * lighting calculations: it can project up to three directional light sources
 * at a time onto faces or vertices, updating their colors accordingly. As the
 * name implies a directional light has a direction but no origin, representing
 * instead light coming from an infinite distance from the target. In this
 * example we are going to point two such lights at our cube.
 *
 * In order for the GTE to determine which lights are shining onto which faces
 * we need to provide it with the respective normals. A polygon's normal is a
 * unit-length vector perpendicular to it and pointing outwards; by computing
 * the angle between the normal and each of the lights' directions, the GTE can
 * estimate the amount of light received by the face and modulate the sources'
 * effect on the final color. If the polygon is facing no lights, a baseline
 * "ambient" light can be added to prevent it from turning completely black.
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include "common/gpu.h"
#include "common/gte.h"
#include "ps1/gpucmd.h"
#include "ps1/gte.h"
#include "ps1/registers.h"

static void transformLightMatrix_(void) {
	GTEMatrix result;

	// Update the light matrix currently loaded into the GTE (see main()) by
	// multiplying it by the rotation matrix. This compensates for the model's
	// rotation and ensures light directions remain stationary.
	gte_storeRotationMatrix(&result);
	gte_setColumnVectors(
		result.values[0][0], result.values[0][1], result.values[0][2],
		result.values[1][0], result.values[1][1], result.values[1][2],
		result.values[2][0], result.values[2][1], result.values[2][2]
	);

	multiplyLightMatrixByVectors(&result);
	gte_loadLightMatrix(&result);
}

// We'll extend our cube model with a set of normal vectors and a per-face
// normal index. This layer of indirection is somewhat pointless here since all
// faces have unique normals, but would save memory in a more complex model with
// many faces sharing the same normals.
typedef struct {
	uint8_t  vertices[4];
	uint32_t color  : 24;
	uint8_t  normal :  8;
} Face;

static const GTEVector16 cubeVertices[] = {
	{ .x = -32, .y = -32, .z = -32 },
	{ .x =  32, .y = -32, .z = -32 },
	{ .x = -32, .y =  32, .z = -32 },
	{ .x =  32, .y =  32, .z = -32 },
	{ .x = -32, .y = -32, .z =  32 },
	{ .x =  32, .y = -32, .z =  32 },
	{ .x = -32, .y =  32, .z =  32 },
	{ .x =  32, .y =  32, .z =  32 }
};

static const GTEVector16 cubeNormals[] = {
	{ .x =         0, .y =         0, .z = -GTE_UNIT },
	{ .x =         0, .y =         0, .z =  GTE_UNIT },
	{ .x =         0, .y = -GTE_UNIT, .z =         0 },
	{ .x =         0, .y =  GTE_UNIT, .z =         0 },
	{ .x = -GTE_UNIT, .y =         0, .z =         0 },
	{ .x =  GTE_UNIT, .y =         0, .z =         0 }
};

static const Face cubeFaces[] = {
	{ .vertices = { 0, 1, 2, 3 }, .color = 0x000080, .normal = 0 },
	{ .vertices = { 6, 7, 4, 5 }, .color = 0x008000, .normal = 1 },
	{ .vertices = { 4, 5, 0, 1 }, .color = 0x008080, .normal = 2 },
	{ .vertices = { 7, 6, 3, 2 }, .color = 0x800000, .normal = 3 },
	{ .vertices = { 6, 4, 2, 0 }, .color = 0x800080, .normal = 4 },
	{ .vertices = { 5, 7, 1, 3 }, .color = 0x808000, .normal = 5 }
};

#define NUM_CUBE_FACES (sizeof(cubeFaces) / sizeof(Face))

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
	setupGTE(SCREEN_WIDTH, SCREEN_HEIGHT);

	gte_setControlReg(GTE_TRX,   0);
	gte_setControlReg(GTE_TRY,   0);
	gte_setControlReg(GTE_TRZ, 128);

	// Initialize the light color matrix, each column of which represents the
	// emission color of the respective light source. We'll use two sources, a
	// bright white one and a dimmer one.
	gte_setLightColorMatrix(
		GTE_UNIT * 2, GTE_UNIT / 2, 0,
		GTE_UNIT * 2, GTE_UNIT / 2, 0,
		GTE_UNIT * 2, GTE_UNIT / 2, 0
	);

	// Set the ambient light color. Note that this color is added to all faces,
	// not just unlit ones.
	gte_setControlReg(GTE_RBK, GTE_UNIT / 2);
	gte_setControlReg(GTE_GBK, GTE_UNIT / 2);
	gte_setControlReg(GTE_BBK, GTE_UNIT / 2);

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

		gte_setRotationMatrix(
			GTE_UNIT,        0,        0,
			       0, GTE_UNIT,        0,
			       0,        0, GTE_UNIT
		);
		rotateCurrentMatrixX(frameCounter * 12);
		rotateCurrentMatrixY(frameCounter * 16);

		// Set the light matrix, each row of which is a unit vector pointing
		// *to* the light source (i.e. with the opposite direction of the light
		// itself), and adjust it for the cube's rotation. The first source will
		// emit from the top, while the second will light the cube from the
		// front-bottom-right corner.
		gte_setLightMatrix(
			         0,  -GTE_UNIT,           0,
			GTE_RSQRT3, GTE_RSQRT3, -GTE_RSQRT3,
			         0,          0,           0
		);
		transformLightMatrix_();

		for (int i = 0; i < (int) NUM_CUBE_FACES; i++) {
			const Face *face = &cubeFaces[i];

			gte_loadV0(&cubeVertices[face->vertices[0]]);
			gte_loadV1(&cubeVertices[face->vertices[1]]);
			gte_loadV2(&cubeVertices[face->vertices[2]]);
			gte_command(GTE_CMD_RTPT | GTE_SF);

			gte_command(GTE_CMD_NCLIP);
			int area = (int) gte_getDataReg(GTE_MAC0);

			if (area <= 0)
				continue;

			uint32_t xy0 = gte_getDataReg(GTE_SXY0);

			gte_loadV0(&cubeVertices[face->vertices[3]]);
			gte_command(GTE_CMD_RTPS | GTE_SF);

			gte_command(GTE_CMD_AVSZ4 | GTE_SF);
			int zIndex = (int) gte_getDataReg(GTE_OTZ);

			if ((zIndex < 0) || (zIndex >= GPU_ORDERING_TABLE_SIZE))
				continue;

			// Feed the first GP0 command word (containing the fully lit face
			// color) into the GTE alongside the face's normal, allowing it to
			// apply the light sources and patch the resulting color back into
			// the command. The LM flag saturates negative lights to zero,
			// preventing the GTE from darkening the face below the ambient
			// color.
			uint32_t cmd0 = face->color | gp0_shadedQuad(false, false, false);

			gte_setDataReg(GTE_RGBC, cmd0);
			gte_loadV0(&cubeNormals[face->normal]);
			gte_command(GTE_CMD_NCCS | GTE_SF | GTE_LM);

			// The modified command is pushed to the tail of a 3-entry queue.
			// Copy the most recent entry over to the packet.
			ptr    = allocateOrderedGP0Packet(chain, zIndex, 5);
			gte_storeDataReg(GTE_RGB2, 0 * 4, ptr);
			ptr[1] = xy0;
			gte_storeDataReg(GTE_SXY0, 2 * 4, ptr);
			gte_storeDataReg(GTE_SXY1, 3 * 4, ptr);
			gte_storeDataReg(GTE_SXY2, 4 * 4, ptr);
		}

		ptr    = allocateOrderedGP0Packet(chain, GPU_ORDERING_TABLE_SIZE - 1, 3);
		ptr[0] = gp0_rgb(64, 64, 64) | gp0_vramFill();
		ptr[1] = gp0_xy(bufferX, bufferY);
		ptr[2] = gp0_xy(SCREEN_WIDTH, SCREEN_HEIGHT);

		ptr    = allocateOrderedGP0Packet(chain, GPU_ORDERING_TABLE_SIZE - 1, 4);
		ptr[0] = gp0_setPage(0, true, false);
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
