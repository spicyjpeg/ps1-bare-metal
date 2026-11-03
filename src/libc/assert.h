/*
 * ps1-bare-metal - Copyright (C) 2023-2026 spicyjpeg
 * SPDX-License-Identifier: MIT
 */

#pragma once

// NDEBUG is automatically defined by CMake when the executable is built in
// release mode.
#ifdef NDEBUG
#define assert(expr) ((void) (expr))
#else
#define assert(expr) \
	((expr) ? ((void) 0) : _assertAbort(__FILE__, __LINE__, #expr))
#endif

#ifdef __cplusplus
extern "C" {
#endif

void _assertAbort(const char *file, int line, const char *expr);

#ifdef __cplusplus
}
#endif
