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

#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include "common/cdrom.h"
#include "ps1/cdromdef.h"
#include "ps1/delay.h"
#include "ps1/registers.h"

#define IRQ_TIMEOUT 5000000

void initCDROM(void) {
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

	CDROM_ADDRESS   = 0;
	CDROM_HCHPCTL   = 0;
	CDROM_ADDRESS   = 1;
	CDROM_HINTMSK_W = 0;
	CDROM_HCLRCTL   = 0
		| CDROM_HCLRCTL_CLRINT_BITMASK
		| CDROM_HCLRCTL_CLRBFEMPT
		| CDROM_HCLRCTL_CLRBFWRDY;

	issueCDROMCommand(CDROM_CMD_INIT, 0, 0);

	CDROMIRQType irq;

	irq = waitForCDROMIRQ(0, 0);
	assert(irq == CDROM_IRQ_ACKNOWLEDGE);
	irq = waitForCDROMIRQ(0, 0);
	assert(irq == CDROM_IRQ_COMPLETE);
}

void waitForCDROMDMADone(void) {
	while (DMA_CHCR(DMA_CDROM) & DMA_CHCR_ENABLE)
		__asm__ volatile("");
}

void issueCDROMCommand(
	CDROMCommand  command,
	const uint8_t *parameters,
	size_t        paramLength
) {
	assert(paramLength <= 16);

	CDROM_ADDRESS = 1;
	CDROM_HCLRCTL = CDROM_HCLRCTL_CLRPRM;
	delayMicroseconds(1);

	CDROM_ADDRESS = 0;

	for (; paramLength > 0; paramLength--)
		CDROM_PARAMETER = *(parameters++);

	CDROM_COMMAND = command;
}

CDROMIRQType checkCDROMIRQ(uint8_t *response, size_t maxRespLength) {
	CDROM_ADDRESS = 1;

	int irq1, irq2;

	do {
		irq1 = CDROM_HINTSTS & CDROM_HINT_INT_BITMASK;
		irq2 = CDROM_HINTSTS & CDROM_HINT_INT_BITMASK;
	} while (irq1 != irq2);

	if (irq1) {
		for (; maxRespLength > 0; maxRespLength--) {
			if (!(CDROM_HSTS & CDROM_HSTS_RSLRRDY))
				break;

			*(response++) = CDROM_RESULT;
		}

		CDROM_HCLRCTL = CDROM_HCLRCTL_CLRINT_BITMASK;
	}

	return irq1;
}

CDROMIRQType waitForCDROMIRQ(uint8_t *response, size_t maxRespLength) {
	for (int timeout = IRQ_TIMEOUT; timeout > 0; timeout -= 5) {
		CDROMIRQType irq = checkCDROMIRQ(response, maxRespLength);

		if (irq)
			return irq;

		delayMicroseconds(5);
	}

	return CDROM_IRQ_NONE;
}
