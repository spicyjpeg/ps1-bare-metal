/*
 * ps1-bare-metal - Copyright (C) 2023-2026 spicyjpeg
 * SPDX-License-Identifier: MIT
 */

/*
 * Let's start with the absolute basics, the obligatory hello world program.
 * Since we don't have the luxury of anything resembling a "text mode" on the
 * GPU, we'll instead output to two much simpler devices: the PS1's serial port
 * and the virtual TTY (log window) provided by PCSX-Redux and DuckStation.
 *
 * The serial port can be found on the back of the console on all models prior
 * to the PSone and connected to a PC with an appropriately modified link cable.
 * It is wired to the CPU's secondary serial interface, known as SIO1 (as
 * opposed to SIO0 which drives the controller and memory card ports). SIO1 and
 * PCSX-Redux's emulated TTY are both exposed through memory-mapped I/O ports,
 * so we'll use the register macros defined in the included ps1/registers.h and
 * ps1/registersdev.h headers to access them.
 */

#include <stdint.h>
#include "ps1/registers.h"
#include "ps1/registersdev.h"

static void initSIO1(unsigned int baud) {
	// Perform a full SIO1 reset.
	SIO_CR(1) = SIO_CR_INTRST;

	// Configure SIO1 for standard serial output (8 data bits, 1 stop bit, no
	// parity), set the baud rate divider based on the CPU clock and ensure the
	// RTS flow control signal is asserted to allow the remote end to send us
	// data. We won't receive anything in this example, but in some cases (e.g.
	// on certain PS1-based arcade systems) RTS is looped back to CTS, which
	// will block sending if not asserted.
	SIO_MR(1) = 0
		| SIO_MR_BR_DIV1
		| SIO_MR_CHLEN_8
		| SIO_MR_SB_1;
	SIO_BR(1) = F_CPU / baud;
	SIO_CR(1) = 0
		| SIO_CR_TXEN
		| SIO_CR_RXEN
		| SIO_CR_RTS;
}

static void initPCSX(void) {
	// The PCSX_* registers are part of a memory region which is unmapped by
	// default. Emulators always map it for convenience's sake, but accessing it
	// on a real console would trigger a CPU exception and crash our code. To
	// prevent this we can extend the nearby "DEV8" I/O region from 7 address
	// bits to 8 (256 bytes), making it span the PCSX_* registers as well. This
	// is done by manipulating bus interface unit (BIU) registers.
	BIU_DEV8_ADDR = DEV8_BASE & 0x1fffffff;

	uint32_t config = BIU_DEV8_DELAY & ~BIU_DEV_DELAY_ADDR_BITS_BITMASK;
	BIU_DEV8_DELAY  = config | BIU_DEV_DELAY_ADDR_BITS(8);
}

static void printCharacter(char ch) {
	// Wait for SIO1 to be ready to send a new byte, then write it to the data
	// register. As mentioned above, SIO1 has TX flow control that cannot be
	// disabled; to avoid getting stuck waiting for it to send data while CTS is
	// not asserted, we'll check CTS manually and bail out as needed.
	while ((SIO_SR(1) & (SIO_SR_TXRDY | SIO_SR_CTS)) == SIO_SR_CTS)
		__asm__ volatile("");

	if (SIO_SR(1) & SIO_SR_CTS)
		SIO_DR(1) = (uint8_t) ch;

	// Send the character to the PCSX TTY as well (if present). This is much
	// faster than a physical serial port, so there is no flow control or status
	// register to check here.
	PCSX_PUTC = (uint8_t) ch;
}

int main(int argc, const char **argv) {
	(void) argc;
	(void) argv;

	// Set up SIO1 for the standard baud rate of 115200 bits per second and
	// output "Hello world!" in a loop, one character at a time.
	initSIO1(115200);
	initPCSX();

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
