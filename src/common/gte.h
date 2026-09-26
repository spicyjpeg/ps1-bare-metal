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

#include <assert.h>
#include "ps1/gte.h"

#define DEF(type) static inline type __attribute__((always_inline))

#define GTE_UNIT   (1 << 12)
#define GTE_SQRT2  (GTE_UNIT * 141421 / 100000)
#define GTE_RSQRT2 (GTE_UNIT *  70711 / 100000)
#define GTE_SQRT3  (GTE_UNIT * 173205 / 100000)
#define GTE_RSQRT3 (GTE_UNIT *  57735 / 100000)

#ifdef __cplusplus
extern "C" {
#endif

DEF(unsigned int) floorLog2(int x) {
	assert(x > 0);

	gte_setDataReg(GTE_LZCS, x);
	gte_loadDelay();
	return 31 - gte_getDataReg(GTE_LZCR);
}
DEF(unsigned int) ceilLog2(int x) {
	assert(x > 0);

	gte_setDataReg(GTE_LZCS, x - 1);
	gte_loadDelay();
	return 32 - gte_getDataReg(GTE_LZCR);
}

void setupGTE(unsigned int width, unsigned int height);

void multiplyRotationMatrixByVectors  (GTEMatrix *output);
void multiplyLightMatrixByVectors     (GTEMatrix *output);
void multiplyLightColorMatrixByVectors(GTEMatrix *output);

void rotateCurrentMatrixX(int angle);
void rotateCurrentMatrixY(int angle);
void rotateCurrentMatrixZ(int angle);

void transformLightMatrix(void);

#ifdef __cplusplus
}
#endif

#undef DEF
