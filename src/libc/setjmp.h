/*
 * ps1-bare-metal - Copyright (C) 2023-2026 spicyjpeg
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdint.h>

typedef struct {
	uintptr_t ra, sp, fp;
	uintptr_t s0, s1, s2, s3, s4, s5, s6, s7;
	uintptr_t gp;
} JmpBufInner;

// The C standards require jmp_buf to be an array type and setjmp() a macro.
typedef JmpBufInner jmp_buf[1];

#define setjmp(env) setjmp_(env)

#ifdef __cplusplus
extern "C" {
#endif

int  setjmp_(jmp_buf env);
void longjmp(const jmp_buf env, int status);

#ifdef __cplusplus
}
#endif
