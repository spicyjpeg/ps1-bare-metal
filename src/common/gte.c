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

#include <stdint.h>
#include "common/gpu.h"
#include "common/gte.h"
#include "common/trig.h"
#include "ps1/cop0.h"
#include "ps1/gte.h"

void setupGTE(unsigned int width, unsigned int height) {
	cop0_setReg(COP0_STATUS, cop0_getReg(COP0_STATUS) | COP0_STATUS_CU2);

	gte_setControlReg(GTE_OFX, (width  << 16) / 2);
	gte_setControlReg(GTE_OFY, (height << 16) / 2);

	int focalLength = (width < height) ? width : height;

	gte_setControlReg(GTE_H, focalLength / 2);

	gte_setControlReg(GTE_ZSF3, GPU_ORDERING_TABLE_SIZE / 3);
	gte_setControlReg(GTE_ZSF4, GPU_ORDERING_TABLE_SIZE / 4);
}

#define MULTIPLY_MATRIX(flag, name) \
	void name(GTEMatrix *output) { \
		gte_command(GTE_CMD_MVMVA | GTE_SF | flag | GTE_V_V0 | GTE_CV_NONE); \
		output->values[0][0] = (int16_t) gte_getDataReg(GTE_IR1); \
		output->values[1][0] = (int16_t) gte_getDataReg(GTE_IR2); \
		output->values[2][0] = (int16_t) gte_getDataReg(GTE_IR3); \
		\
		gte_command(GTE_CMD_MVMVA | GTE_SF | flag | GTE_V_V1 | GTE_CV_NONE); \
		output->values[0][1] = (int16_t) gte_getDataReg(GTE_IR1); \
		output->values[1][1] = (int16_t) gte_getDataReg(GTE_IR2); \
		output->values[2][1] = (int16_t) gte_getDataReg(GTE_IR3); \
		\
		gte_command(GTE_CMD_MVMVA | GTE_SF | flag | GTE_V_V2 | GTE_CV_NONE); \
		output->values[0][2] = (int16_t) gte_getDataReg(GTE_IR1); \
		output->values[1][2] = (int16_t) gte_getDataReg(GTE_IR2); \
		output->values[2][2] = (int16_t) gte_getDataReg(GTE_IR3); \
	}

MULTIPLY_MATRIX(GTE_MX_RT,  multiplyRotationMatrixByVectors)
MULTIPLY_MATRIX(GTE_MX_LLM, multiplyLightMatrixByVectors)
MULTIPLY_MATRIX(GTE_MX_LCM, multiplyLightColorMatrixByVectors)

#undef MULTIPLY_MATRIX

#define ROTATE_MATRIX(v11, v12, v13, v21, v22, v23, v31, v32, v33, name) \
	void name(int angle) { \
		int s = isin(angle); \
		int c = icos(angle); \
		\
		gte_setColumnVectors( \
			v11, v12, v13, \
			v21, v22, v23, \
			v31, v32, v33 \
		); \
		\
		GTEMatrix result; \
		\
		multiplyRotationMatrixByVectors(&result); \
		gte_loadRotationMatrix(&result); \
	}

ROTATE_MATRIX(
	GTE_UNIT, 0,  0,
	       0, c, -s,
	       0, s,  c,
	rotateCurrentMatrixX
)
ROTATE_MATRIX(
	 c,        0, s,
	 0, GTE_UNIT, 0,
	-s,        0, c,
	rotateCurrentMatrixY
)
ROTATE_MATRIX(
	c, -s,        0,
	s,  c,        0,
	0,  0, GTE_UNIT,
	rotateCurrentMatrixZ
)

#undef ROTATE_MATRIX

void transformLightMatrix(void) {
	GTEMatrix result;

	gte_storeRotationMatrix(&result);
	gte_setColumnVectors(
		result.values[0][0], result.values[0][1], result.values[0][2],
		result.values[1][0], result.values[1][1], result.values[1][2],
		result.values[2][0], result.values[2][1], result.values[2][2]
	);

	multiplyLightMatrixByVectors(&result);
	gte_loadLightMatrix(&result);
}
