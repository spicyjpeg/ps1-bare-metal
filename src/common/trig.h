/*
 * ps1-bare-metal - Copyright (C) 2023-2026 spicyjpeg
 * SPDX-License-Identifier: MIT
 */

#pragma once

#define DEF(type) static inline type __attribute__((always_inline))

#define ISIN_SHIFT  10
#define ISIN2_SHIFT 15
#define ISIN_PI     (1 << (ISIN_SHIFT  + 1))
#define ISIN2_PI    (1 << (ISIN2_SHIFT + 1))

#ifdef __cplusplus
extern "C" {
#endif

int isin(int x);
int isin2(int x);

DEF(int) icos(int x) {
	return isin(x + (1 << ISIN_SHIFT));
}
DEF(int) icos2(int x) {
	return isin2(x + (1 << ISIN2_SHIFT));
}

#ifdef __cplusplus
}
#endif

#undef DEF
