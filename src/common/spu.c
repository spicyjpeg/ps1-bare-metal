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
#include "common/spu.h"
#include "ps1/delay.h"
#include "ps1/registers.h"

#define DMA_MAX_CHUNK_SIZE 16

#define DUMMY_BLOCK_OFFSET   (SPU_RAM_SIZE - 32)
#define REVERB_BUFFER_OFFSET (SPU_RAM_SIZE - 16)

static const uint8_t dummyBlock[] = {
	0, 5, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

void initSPU(void) {
	BIU_DEV4_DELAY = 0
		| BIU_DEV_DELAY_WRITE_CYCLES(1)
		| BIU_DEV_DELAY_READ_CYCLES(14)
		| BIU_DEV_DELAY_RECOVERY
		| BIU_DEV_DELAY_WIDTH_16
		| BIU_DEV_DELAY_AUTO_INCR
		| BIU_DEV_DELAY_ADDR_BITS(9)
		| BIU_DEV_DELAY_DMA_CYCLES(0)
		| BIU_DEV_DELAY_DMA_CYCLES_ENABLE;
	BIU_COM_DELAY  = 0
		| BIU_COM_DELAY_RECOVERY(5)
		| BIU_COM_DELAY_HOLD(2)
		| BIU_COM_DELAY_FLOAT(3)
		| BIU_COM_DELAY_PRESTROBE(1);

	SPU_ATTR     = 0
		| SPU_ATTR_XFER_NONE
		| SPU_ATTR_ENABLE;
	SPU_RAM_CTRL = 0
		| SPU_RAM_CTRL_BANKS_1
		| SPU_RAM_CTRL_SIZE_512KB;
	SPU_MVOLL    = 0x3fff & ~SPU_VOL_SLIDE_ENABLE;
	SPU_MVOLR    = 0x3fff & ~SPU_VOL_SLIDE_ENABLE;
	SPU_EVOLL    = 0;
	SPU_EVOLR    = 0;
	SPU_ESA      = REVERB_BUFFER_OFFSET / SPU_RAM_ADDR_UNIT;

	DMA_DPCR         |= DMA_DPCR_CH_ENABLE(DMA_SPU);
	DMA_CHCR(DMA_SPU) = 0;

	sendSPURAMData(dummyBlock, DUMMY_BLOCK_OFFSET, sizeof(dummyBlock));
	waitForSPUDMADone();

	resetAllSPUChannels();
	SPU_ATTR = 0
		| SPU_ATTR_XFER_NONE
		| SPU_ATTR_DAC_ENABLE
		| SPU_ATTR_ENABLE;
}

void waitForSPUDMADone(void) {
	while (DMA_CHCR(DMA_SPU) & DMA_CHCR_ENABLE)
		__asm__ volatile("");

	delayMicroseconds(2 * DMA_MAX_CHUNK_SIZE);
}

void sendSPURAMData(const void *data, unsigned int offset, size_t length) {
	waitForSPUDMADone();
	assert(!((uintptr_t) data % 4));
	assert(!(offset % SPU_RAM_ADDR_UNIT));
	assert((offset + length) <= SPU_RAM_SIZE);

	length = (length + 3) / 4;
	size_t chunkSize, numChunks;

	if (length < DMA_MAX_CHUNK_SIZE) {
		chunkSize = length;
		numChunks = 1;
	} else {
		chunkSize = DMA_MAX_CHUNK_SIZE;
		numChunks = length / DMA_MAX_CHUNK_SIZE;

		assert(!(length % DMA_MAX_CHUNK_SIZE));
	}

	uint16_t attr = SPU_ATTR & ~SPU_ATTR_XFER_BITMASK;
	SPU_ATTR      = attr;

	while ((SPU_STATX & SPU_STATX_XFER_BITMASK) != SPU_STATX_XFER_NONE)
		__asm__ volatile("");

	SPU_TSA  = offset / SPU_RAM_ADDR_UNIT;
	SPU_ATTR = attr | SPU_ATTR_XFER_DMA_WRITE;

	while ((SPU_STATX & SPU_STATX_XFER_BITMASK) != SPU_STATX_XFER_DMA_WRITE)
		__asm__ volatile("");

	// SPU RAM writes can be performed with the default bus configuration, while
	// reads require slightly increasing DMA waitstates.
	uint32_t ctrl  = BIU_DEV4_DELAY & ~BIU_DEV_DELAY_DMA_CYCLES_BITMASK;
	BIU_DEV4_DELAY = ctrl           |  BIU_DEV_DELAY_DMA_CYCLES(0);

	DMA_MADR(DMA_SPU) = (uintptr_t) data;
	DMA_BCR (DMA_SPU) = chunkSize | (numChunks << 16);
	DMA_CHCR(DMA_SPU) = 0
		| DMA_CHCR_WRITE
		| DMA_CHCR_MODE_SLICE
		| DMA_CHCR_ENABLE;
}

void receiveSPURAMData(void *data, unsigned int offset, size_t length) {
	waitForSPUDMADone();
	assert(!((uintptr_t) data % 4));
	assert(!(offset % SPU_RAM_ADDR_UNIT));
	assert((offset + length) <= SPU_RAM_SIZE);

	length = (length + 3) / 4;
	size_t chunkSize, numChunks;

	if (length < DMA_MAX_CHUNK_SIZE) {
		chunkSize = length;
		numChunks = 1;
	} else {
		chunkSize = DMA_MAX_CHUNK_SIZE;
		numChunks = length / DMA_MAX_CHUNK_SIZE;

		assert(!(length % DMA_MAX_CHUNK_SIZE));
	}

	uint16_t attr = SPU_ATTR & ~SPU_ATTR_XFER_BITMASK;
	SPU_ATTR      = attr;

	while ((SPU_STATX & SPU_STATX_XFER_BITMASK) != SPU_STATX_XFER_NONE)
		__asm__ volatile("");

	SPU_TSA  = offset / SPU_RAM_ADDR_UNIT;
	SPU_ATTR = attr | SPU_ATTR_XFER_DMA_READ;

	while ((SPU_STATX & SPU_STATX_XFER_BITMASK) != SPU_STATX_XFER_DMA_READ)
		__asm__ volatile("");

	uint32_t ctrl  = BIU_DEV4_DELAY & ~BIU_DEV_DELAY_DMA_CYCLES_BITMASK;
	BIU_DEV4_DELAY = ctrl           |  BIU_DEV_DELAY_DMA_CYCLES(2);

	DMA_MADR(DMA_SPU) = (uintptr_t) data;
	DMA_BCR (DMA_SPU) = chunkSize | (numChunks << 16);
	DMA_CHCR(DMA_SPU) = 0
		| DMA_CHCR_READ
		| DMA_CHCR_MODE_SLICE
		| DMA_CHCR_ENABLE;
}

void resetAllSPUChannels(void) {
	for (int i = 0; i < SPU_NUM_CHANNELS; i++) {
		SPU_CH_VOLL (i) = 0;
		SPU_CH_VOLR (i) = 0;
		SPU_CH_PITCH(i) = SPU_PITCH_UNIT;
		SPU_CH_SSA  (i) = DUMMY_BLOCK_OFFSET / SPU_RAM_ADDR_UNIT;
	}

	SPU_PMON0 = 0;
	SPU_PMON1 = 0;
	SPU_NON0  = 0;
	SPU_NON1  = 0;
	SPU_EON0  = 0;
	SPU_EON1  = 0;

	SPU_KON0 = 0xffff;
	SPU_KON1 = 0x00ff;
}

int findFreeSPUChannel(void) {
	for (int i = 0; i < SPU_NUM_CHANNELS; i++) {
		if (!SPU_CH_ENVX(i))
			return i;
	}

	return -1;
}

int playSample(unsigned int offset, unsigned int sampleRate, int16_t volume) {
	int ch = findFreeSPUChannel();

	if (ch >= 0) {
		SPU_CH_VOLL (ch) = (volume / 2) & ~SPU_VOL_SLIDE_ENABLE;
		SPU_CH_VOLR (ch) = (volume / 2) & ~SPU_VOL_SLIDE_ENABLE;
		SPU_CH_PITCH(ch) = (sampleRate * SPU_PITCH_UNIT) / 44100;
		SPU_CH_SSA  (ch) = offset / SPU_RAM_ADDR_UNIT;

		SPU_CH_ADSR1(ch) = 0
			| SPU_ADSR1_SL(15)
			| SPU_ADSR1_DR(15)
			| SPU_ADSR1_AR(0);
		SPU_CH_ADSR2(ch) = 0
			| SPU_ADSR2_RR(0)
			| SPU_ADSR2_SR(0);
		SPU_CH_ENVX (ch) = volume;

		if (ch < 16)
			SPU_KON0 = 1 << ch;
		else
			SPU_KON1 = 1 << (ch - 16);
	}

	return ch;
}
