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
 * Having explored the capabilities of the PS1's GPU in previous examples, it is
 * now time to focus on the other piece of hardware that makes 3D graphics on
 * the PS1 possible: the geometry transformation engine (GTE), a specialized
 * coprocessor whose job is to perform various geometry-related calculations
 * much faster than the CPU could on its own.
 *
 * In order to draw a 3D scene, we can use the GTE to process each of our
 * models' vertices, generating projected 2D screen-space coordinates that we
 * can then wrap into GP0 polygon commands for drawing. We may also take
 * advantage of the GTE's fast 3x3 matrix multiplier to set up a 3-axis rotation
 * matrix to run our vertices through. In this example we're going to do so with
 * a simple cube model consisting of 6 quads, translating it away from the
 * camera and animating its rotation.
 *
 * If you are new to linear algebra and 3D graphics, this example may be harder
 * to follow than the previous ones. You may want to familiarize with the basics
 * first by following a guide such as this one:
 *     https://wolfire.com/blog/2009/07/linear-algebra-for-game-developers-part-1
 *
 * NOTE: unlike all other PS1 peripherals, the GTE is not memory-mapped and must
 * instead be accessed via dedicated CPU instructions that require the use of
 * inline assembly. This example uses the ps1/cop0.h and ps1/gte.h headers,
 * which wrap the raw assembly and provide a C API for accessing registers and
 * executing GTE commands.
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include "common/gpu.h"
#include "common/trig.h"
#include "ps1/cop0.h"
#include "ps1/gpucmd.h"
#include "ps1/gte.h"
#include "ps1/registers.h"

// The GTE uses a 20.12 fixed-point format for most values. What this means is
// that fractional values will be stored as integers by multiplying them by a
// fixed unit, in this case 4096 or 2^12 (hence making the fractional part 12
// bits long). We'll define this unit value to make their handling easier.
#define GTE_UNIT (1 << 12)

static void setupGTE(unsigned int width, unsigned int height) {
	// Ensure the GTE, which is coprocessor 2, is enabled. MIPS coprocessors are
	// enabled through the status register in coprocessor 0, which is always
	// accessible.
	cop0_setReg(COP0_STATUS, cop0_getReg(COP0_STATUS) | COP0_STATUS_CU2);

	// Set the offset to be added to all calculated screen space coordinates (we
	// want our cube to appear at the center of the screen) Note that OFX and
	// OFY are 16.16 fixed-point rather than 20.12.
	gte_setControlReg(GTE_OFX, (width  << 16) / 2);
	gte_setControlReg(GTE_OFY, (height << 16) / 2);

	// Set the distance of the perspective projection plane (i.e. the camera's
	// focal length), which affects the field of view.
	int focalLength = (width < height) ? width : height;

	gte_setControlReg(GTE_H, focalLength / 2);

	// Set the scaling factor for Z averaging. For each polygon drawn, the GTE
	// will sum the transformed Z coordinates of its vertices multiplied by this
	// value in order to derive the ordering table bucket index the polygon will
	// be sorted into. This will work best if the ordering table length is a
	// multiple of 12 (i.e. both 3 and 4) or high enough to make any rounding
	// error negligible.
	gte_setControlReg(GTE_ZSF3, GPU_ORDERING_TABLE_SIZE / 3);
	gte_setControlReg(GTE_ZSF4, GPU_ORDERING_TABLE_SIZE / 4);
}

// When transforming vertices, the GTE will multiply their vectors by a 3x3
// matrix stored in its registers. This matrix can be used, among other things,
// to rotate the model by multiplying it by the appropriate rotation matrices.
// The functions below handle manipulation of this matrix.
static void multiplyRotationMatrixByVectors(GTEMatrix *output) {
	// Multiply the GTE's current matrix by the matrix whose column vectors are
	// V0/V1/V2, then store the result to the provided location. This has to be
	// done one column at a time, as the GTE only supports multiplying a matrix
	// by a vector using the MVMVA command.
	gte_command(GTE_CMD_MVMVA | GTE_SF | GTE_MX_RT | GTE_V_V0 | GTE_CV_NONE);
	output->values[0][0] = (int16_t) gte_getDataReg(GTE_IR1);
	output->values[1][0] = (int16_t) gte_getDataReg(GTE_IR2);
	output->values[2][0] = (int16_t) gte_getDataReg(GTE_IR3);

	gte_command(GTE_CMD_MVMVA | GTE_SF | GTE_MX_RT | GTE_V_V1 | GTE_CV_NONE);
	output->values[0][1] = (int16_t) gte_getDataReg(GTE_IR1);
	output->values[1][1] = (int16_t) gte_getDataReg(GTE_IR2);
	output->values[2][1] = (int16_t) gte_getDataReg(GTE_IR3);

	gte_command(GTE_CMD_MVMVA | GTE_SF | GTE_MX_RT | GTE_V_V2 | GTE_CV_NONE);
	output->values[0][2] = (int16_t) gte_getDataReg(GTE_IR1);
	output->values[1][2] = (int16_t) gte_getDataReg(GTE_IR2);
	output->values[2][2] = (int16_t) gte_getDataReg(GTE_IR3);
}

