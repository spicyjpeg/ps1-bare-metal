/*
 * ps1-bare-metal - Copyright (C) 2023-2026 spicyjpeg
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdint.h>
#include "common/gpu.h"

#define FONT_SPACE_WIDTH  4
#define FONT_TAB_WIDTH   64
#define FONT_LINE_HEIGHT 10

#ifdef __cplusplus
extern "C" {
#endif

void printString(
	GPUDMAChain       *chain,
	const TextureInfo *font,
	int               x,
	int               y,
	const char        *str
);
void printStringOrdered(
	GPUOrderedDMAChain *chain,
	const TextureInfo  *font,
	int                x,
	int                y,
	unsigned int       zIndex,
	const char         *str
);

int getStringWidth(const char *str);

#ifdef __cplusplus
}
#endif
