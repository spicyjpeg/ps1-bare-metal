/*
 * ps1-bare-metal - (C) 2023-2026 spicyjpeg
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

/*
 * Let's start with the absolute basics, the obligatory hello world program. We
 * are going to just print "Hello world!" in an infinite loop; since we don't
 * have the luxury of a terminal or even anything resembling a "text mode" on
 * the GPU, we'll use the PS1's serial port instead.
 *
 * The serial port can be found on the back of the console on all models except
 * the PSone, and can be connected to a PC with an appropriately modified link
 * cable. Internally it is connected to the secondary serial interface, known as
 * SIO1 (as opposed to SIO0 which is wired to the controller and memory card
 * ports). SIO1 is controlled through I/O registers, which we're going to
 * manipulate to get it to output our message.
 */

#include "ps1/registers.h"

static void printCharacter(char ch) {
	// Wait until the serial interface is ready to send a new byte, then write
	// it to the data register.
	// NOTE: the serial interface checks for an external signal (CTS) and will
	// *not* send any data until it is asserted. To avoid blocking forever if
	// CTS is not asserted, we have to check for it manually and abort if
	// necessary.
	while ((SIO_SR(1) & (SIO_SR_TXRDY | SIO_SR_CTS)) == SIO_SR_CTS)
		__asm__ volatile("");

	if (SIO_SR(1) & SIO_SR_CTS)
		SIO_DR(1) = ch;
}

int main(int argc, const char **argv) {
	(void) argc;
	(void) argv;

	// Reset the serial interface and initialize it to output data at 115200bps,
	// 8 data bits, 1 stop bit and no parity.
	SIO_CR(1) = SIO_CR_INTRST;

	SIO_MR(1) = 0
		| SIO_MR_BR_DIV1
		| SIO_MR_CHLEN_8
		| SIO_MR_SB_1;
	SIO_BR(1) = F_CPU / 115200;
	SIO_CR(1) = 0
		| SIO_CR_TXEN
		| SIO_CR_RXEN
		| SIO_CR_RTS;

	// Output "Hello world!" in a loop, one character at a time.
	for (;;) {
		const char *str = "Hello world!\n";

		for (; *str; str++)
			printCharacter(*str);
	}

	// We're not actually going to return. Unless a loader was used to launch
	// the program, returning from main() would crash the console as there would
	// be nothing to return to.
	return 0;
}
