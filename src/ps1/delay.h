/*
 * ps1-bare-metal - Copyright (C) 2023-2026 spicyjpeg
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "ps1/registers.h"

#define DEF(type)     static inline type __attribute__((always_inline))
#define DIV(num, den) (((num) + (den) / 2) / (den))

DEF(void) delayCycles(int time) {
	__asm__ volatile(
		".set push\n"
		".set noreorder\n"
		"bgtz  %0, .\n"
		"addiu %0, -2\n"
		".set pop\n"
		: "+r"(time)
	);
}

DEF(void) delayMicroseconds(int time) {
	delayCycles(DIV(time * DIV(F_CPU * 8, 1000000), 8));
}

#undef DEF
#undef DIV
