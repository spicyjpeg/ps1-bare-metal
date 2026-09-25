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
 * We have explored 2D and 3D graphics, controllers and sound, but so far we
 * have been limited to running entirely from RAM with no ability to load files
 * beyond the ones we embedded into the executable at build time. In order to be
 * able to have more than 2 MB of assets, we must understand how to load them on
 * demand from the disc in the console.
 *
 * The PS1's CD-ROM drive has a somewhat confusing architecture, being made up
 * of three different processors (the CD DSP, sector decoder and mechanism
 * controller or "mechacon") operating concurrently. The CPU only has direct
 * access to the sector decoder, whose main job is to handle data sectors during
 * a read. To actually start reading data or perform other tasks such as CD
 * audio playback, we must send commands to the mechacon through a mailbox built
 * into the decoder, then wait for it to respond by raising one or more
 * interrupts. In this example we'll simply use basic commands to initialize the
 * drive and fetch information such as its firmware version, region and current
 * status, laying down the foundations for the next examples that will actually
 * read data from a disc.
 *
 * NOTE: this example uses mechacon command and interrupt definitions from the
 * ps1/cdromdef.h header, as listing them directly here would only add
 * unnecessary clutter.
 */

#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include "common/font.h"
#include "common/gpu.h"
#include "ps1/cdromdef.h"
#include "ps1/delay.h"
#include "ps1/gpucmd.h"
#include "ps1/registers.h"

static void issueCDROMCommand(
	CDROMCommand  command,
	const uint8_t *parameters,
	size_t        paramLength
) {
	// Make sure the sector decoder's parameter mailbox won't be overrun.
	assert(paramLength <= 16);

	// Clear any leftover data from the mailbox. The decoder uses bank switching
	// for its registers: it has a 3-byte window into 4 readable and 12 writable
	// registers, plus a fixed HSTS/ADDRESS register to set the active bank.
	// HCLRCTL is in bank 1 (see ps1/registers.h), so we must write 1 to ADDRESS
	// before accessing it.
	CDROM_ADDRESS = 1;
	CDROM_HCLRCTL = CDROM_HCLRCTL_CLRPRM;
	delayMicroseconds(1);

	// Fill up the mailbox and set the command to be executed by the mechacon.
	// Both registers are in bank 0.
	CDROM_ADDRESS = 0;

	for (; paramLength > 0; paramLength--)
		CDROM_PARAMETER = *(parameters++);

	CDROM_COMMAND = command;
}

#define IRQ_TIMEOUT 5000000

static CDROMIRQType waitForCDROMIRQ(uint8_t *response, size_t maxRespLength) {
	// Check for incoming interrupts by polling the interrupt status register in
	// bank 1, with a timeout to avoid stalling shall the drive become
	// unresponsive. Note that this is a separate register from the CPU's
	// IRQ_STAT: the decoder has its own interrupt controller to keep track of
	// what exactly caused any IRQ sent to the main one.
	CDROM_ADDRESS = 1;

	for (int timeout = IRQ_TIMEOUT; timeout > 0; timeout -= 10) {
		// Bits 0-2 of HINTSTS were meant to represent three independent
		// notification flags from the mechacon, but in practice the firmware
		// uses them as a single 3-bit "interrupt type" code instead. These
		// flags are not guaranteed to update at exactly the same time
		// (especially on earlier console models that run the mechacon and
		// decoder on different clocks), so we must ensure they are stable
		// before continuing.
		int irq1 = CDROM_HINTSTS & CDROM_HINT_INT_BITMASK;
		int irq2 = CDROM_HINTSTS & CDROM_HINT_INT_BITMASK;

		if (irq1 && (irq1 == irq2)) {
			// Once the flags settle, we can safely reset them and read out any
			// bytes received in the response mailbox.
			for (; maxRespLength > 0; maxRespLength--) {
				if (!(CDROM_HSTS & CDROM_HSTS_RSLRRDY))
					break;

				*(response++) = CDROM_RESULT;
			}

			CDROM_HCLRCTL = CDROM_HCLRCTL_CLRINT_BITMASK;
			return irq1;
		}

		delayMicroseconds(10);
	}

	return CDROM_IRQ_NONE;
}

