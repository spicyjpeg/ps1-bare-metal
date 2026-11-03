/*
 * ps1-bare-metal - Copyright (C) 2023-2026 spicyjpeg
 * SPDX-License-Identifier: MIT
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
