/*
 * ps1-bare-metal - Copyright (C) 2023-2026 spicyjpeg
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "vendor/printf.h"

#ifdef __cplusplus
extern "C" {
#endif

void initKernelTTY(void);
void initSIO1TTY(unsigned int baud);
void initPCSXTTY(void);
void initATCONSTTY(void);
void initDeckardTTY(void);

void putchar(char ch);
int getchar(void);
int puts(const char *str);

#ifdef __cplusplus
}
#endif