static void rotateCurrentMatrixX(int angle) {
	// Compute an X- or Y-axis rotation matrix (Z rotation is not used in this
	// example), then "combine" it with the current one by multiplying the two
	// and loading the result back into the GTE. isin() and icos() are simple
	// 20.12 -> 1.12 fixed-point sine and cosine implementations defined in
	// common/trig.c.
	int s = isin(angle);
	int c = icos(angle);

	gte_setColumnVectors(
		GTE_UNIT, 0,  0,
		       0, c, -s,
		       0, s,  c
	);

	GTEMatrix result;

	multiplyRotationMatrixByVectors(&result);
	gte_loadRotationMatrix(&result);
}

static void rotateCurrentMatrixY(int angle) {
	int s = isin(angle);
	int c = icos(angle);

	gte_setColumnVectors(
		 c,        0, s,
		 0, GTE_UNIT, 0,
		-s,        0, c
	);

	GTEMatrix result;

	multiplyRotationMatrixByVectors(&result);
	gte_loadRotationMatrix(&result);
}

// We're going to store the 3D model of our cube as two separate arrays, one
// containing a list of unique vertices and the other referencing those vertices
// to build up quadrilateral faces. This approach of having a "palette" of
// vertices, in a similar way to how indexed color works, allows for significant
// memory savings as most if not all faces usually have vertices in common.
typedef struct {
	uint8_t  vertices[4];
	uint32_t color;
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

// Note that there are several requirements on the order of vertices:
// - they must be arranged in a Z-like shape rather than clockwise or
//   counterclockwise, since the GPU processes a quad with vertices (A, B, C, D)
//   as two triangles with vertices (A, B, C) and (B, C, D) respectively;
// - the first 3 vertices must be ordered clockwise when the face is viewed from
//   the front, as the code relies on this to determine whether or not the quad
//   is facing the camera (see main()).
// For instance, only the first of these faces (viewed from the front) has its
// vertices ordered correctly:
//     0----1        0----1        2----3
//     |  / |        | \/ |        | \  |
//     | /  |        | /\ |        |  \ |
//     2----3        3----2        0----1
//     Correct    Not Z-shaped  Not clockwise
static const Face cubeFaces[] = {
	{ .vertices = { 0, 1, 2, 3 }, .color = 0x0000ff },
	{ .vertices = { 6, 7, 4, 5 }, .color = 0x00ff00 },
	{ .vertices = { 4, 5, 0, 1 }, .color = 0x00ffff },
	{ .vertices = { 7, 6, 3, 2 }, .color = 0xff0000 },
	{ .vertices = { 6, 4, 2, 0 }, .color = 0xff00ff },
	{ .vertices = { 5, 7, 1, 3 }, .color = 0xffff00 }
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

	// Set up the GTE's translation vector (added to each vertex) to move the
	// cube away from the camera.
	gte_setControlReg(GTE_TRX,   0);
	gte_setControlReg(GTE_TRY,   0);
	gte_setControlReg(GTE_TRZ, 128);

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

		// Reset and recompute the rotation matrix to animate the cube.
		gte_setRotationMatrix(
			GTE_UNIT,        0,        0,
			       0, GTE_UNIT,        0,
			       0,        0, GTE_UNIT
		);
		rotateCurrentMatrixX(frameCounter * 12);
		rotateCurrentMatrixY(frameCounter * 16);

		// Draw the cube one face at a time.
		for (int i = 0; i < (int) NUM_CUBE_FACES; i++) {
			const Face *face = &cubeFaces[i];

			// Apply perspective projection to the first 3 vertices. The GTE can
			// only process up to 3 vertices at a time; since we are using quads
			// rather than triangles, we'll have to transform the last one
			// separately. The SF flag sets the data format to 4.12 fixed-point.
			gte_loadV0(&cubeVertices[face->vertices[0]]);
			gte_loadV1(&cubeVertices[face->vertices[1]]);
			gte_loadV2(&cubeVertices[face->vertices[2]]);
			gte_command(GTE_CMD_RTPT | GTE_SF);

			// Determine the winding order of the projected vertices (and area
			// of their respective triangle) using the shoelace formula. If they
			// are ordered clockwise then the face is visible, otherwise it can
			// be culled as it is not facing the camera. Note that
			// gte_getDataReg() always returns a 32-bit unsigned value, but some
			// GTE registers should be interpreted as signed.
			gte_command(GTE_CMD_NCLIP);
			int area = (int) gte_getDataReg(GTE_MAC0);

			if (area <= 0)
				continue;

			// Save the first vertex before projecting the last one, which will
			// evict the first from the GTE's 3-entry queue of projected X/Y
			// values.
			uint32_t xy0 = gte_getDataReg(GTE_SXY0);

			gte_loadV0(&cubeVertices[face->vertices[3]]);
			gte_command(GTE_CMD_RTPS | GTE_SF);

			// Compute the average Z coordinate of all four vertices and use it
			// to determine the ordering table bucket index for this face. No
			// saving or restoring is needed here since the GTE is equipped with
			// a 4-entry Z queue.
			gte_command(GTE_CMD_AVSZ4 | GTE_SF);
			int zIndex = (int) gte_getDataReg(GTE_OTZ);

			if ((zIndex < 0) || (zIndex >= GPU_ORDERING_TABLE_SIZE))
				continue;

			// Allocate a quad command and fill it in with X/Y coordinates
			// directly from the queue.
			ptr    = allocateOrderedGP0Packet(chain, zIndex, 5);
			ptr[0] = face->color | gp0_shadedQuad(false, false, false);
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