static void initCDROM(void) {
	// The CD-ROM sits on the same 16-bit bus as the SPU but is only an 8-bit
	// device, so it too needs its own bus configuration.
	BIU_DEV5_DELAY = 0
		| BIU_DEV_DELAY_WRITE_CYCLES(3)
		| BIU_DEV_DELAY_READ_CYCLES(4)
		| BIU_DEV_DELAY_RECOVERY
		| BIU_DEV_DELAY_PRESTROBE
		| BIU_DEV_DELAY_WIDTH_8
		| BIU_DEV_DELAY_ADDR_BITS(2);
	BIU_COM_DELAY  = 0
		| BIU_COM_DELAY_RECOVERY(5)
		| BIU_COM_DELAY_HOLD(2)
		| BIU_COM_DELAY_FLOAT(3)
		| BIU_COM_DELAY_PRESTROBE(1);

	// Initialize the sector decoder, clear any pending interrupts and mask all
	// IRQ signals so that they won't be needlessly forwarded to IRQ_STAT.
	CDROM_ADDRESS   = 0;
	CDROM_HCHPCTL   = 0;
	CDROM_ADDRESS   = 1;
	CDROM_HINTMSK_W = 0;
	CDROM_HCLRCTL   = 0
		| CDROM_HCLRCTL_CLRINT_BITMASK
		| CDROM_HCLRCTL_CLRBFEMPT
		| CDROM_HCLRCTL_CLRBFWRDY;

	// Initialize the mechacon and wait for its two responses: an "acknowledge"
	// interrupt first, followed by a "complete" interrupt once the drive is
	// ready (see ps1/cdromdef.h for details on which commands send which IRQs).
	// NOTE: in order to avoid corrupting the mailboxes we must always wait for
	// an acknowledge IRQ before sending another command. It is however safe to
	// send certain commands between the acknowledge and complete IRQs.
	issueCDROMCommand(CDROM_CMD_INIT, 0, 0);

	while (waitForCDROMIRQ(0, 0) != CDROM_IRQ_ACKNOWLEDGE)
		__asm__ volatile("");
	while (waitForCDROMIRQ(0, 0) != CDROM_IRQ_COMPLETE)
		__asm__ volatile("");
}

static void printCDROMInfo(char *output) {
	char *ptr = output;
	ptr      += sprintf(ptr, "CD-ROM drive information:\n");

	uint8_t      subcommand, response[16];
	CDROMIRQType irq;

	// Retrieve the mechacon's firmware version using a CD-ROM "test" command.
	// This command returns 4 bytes in its acknowledge IRQ: a build date in
	// binary-coded decimal format followed by a version number. There is no
	// complete IRQ.
	subcommand = CDROM_TEST_GET_VERSION;

	issueCDROMCommand(CDROM_CMD_TEST, &subcommand, sizeof(subcommand));
	irq = waitForCDROMIRQ(response, sizeof(response));

	if (irq == CDROM_IRQ_ACKNOWLEDGE)
		ptr += sprintf(
			ptr,
			"  Firmware version:\t%02X\n"
			"  Build date:\t\t%04d-%02X-%02X\n",
			response[3],
			1900 + cdrom_decodeBCD(response[0]),
			response[1],
			response[2]
		);
	else
		ptr += sprintf(ptr, "  Firmware version:\tunknown (got IRQ %d)\n", irq);

	if (response[3] != 0xc0) {
		// Fetch the drive's region using another test command. The returned
		// string ("for U/C", "for Japan", "for Europe" and so on) is not
		// null-terminated, so we must prefill the response buffer in order to
		// be able to print it.
		subcommand = CDROM_TEST_GET_REGION;
		__builtin_memset(response, 0, sizeof(response));

		issueCDROMCommand(CDROM_CMD_TEST, &subcommand, sizeof(subcommand));
		irq = waitForCDROMIRQ(response, sizeof(response));

		if (irq == CDROM_IRQ_ACKNOWLEDGE)
			ptr += sprintf(ptr, "  Drive region:\t%s\n", response);
		else
			ptr += sprintf(ptr, "  Drive region:\tunknown (got IRQ %d)\n", irq);
	} else {
		// Firmware version C0 does not support querying the region, but was
		// only ever used on early Japanese units.
		ptr += sprintf(ptr, "  Drive region:\tfor Japan (inferred)\n");
	}
}

