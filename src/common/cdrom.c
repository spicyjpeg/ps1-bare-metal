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
#include <stdbool.h>
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

	DMA_DPCR           |= DMA_DPCR_CH_ENABLE(DMA_CDROM);
	DMA_CHCR(DMA_CDROM) = 0;

	issueCDROMCommand(CDROM_CMD_INIT, 0, 0);

	CDROMIRQType irq;

	irq = waitForCDROMIRQ(0, 0);
	assert(irq == CDROM_IRQ_ACKNOWLEDGE);
	irq = waitForCDROMIRQ(0, 0);
	assert(irq == CDROM_IRQ_COMPLETE);
}

void setupCDROMAudio(bool mono) {
	CDROM_ADDRESS = 2;
	CDROM_ATV0    = mono ? 64 : 128; // L -> L
	CDROM_ATV1    = mono ? 64 :   0; // L -> R
	CDROM_ADDRESS = 3;
	CDROM_ATV2    = mono ? 64 : 128; // R -> R
	CDROM_ATV3    = mono ? 64 :   0; // R -> L
	CDROM_ADPCTL  = CDROM_ADPCTL_CHNGATV;

	SPU_ATTR |= SPU_ATTR_I2SA_ENABLE;
	SPU_AVOLL = 0x7fff;
	SPU_AVOLR = 0x7fff;
}

void waitForCDROMDMADone(void) {
	while (DMA_CHCR(DMA_CDROM) & DMA_CHCR_ENABLE)
		__asm__ volatile("");
}

void receiveCDROMSector(void *data, size_t length) {
	waitForCDROMDMADone();
	assert(!((uintptr_t) data % 4));

	length = (length + 3) / 4;

	CDROM_ADDRESS = 0;
	CDROM_HCHPCTL = CDROM_HCHPCTL_BFRD;

	while (!(CDROM_HSTS & CDROM_HSTS_DRQSTS))
		__asm__ volatile("");

	DMA_MADR(DMA_CDROM) = (uintptr_t) data;
	DMA_BCR (DMA_CDROM) = length;
	DMA_CHCR(DMA_CDROM) = 0
		| DMA_CHCR_READ
		| DMA_CHCR_MODE_BURST
		| DMA_CHCR_ENABLE
		| DMA_CHCR_TRIGGER;
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

bool playCDDA(unsigned int track, uint8_t mode) {
	checkCDROMIRQ(0, 0);

	mode |= CDROM_MODE_CDDA;

	issueCDROMCommand(CDROM_CMD_SETMODE, &mode, sizeof(mode));
	if (waitForCDROMIRQ(0, 0) != CDROM_IRQ_ACKNOWLEDGE)
		return false;

	uint8_t bcdTrack = cdrom_encodeBCD(track);

	if (track > 0)
		issueCDROMCommand(CDROM_CMD_PLAY, &bcdTrack, sizeof(bcdTrack));
	else
		issueCDROMCommand(CDROM_CMD_PLAY, 0, 0);
	if (waitForCDROMIRQ(0, 0) != CDROM_IRQ_ACKNOWLEDGE)
		return false;

	return true;
}

bool startCDROMRead(unsigned int lba, uint8_t mode, bool retry) {
	checkCDROMIRQ(0, 0);

	uint8_t msf[3];

	cdrom_lbaToMSF(msf, lba);

	issueCDROMCommand(CDROM_CMD_SETLOC, msf, sizeof(msf));
	if (waitForCDROMIRQ(0, 0) != CDROM_IRQ_ACKNOWLEDGE)
		return false;

	issueCDROMCommand(CDROM_CMD_SETMODE, &mode, sizeof(mode));
	if (waitForCDROMIRQ(0, 0) != CDROM_IRQ_ACKNOWLEDGE)
		return false;

	issueCDROMCommand(retry ? CDROM_CMD_READN : CDROM_CMD_READS, 0, 0);
	if (waitForCDROMIRQ(0, 0) != CDROM_IRQ_ACKNOWLEDGE)
		return false;

	return true;
}

bool pauseCDROMRead(void) {
	checkCDROMIRQ(0, 0);

	issueCDROMCommand(CDROM_CMD_PAUSE, 0, 0);
	if (waitForCDROMIRQ(0, 0) != CDROM_IRQ_ACKNOWLEDGE)
		return false;
	if (waitForCDROMIRQ(0, 0) != CDROM_IRQ_COMPLETE)
		return false;

	return true;
}

size_t readCDROMSectorsBlocking(
	void         *data,
	unsigned int lba,
	size_t       numSectors,
	uint8_t      mode
) {
	if (!startCDROMRead(lba, mode, true))
		return 0;

	uint8_t *ptr         = (uint8_t *) data;
	size_t  sectorLength = cdrom_sectorLength(mode);
	size_t  sectorsRead  = 0;

	for (; sectorsRead < numSectors; sectorsRead++) {
		if (waitForCDROMIRQ(0, 0) != CDROM_IRQ_DATA_READY)
			break;

		receiveCDROMSector(ptr, sectorLength);
		waitForCDROMDMADone();

		ptr += sectorLength;
	}

	pauseCDROMRead();

	return sectorsRead;
}
