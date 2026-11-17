/*
 * ps1-bare-metal - Copyright (C) 2023-2026 spicyjpeg
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "ps1/registers.h"

#define DIV(num, den) (((num) + (den) / 2) / (den))

[[gnu::always_inline]] static inline void delayCycles(int time) {
	__asm__ volatile(
		".set push\n"
		".set noreorder\n"
		"bgtz  %0, .\n"
		"addiu %0, -2\n"
		".set pop\n"
		: "+r"(time)
	);
}

[[gnu::always_inline]] static inline void delayMicroseconds(int time) {
	delayCycles(DIV(time * DIV(F_CPU * 8, 1000000), 8));
}

#undef DIV
