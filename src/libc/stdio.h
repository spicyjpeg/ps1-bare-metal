/*
 * ps1-bare-metal - (C) 2023 spicyjpeg
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES WITH
 * REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY
 * AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY SPECIAL, DIRECT,
 * INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM
 * LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR
 * OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 * PERFORMANCE OF THIS SOFTWARE.
 */

#pragma once

// Include printf() from the third-party library.
#include "vendor/printf.h"

#define putchar _putchar
#define getchar _getchar
#define puts    _puts

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initializes the serial port (SIO1) with the given baud rate, no
 * parity, 8 data bits and 1 stop bit. Must be called prior to using putchar(),
 * getchar(), puts() or printf().
 *
 * @param baud
 */
void initSerialIO(int baud);

void _putchar(char ch);
int _getchar(void);
int _puts(const char *str);

#ifdef __cplusplus
}
#endif
