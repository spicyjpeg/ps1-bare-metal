/*
 * ps1-bare-metal - Copyright (C) 2023-2026 spicyjpeg
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stddef.h>
#include <stdint.h>

#define DEF(type) static inline type __attribute__((always_inline))

#ifdef __cplusplus
extern "C" {
#endif

DEF(int) abs(int value) {
	return (value < 0) ? (-value) : value;
}
DEF(long) labs(long value) {
	return (value < 0) ? (-value) : value;
}

// crt0.c
void *sbrk(ptrdiff_t incr);

// malloc.c
void *malloc(size_t size);
void *calloc(size_t num, size_t size);
void *realloc(void *ptr, size_t size);
void free(void *ptr);

// misc.c
void abort(void);

// string.c
long long strtoll(
	const char *__restrict str,
	char **__restrict      strEnd,
	int                    base
);

DEF(long) strtol(
	const char *__restrict str,
	char **__restrict      strEnd,
	int                    base
) {
	return (long) strtoll(str, strEnd, base);
}

#ifdef __cplusplus
}
#endif

#undef DEF
