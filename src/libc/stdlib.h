/*
 * ps1-bare-metal - Copyright (C) 2023-2026 spicyjpeg
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

[[gnu::always_inline]] static inline int abs(int value) {
	return (value < 0) ? (-value) : value;
}
[[gnu::always_inline]] static inline long labs(long value) {
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
[[noreturn]] void abort(void);

// string.c
long long strtoll(
	const char *__restrict str,
	char **__restrict      strEnd,
	int                    base
);

[[gnu::always_inline]] static inline long strtol(
	const char *__restrict str,
	char **__restrict      strEnd,
	int                    base
) {
	return (long) strtoll(str, strEnd, base);
}

#ifdef __cplusplus
}
#endif
