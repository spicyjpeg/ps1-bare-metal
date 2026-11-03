/*
 * ps1-bare-metal - Copyright (C) 2023-2026 spicyjpeg
 * SPDX-License-Identifier: MIT
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include "ps1/kernel.h"
#include "ps1/registers.h"
#include "ps1/registersdev.h"

/* TTY setup */

typedef enum {
	TTY_KERNEL  = 1 << 0,
	TTY_SIO1    = 1 << 1,
	TTY_PCSX    = 1 << 2,
	TTY_ATCONS  = 1 << 3,
	TTY_DECKARD = 1 << 4
} TTYFlag;

static uint8_t ttyFlags = 0;

void initKernelTTY(void) {
	// Attempt to make stdin reads non-blocking to allow getchar() to wait for
	// incoming bytes from stdin, SIO1 and ATCONS simultaneously. Some kernel
	// TTY drivers such as Unirom's do not support this option and will block
	// regardless, but there is nothing else we can do about it.
	kernel_ioctl(KERNEL_FD_STDIN, KERNEL_IOCTL_NBLOCK, true);

	ttyFlags |= TTY_KERNEL;
}

void initSIO1TTY(unsigned int baud) {
	SIO_CR(1) = SIO_CR_INTRST;

	SIO_MR(1) = 0
		| SIO_MR_BR_DIV1
		| SIO_MR_CHLEN_8
		| SIO_MR_SB_1;
	SIO_BR(1) = F_CPU / baud;
	SIO_CR(1) = 0
		| SIO_CR_TXEN
		| SIO_CR_RXEN
		| SIO_CR_RTS;

	ttyFlags |= TTY_SIO1;
}

void initPCSXTTY(void) {
	BIU_DEV8_ADDR = DEV8_BASE & 0x1fffffff;

	uint32_t config = BIU_DEV8_DELAY & ~BIU_DEV_DELAY_ADDR_BITS_BITMASK;
	BIU_DEV8_DELAY  = config | BIU_DEV_DELAY_ADDR_BITS(8);

	ttyFlags |= TTY_PCSX;
}

void initATCONSTTY(void) {
	// The window size is set to 256 bytes here in order to properly handle
	// initATCONSTTY() being called after initPCSXTTY().
	BIU_DEV8_ADDR  = DEV8_BASE & 0x1fffffff;
	BIU_DEV8_DELAY = 0
		| BIU_DEV_DELAY_WRITE_CYCLES(2)
		| BIU_DEV_DELAY_READ_CYCLES(2)
		| BIU_DEV_DELAY_WIDTH_16
		| BIU_DEV_DELAY_ADDR_BITS(8);

	ttyFlags |= TTY_ATCONS;
}

void initDeckardTTY(void) {
	ttyFlags |= TTY_DECKARD;
}

/* TTY functions */

void putchar(char ch) {
	uint8_t byte = (uint8_t) ch;

	if (ttyFlags & TTY_KERNEL)
		kernel_write(KERNEL_FD_STDOUT, &byte, sizeof(byte));

	if (ttyFlags & TTY_SIO1) {
		while ((SIO_SR(1) & (SIO_SR_TXRDY | SIO_SR_CTS)) == SIO_SR_CTS)
			__asm__ volatile("");

		if (SIO_SR(1) & SIO_SR_CTS)
			SIO_DR(1) = byte;
	}

	if (ttyFlags & TTY_PCSX)
		PCSX_PUTC = byte;

	if (ttyFlags & TTY_ATCONS) {
		while (!(DTLH_STAT & DTLH_STAT_TTY_TX_NOT_FULL))
			__asm__ volatile("");

		DTLH_TTY       = byte;
		DTLH_IRQ_MASK |= 1 << DTLH_IRQ_TTY_HOST;
	}

	if (ttyFlags & TTY_DECKARD)
		// This register technically only exists on PS2 development kits, but
		// the "Deckard" IOP emulator used in later PS2 models sniffs writes to
		// it and redirects them to its own debug serial port (broken out to a
		// test pad on the motherboard).
		DTLT_PIF_CPS = byte;
}

int getchar(void) {
	if (!ttyFlags)
		return -1;

	for (;;) {
		if (ttyFlags & TTY_KERNEL) {
			uint8_t byte;

			if (kernel_read(KERNEL_FD_STDIN, &byte, sizeof(byte)))
				return byte;
		}

		if (ttyFlags & TTY_SIO1) {
			if (SIO_SR(1) & SIO_SR_RXRDY)
				return SIO_DR(1);
		}

		if (ttyFlags & TTY_ATCONS) {
			if (DTLH_STAT & DTLH_STAT_TTY_RX_NOT_EMPTY) {
				uint8_t byte   = DTLH_TTY;
				DTLH_IRQ_STAT  = 1 << DTLH_IRQ_TTY_RX;
				DTLH_IRQ_MASK |= 1 << DTLH_IRQ_TTY_HOST;

				return byte;
			}
		}
	}
}

int puts(const char *str) {
	int length = 1;

	for (; *str; str++, length++)
		putchar(*str);

	putchar('\n');
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
