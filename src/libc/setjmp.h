/*
 * ps1-bare-metal - Copyright (C) 2023-2026 spicyjpeg
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdint.h>

typedef uintptr_t jmp_buf[12];

#ifdef __cplusplus
extern "C" {
#endif

int  setjmp (jmp_buf env);
void longjmp(jmp_buf env, int status);

#ifdef __cplusplus
}
#endif
