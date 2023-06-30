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

#include <stdio.h>
#include "ps1/registers.h"

/* Serial port stdin/stdout */

void initSerialIO(int baud) {
	SIO_CTRL(1) = SIO_CTRL_RESET;

	SIO_MODE(1) = SIO_MODE_BAUD_DIV16 | SIO_MODE_DATA_8 | SIO_MODE_STOP_1;
	SIO_BAUD(1) = (F_CPU / 16) / baud;
	SIO_CTRL(1) = SIO_CTRL_TX_ENABLE | SIO_CTRL_RX_ENABLE | SIO_CTRL_RTS;
}

void _putchar(char ch) {
	// The serial interface will buffer but not send any data if the CTS input
	// is not asserted, so we are going to abort if CTS is not set to avoid
	// waiting forever.
	while (
		(SIO_STAT(1) & (SIO_STAT_TX_NOT_FULL | SIO_STAT_CTS)) == SIO_STAT_CTS
	)
		__asm__ volatile("");

	if (SIO_STAT(1) & SIO_STAT_CTS)
		SIO_DATA(1) = ch;
}

int _getchar(void) {
	while (!(SIO_STAT(1) & SIO_STAT_RX_NOT_EMPTY))
		__asm__ volatile("");

	return SIO_DATA(1);
}

int _puts(const char *str) {
	int length = 1;

	for (; *str; str++, length++)
		_putchar(*str);

	_putchar('\n');
	return length;
}

/* Abort functions */

void _assertAbort(const char *file, int line, const char *expr) {
#ifndef NDEBUG
	printf("%s:%d: assert(%s)\n", file, line, expr);
#endif

	for (;;)
		__asm__ volatile("");
}

void abort(void) {
#ifndef NDEBUG
	puts("abort()");
#endif

	for (;;)
		__asm__ volatile("");
}

void __cxa_pure_virtual(void) {
#ifndef NDEBUG
	puts("__cxa_pure_virtual()");
#endif

	for (;;)
		__asm__ volatile("");
}
