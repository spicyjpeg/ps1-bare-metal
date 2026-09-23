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
 * In this tutorial we are going to take a preliminary look at the PS1's audio
 * capabilities. At the heart of the console's sound system is the SPU, a
 * descendant of the S-DSP chip Sony designed for the SNES capable of playing
 * samples from its dedicated 512 KB RAM bank, mixing up to 24 mono channels and
 * simulating reverb.
 *
 * Using the SPU is relatively straightforward. Like VRAM, SPU RAM is not
 * directly mapped to the CPU but must instead be accessed through the SPU
 * itself using DMA. Once our sample data is loaded, we can point one of the 24
 * channels at it, set its volume (optionally with a custom ADSR envelope) and
 * kick off playback. End and loop points are embedded in the data, so no CPU
 * intervention is needed to stop or restart playback of a properly encoded
 * sample after it ends.
 *
 * The only minor complication comes from the fact the SPU only supports onez
 * data format - a variant of the proprietary "bit rate reduction" (BRR) ADPCM
 * codec introduced with the S-DSP and subsequently used on later PlayStations.
 * The build script for this example uses psxavenc to perform the encoding and
 * embed the resulting data into the executable, requiring it to be installed
 * from its repository and added to your PATH:
 *     https://codeberg.org/WonderfulToolchain/psxavenc
 */

#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include "common/font.h"
#include "common/gpu.h"
#include "ps1/delay.h"
#include "ps1/gpucmd.h"
#include "ps1/registers.h"

// Since all SPU registers are only 16 bits wide, pointers within SPU RAM are
// always specified in 8-byte units in order to address the full 512 KB.
#define SPU_RAM_ADDR_UNIT 8
#define SPU_RAM_SIZE      (0x10000 * SPU_RAM_ADDR_UNIT)

#define DMA_MAX_CHUNK_SIZE 16

static void waitForSPUDMADone(void) {
	// Wait for the SPU's DMA channel to finish any pending transfer first.
	while (DMA_CHCR(DMA_SPU) & DMA_CHCR_ENABLE)
		__asm__ volatile("");

	// As with the GPU, the SPU has an internal 64-byte (16-word) queue it
	// receives DMA chunks into before writing them to SPU RAM. We'll wait the
	// ~2 us per word it takes for the SPU to drain it.
	delayMicroseconds(2 * DMA_MAX_CHUNK_SIZE);
}

static void sendSPURAMData(
	const void   *data,
	unsigned int offset,
	size_t       length
) {
	waitForSPUDMADone();
	assert(!((uintptr_t) data % 4));

	// Make sure the destination offset is aligned and the transfer won't
	// overflow SPU RAM.
	assert(!(offset % SPU_RAM_ADDR_UNIT));
	assert((offset + length) <= SPU_RAM_SIZE);

	// The logic for length rounding and chunking is pretty much identical to
	// how we implemented VRAM DMA transfers, with the same data length
	// requirements. Most BRR encoders pad samples to a multiple of 64 bytes by
	// default, so this shouldn't be an issue.
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

	// Terminate any previous transfer and disconnect the SPU from DMA.
	uint16_t attr = SPU_ATTR & ~SPU_ATTR_XFER_BITMASK;
	SPU_ATTR      = attr;

	while ((SPU_STATX & SPU_STATX_XFER_BITMASK) != SPU_STATX_XFER_NONE)
		__asm__ volatile("");

	// Set where the next transfer shall be placed in SPU RAM (in 8-byte units)
	// and re-enable DMA synchronization.
	SPU_TSA  = offset / SPU_RAM_ADDR_UNIT;
	SPU_ATTR = attr | SPU_ATTR_XFER_DMA_WRITE;

	while ((SPU_STATX & SPU_STATX_XFER_BITMASK) != SPU_STATX_XFER_DMA_WRITE)
		__asm__ volatile("");

	// Kick off the transfer at the DMA side.
	DMA_MADR(DMA_SPU) = (uintptr_t) data;
	DMA_BCR (DMA_SPU) = chunkSize | (numChunks << 16);
	DMA_CHCR(DMA_SPU) = 0
		| DMA_CHCR_WRITE
		| DMA_CHCR_MODE_SLICE
		| DMA_CHCR_ENABLE;
}

// SPU RAM is shared between samples, audio capture buffers (in the first 4 KB)
// and reverb buffers (at the end, with a configurable size but no less than 16
// bytes). We'll have to ensure we don't bump into either when uploading our
// samples.
#define SPU_SAMPLE_OFFSET    0x1000
#define DUMMY_BLOCK_OFFSET   (SPU_RAM_SIZE - 32)
#define REVERB_BUFFER_OFFSET (SPU_RAM_SIZE - 16)