static void printCDROMStatus(char *output) {
	char *ptr = output;
	ptr      += sprintf(ptr, "Current CD-ROM status:\n");

	uint8_t status;

	// Use the "nop" command to obtain the mechacon's current status flags.
	// Almost all non-test commands return them in the first byte of their
	// acknowledge responses, though some flags such as the lid status remain
	// set until a nop command specifically is sent to clear them. As with test
	// commands there is no complete IRQ to wait for.
	issueCDROMCommand(CDROM_CMD_NOP, 0, 0);
	CDROMIRQType irq = waitForCDROMIRQ(&status, sizeof(status));

	if (irq == CDROM_IRQ_ACKNOWLEDGE)
		ptr += sprintf(
			ptr,
			"  Status byte:\t%02X\n"
			"  Spindle motor:\t%s\n"
			"  Lid status:\t\t%s\n"
			"  Busy seeking:\t%s\n",
			status,
			(status & CDROM_CMDSTAT_SPINDLE_ON) ? "on"   : "off",
			(status & CDROM_CMDSTAT_LID_OPEN)   ? "open" : "closed",
			(status & CDROM_CMDSTAT_SEEKING)    ? "yes"  : "no"
		);
	else
		ptr += sprintf(ptr, "  Unknown (got IRQ %d)\n", irq);
}

#define SCREEN_HRES   GP1_HRES_320
#define SCREEN_VRES   GP1_VRES_256
#define SCREEN_WIDTH  320
#define SCREEN_HEIGHT 240

#define FONT_WIDTH       96
#define FONT_HEIGHT      56
#define FONT_COLOR_DEPTH GP0_COLOR_4BPP

extern const uint8_t fontTexture[], fontPalette[];

int main(int argc, const char **argv) {
	(void) argc;
	(void) argv;

	initSerialIO(115200);
	setupGPU(
		getCurrentVideoMode(),
		SCREEN_HRES,
		SCREEN_VRES,
		SCREEN_WIDTH,
		SCREEN_HEIGHT
	);
	initCDROM();

	TextureInfo font;

	uploadIndexedTexture(
		&font,
		fontTexture,
		fontPalette,
		SCREEN_WIDTH * 2,
		0,
		SCREEN_WIDTH * 2,
		FONT_HEIGHT,
		FONT_WIDTH,
		FONT_HEIGHT,
		FONT_COLOR_DEPTH
	);

	// Probe the drive's firmware information once, before entering the main
	// loop.
	char info[256];

	printCDROMInfo(info);

	GPUDMAChain dmaChains[2];
	bool        usingSecondFrame = false;

	for (;;) {
		int bufferX = usingSecondFrame ? SCREEN_WIDTH : 0;
		int bufferY = 0;

		GPUDMAChain *chain = &dmaChains[usingSecondFrame];
		usingSecondFrame   = !usingSecondFrame;

		uint32_t *ptr;

		GPU_GP1 = gp1_fbOffset(bufferX, bufferY);

		chain->nextPacket = chain->data;

		ptr    = allocateGP0Packet(chain, 4);
		ptr[0] = gp0_setPage(0, true, false);
		ptr[1] = gp0_fbOffset1(bufferX, bufferY);
		ptr[2] = gp0_fbOffset2(
			bufferX + SCREEN_WIDTH  - 1,
			bufferY + SCREEN_HEIGHT - 1
		);
		ptr[3] = gp0_fbOrigin(bufferX, bufferY);

		ptr    = allocateGP0Packet(chain, 3);
		ptr[0] = gp0_rgb(64, 64, 64) | gp0_vramFill();
		ptr[1] = gp0_xy(bufferX, bufferY);
		ptr[2] = gp0_xy(SCREEN_WIDTH, SCREEN_HEIGHT);

		// Probe and display the drive's status once per frame.
		char status[256];

		printCDROMStatus(status);
		printString(chain, &font, 16, 32, info);
		printString(chain, &font, 16, 96, status);

		*(chain->nextPacket) = gp0_endTag(0);

		waitForGP0Ready();
		waitForVSync();
		sendGPULinkedList(chain->data);
	}

	return 0;
}
