/*
 * ps1-bare-metal - Copyright (C) 2023-2026 spicyjpeg
 * SPDX-License-Identifier: MIT
 */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

// The interrupt handler is declared here but must be provided by your code.
// When using C++, this header must be included in the file that defines it in
// order to prevent issues with name mangling.
void interruptHandler(void);

void initKernelISR(void);
void restoreKernelISR(void);

void installCustomISR(void);
void uninstallCustomISR(void);

#ifdef __cplusplus
}
#endif