// All SPU channels are always playing in the background, with the "end of
// sample" flag in the BRR data only muting channels without actually stopping
// them. This is usually not a problem - a playing-but-muted channel produces no
// sound - but may become one when using more advanced features such as the SPU
// interrupt. To prevent any erratic behavior it's thus common practice to keep
// a silent looping sample in SPU RAM and "play" it on all unused channels.
static const uint8_t dummyBlock[] = {
	// The BRR ADPCM format uses 16-byte blocks, each consisting of a 2-byte
	// header followed by 28 packed sample nibbles. The second byte holds end
	// and loop flags; a value of 5 instructs the channel to mute itself then
	// jump back to the beginning of the block.
	0, 5, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

#define SPU_NUM_CHANNELS 24
#define SPU_PITCH_UNIT   (1 << 12)

static void resetAllSPUChannels(void) {
	// Mute all channels, set their sample rate to a non-zero value and point
	// them at the dummy block. While VOLL, VOLR and PITCH take effect
	// immediately, SSA only sets the address the channel will jump to once
	// "keyed on" (restarted).
	for (int i = 0; i < SPU_NUM_CHANNELS; i++) {
		SPU_CH_VOLL (i) = 0;
		SPU_CH_VOLR (i) = 0;
		SPU_CH_PITCH(i) = SPU_PITCH_UNIT;
		SPU_CH_SSA  (i) = DUMMY_BLOCK_OFFSET / SPU_RAM_ADDR_UNIT;
	}

	// Ensure pitch modulation, noise mode and reverb output is disabled for all
	// channels.
	SPU_PMON0 = 0;
	SPU_PMON1 = 0;
	SPU_NON0  = 0;
	SPU_NON1  = 0;
	SPU_EON0  = 0;
	SPU_EON1  = 0;

	// Key on all channels to finalize the jump to the dummy block.
	SPU_KON0 = 0xffff;
	SPU_KON1 = 0x00ff;
}

static void initSPU(void) {
	// The SPU sits on a 16-bit bus shared with the CD-ROM drive, BIOS ROM and
	// parallel port. For each of those, the CPU's bus interface must be
	// configured to use the proper signals and timings for the device. The BIOS
	// kernel already performs this initialization on startup, but it's useful
	// to do it again explicitly as a safeguard.
	BIU_DEV4_CTRL = 0
		| BIU_CTRL_WRITE_DELAY(1)
		| BIU_CTRL_READ_DELAY(14)
		| BIU_CTRL_RECOVERY
		| BIU_CTRL_WIDTH_16
		| BIU_CTRL_AUTO_INCR
		| BIU_CTRL_ADDR_BITS(9)
		| BIU_CTRL_DMA_DELAY(0)
		| BIU_CTRL_DMA_DELAY_ENABLE;
	BIU_COM_DELAY = 0
		| BIU_COM_DELAY_RECOVERY(5)
		| BIU_COM_DELAY_HOLD(2)
		| BIU_COM_DELAY_FLOAT(3)
		| BIU_COM_DELAY_PRESTROBE(1);

	// Enable the SPU with the audio output temporarily muted and set up basic
	// settings: RAM size and master volume in 15-bit signed format (the topmost
	// bit enables a "volume slide" feature we won't use).
	SPU_ATTR     = 0
		| SPU_ATTR_XFER_NONE
		| SPU_ATTR_ENABLE;
	SPU_RAM_CTRL = 0
		| SPU_RAM_CTRL_BANKS_1
		| SPU_RAM_CTRL_SIZE_512KB;
	SPU_MVOLL    = 0x3fff & ~SPU_VOL_SLIDE_ENABLE;
	SPU_MVOLR    = 0x3fff & ~SPU_VOL_SLIDE_ENABLE;

	// Set the reverb volume (in 16-bit signed format this time as there is no
	// volume slide option) and start address of its buffer. Reverb output
	// cannot be completely disabled, so we'll mute it and shrink the buffer to
	// the minimum allowed size of 16 bytes.
	SPU_EVOLL = 0;
	SPU_EVOLR = 0;
	SPU_ESA   = REVERB_BUFFER_OFFSET / SPU_RAM_ADDR_UNIT;

	// Enable and reset the SPU's DMA channel.
	DMA_DPCR         |= DMA_DPCR_CH_ENABLE(DMA_SPU);
	DMA_CHCR(DMA_SPU) = 0;

	// Upload our dummy sample at the end of SPU RAM, then play it on all
	// channels to reset them and unmute the audio output.
	sendSPURAMData(dummyBlock, DUMMY_BLOCK_OFFSET, sizeof(dummyBlock));
	waitForSPUDMADone();

	resetAllSPUChannels();
	SPU_ATTR = 0
		| SPU_ATTR_XFER_NONE
		| SPU_ATTR_DAC_ENABLE
		| SPU_ATTR_ENABLE;
}

static int findFreeSPUChannel(void) {
	// Return the index of the first channel (if any) whose current ADSR volume
	// is zero, which implies either the "end of sample" flag was reached or the
	// channel was previously keyed off and the envelope went idle.
	for (int i = 0; i < SPU_NUM_CHANNELS; i++) {
		if (!SPU_CH_ENVX(i))
			return i;
	}

	return -1;
}

static int playSample(
	unsigned int offset,
	unsigned int sampleRate,
	int16_t      volume
) {
	int ch = findFreeSPUChannel();

	if (ch >= 0) {
		// Set the channel's volume (with volume slide disabled), pitch (as a
		// fraction of the SPU's output sample rate in 4.12 fixed-point format)
		// and start address.
		SPU_CH_VOLL (ch) = (volume / 2) & ~SPU_VOL_SLIDE_ENABLE;
		SPU_CH_VOLR (ch) = (volume / 2) & ~SPU_VOL_SLIDE_ENABLE;
		SPU_CH_PITCH(ch) = (sampleRate * SPU_PITCH_UNIT) / 44100;
		SPU_CH_SSA  (ch) = offset / SPU_RAM_ADDR_UNIT;

		// Configure the channel's ADSR envelope. For basic sound playback we
		// want an envelope consisting only of a full-volume sustain phase with
		// no attack, decay or release.
		SPU_CH_ADSR1(ch) = 0
			| SPU_ADSR1_SL(15)
			| SPU_ADSR1_DR(15)
			| SPU_ADSR1_AR(0);
		SPU_CH_ADSR2(ch) = 0
			| SPU_ADSR2_RR(0)
			| SPU_ADSR2_SR(0);

		// Initialize the envelope volume. This is not required by the SPU
		// itself, but will prevent findFreeSPUChannel() from returning this
		// channel again during the time it takes for the SPU to first update
		// ENVX.
		SPU_CH_ENVX(ch) = volume;

		// Key on the channel to apply the SSA change and restart the envelope.
		if (ch < 16)
			SPU_KON0 = 1 << ch;
		else
			SPU_KON1 = 1 << (ch - 16);
	}

	return ch;
}

static void printChannelInfo(char *output) {
	char *ptr = output;
	ptr      += sprintf(ptr, "Active SPU channels:\n");

	for (int i = 0; i < SPU_NUM_CHANNELS; i++) {
		int16_t  envx = (int16_t) SPU_CH_ENVX(i);
		uint16_t ssa  = SPU_CH_SSA(i);

		// Do not show channels that are either idle or playing the dummy block.
		// Note that SSA is *not* automatically updated with the current address
		// the channel is playing from.
		if (!envx || (ssa == (DUMMY_BLOCK_OFFSET / 8)))
			continue;

		ptr += sprintf(
			ptr,
			"  #%d\tADSR: %d%%, start: %05X, loop: %05X\n",
			i,
			(envx * 100) >> 15,
			ssa            * SPU_RAM_ADDR_UNIT,
			SPU_CH_LSAX(i) * SPU_RAM_ADDR_UNIT
		);
	}
}

#define SCREEN_HRES   GP1_HRES_320
#define SCREEN_VRES   GP1_VRES_256
#define SCREEN_WIDTH  320
#define SCREEN_HEIGHT 240

#define FONT_WIDTH       96
#define FONT_HEIGHT      56
#define FONT_COLOR_DEPTH GP0_COLOR_4BPP

extern const uint8_t fontTexture[], fontPalette[];

// The sample data encoded by psxavenc will be embedded into the executable by
// our CMake script, but we also need to know its size and sample rate in order
// to play it at the correct pitch.
#define SAMPLE_RATE 22050

extern const uint8_t sampleData[];
extern const size_t  sampleLength;

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
	initSPU();

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

	// Upload our sample to SPU RAM after the capture buffers.
	sendSPURAMData(sampleData, SPU_SAMPLE_OFFSET, sampleLength);
	waitForSPUDMADone();

	GPUDMAChain dmaChains[2];
	bool        usingSecondFrame = false;
	int         frameCounter     = 0;
	int         nextTrigger      = 0;

	for (;;) {
		int bufferX = usingSecondFrame ? SCREEN_WIDTH : 0;
		int bufferY = 0;

		GPUDMAChain *chain = &dmaChains[usingSecondFrame];
		usingSecondFrame   = !usingSecondFrame;
		frameCounter++;

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

		// Display all currently active channels.
		char info[256];

		printChannelInfo(info);
		printString(chain, &font, 16, 32, info);

		// Trigger playback of our sound every 120 frames (2 or 2.4 seconds
		// depending on video mode).
		if (frameCounter > nextTrigger) {
			playSample(SPU_SAMPLE_OFFSET, SAMPLE_RATE, 0x7fff);
			nextTrigger = frameCounter + 120;
		}

		*(chain->nextPacket) = gp0_endTag(0);

		waitForGP0Ready();
		waitForVSync();
		sendGPULinkedList(chain->data);
	}

	return 0;
}
