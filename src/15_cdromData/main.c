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
 * Having seen how to play CD audio tracks, we can now move onto the CD-ROM
 * drive's main job: reading files and other data from the disc. A CD-ROM is a
 * block device, meaning its data is laid out as a series of fixed-size blocks
 * or sectors, each indexed by its logical block address (LBA). These sectors
 * typically hold 2048 bytes each and are placed in a data track, to distinguish
 * them from any CD-DA track on the same disc. Files and directories are an
 * abstraction layer that sits on top of the raw blocks, but nothing stops us
 * from accessing any content directly if we know its LBAs.
 *
 * The PS1's CD-ROM drive reads data in a similar way to how it plays audio:
 * once given a starting LBA, it will keep reading sectors sequentially from
 * there and send us their contents one at a time until we manually pause it.
 *
 * This example requires a PS1 game disc or other data CD in the console. The
 * drive enforces region checks when reading data, so if you use a burned or
 * non-game disc you will need a modchip or a loader that performs CD-ROM
 * unlocking (e.g. Unirom or tonyhax, on non-Japanese consoles). On an emulator
 * you can use the "swap disc" option after launching the executable.
 */

#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include "common/cdrom.h"
#include "common/font.h"
#include "common/gpu.h"
#include "common/sio0.h"
#include "ps1/cdromdef.h"
#include "ps1/gpucmd.h"
#include "ps1/registers.h"

static void initCDROM_(void) {
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

	// Enable and reset the DMA channel allocated to the CD-ROM decoder.
	DMA_DPCR           |= DMA_DPCR_CH_ENABLE(DMA_CDROM);
	DMA_CHCR(DMA_CDROM) = 0;

	issueCDROMCommand(CDROM_CMD_INIT, 0, 0);

	CDROMIRQType irq;

	irq = waitForCDROMIRQ(0, 0);
	assert(irq == CDROM_IRQ_ACKNOWLEDGE);
	irq = waitForCDROMIRQ(0, 0);
	assert(irq == CDROM_IRQ_COMPLETE);
}

static void waitForCDROMDMADone_(void) {
	while (DMA_CHCR(DMA_CDROM) & DMA_CHCR_ENABLE)
		__asm__ volatile("");
}

static void receiveCDROMSector_(void *data, size_t length) {
	waitForCDROMDMADone_();
	assert(!((uintptr_t) data % 4));

	length = (length + 3) / 4;

	// While the mechacon is responsible for starting and stopping reads, the
	// actual sector data is processed by the CD-ROM decoder and stored in its
	// buffer, separately from commands and responses. In order to fetch a read
	// sector we must thus request a buffer read directly from the decoder.
	CDROM_ADDRESS = 0;
	CDROM_HCHPCTL = CDROM_HCHPCTL_BFRD;

	// We can then perform the fetch via DMA. Unlike most other peripherals the
	// decoder not support DMA slice mode, so we must instead synchronize
	// manually (by waiting until it reports to be ready for buffer access) then
	// trigger a burst mode transfer.
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

static bool startCDROMRead_(unsigned int lba, uint8_t mode, bool retry) {
	checkCDROMIRQ(0, 0);

	// The mechacon does not accept LBAs directly and instead references both
	// audio and data sectors by their CD-DA timestamps, consisting of minute,
	// second and frame indices in BCD format. We'll use the helpers defined in
	// ps1/cdromdef.h to convert between the two formats.
	uint8_t msf[3];

	cdrom_lbaToMSF(msf, lba);

	// Set the mode bitfield and converted starting location, then kick off the
	// read. The mechacon supports two different read commands:
	// - CDROM_CMD_READN will automatically seek back and retry reading any
	//   corrupted sectors, which is the desired behavior in most cases;
	// - CDROM_CMD_READS will instead ignore errors and move on. This is meant
	//   for real-time audio/video streaming, where dropping unreadable frames
	//   may be preferable to desynchronizing playback to attempt a retry.
	// Like the play command, no complete IRQ is raised after starting a read.
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

// We'll wrap the process of loading a contiguous span of sectors from the
// CD-ROM into RAM into a single function. This is once again a simplistic
// implementation that blocks until the entire read is complete; more advanced
// use cases such as streaming data without blocking the main loop would require
// a different approach.
static size_t readCDROMSectorsBlocking_(
	void         *data,
	unsigned int lba,
	size_t       numSectors,
	uint8_t      mode
) {
	if (!startCDROMRead_(lba, mode, true))
		return 0;

	// CD-ROM sectors are actually larger than 2048 bytes under the hood: each
	// one contains additional metadata and error correction information used
	// internally. The drive supports returning this extra data by setting mode
	// flags, so we must determine which sector size was requested and adjust
	// our pointer arithmetic accordingly.
	uint8_t *ptr         = (uint8_t *) data;
	size_t  sectorLength = cdrom_sectorLength(mode);
	size_t  sectorsRead  = 0;

	for (; sectorsRead < numSectors; sectorsRead++) {
		// The "data ready" IRQ is used by the mechacon to signal that a new
		// sector has been read into the decoder's buffer. Since the decoder
		// cannot queue more than one sector, we must fetch it as quickly as
		// possible after receiving this response.
		if (waitForCDROMIRQ(0, 0) != CDROM_IRQ_DATA_READY)
			break;

		receiveCDROMSector_(ptr, sectorLength);
		waitForCDROMDMADone_();

		ptr += sectorLength;
	}

	// Halt reading as soon as all requested data has been received, or if an
	// unexpected IRQ occurred. The pause command we used for CD-DA also works
	// during a data read.
	pauseCDROMRead();

	return sectorsRead;
}

#define SECTOR_LENGTH   2048
#define ISO9660_PVD_LBA   16

static void printDiscInfo(char *output) {
	output +=
		sprintf(output, "Disc information (from primary volume descriptor):\n");

	// To demonstrate data reading in a somewhat disc-agnostic way, we'll load
	// the ISO 9660 "primary volume descriptor" structure present at LBA 16 on
	// all PS1 games and most other data CDs. This structure spans one sector
	// and holds basic metadata about the contents of the disc.
	uint8_t pvd[SECTOR_LENGTH];

	// We only need the 2048-byte data area of the sector. We are no longer
	// beholden to CD-DA timings so we can increase the read speed from 1x to 2x
	// (150 sectors or 300 KB per second).
	uint8_t mode = 0
		| CDROM_MODE_SIZE_2048
		| CDROM_MODE_SPEED_2X;

	if (readCDROMSectorsBlocking_(pvd, ISO9660_PVD_LBA, 1, mode) == 1) {
		// Display a few fields from the descriptor, all fixed-length ASCII
		// strings except for the capacity. The details of parsing ISO 9660 are
		// out of this example's scope, so we'll blindly assume the descriptor
		// is valid and not perform any validation.
		uint32_t capacity = 0
			| (pvd[0x050] <<  0)
			| (pvd[0x051] <<  8)
			| (pvd[0x052] << 16)
			| (pvd[0x053] << 24);

		output += sprintf(
			output,
			"  Name:\t%.32s\n"
			"  System:\t%.32s\n"
			"  Date:\t%.4s-%.2s-%.2s %.2s:%.2s:%.2s\n"
			"  Capacity:\t%u sectors (%u MB)\n",
			&pvd[0x028], // Volume identifier
			&pvd[0x008], // System identifier
			&pvd[0x32d], // Creation date, year
			&pvd[0x331], // Creation date, month
			&pvd[0x333], // Creation date, day
			&pvd[0x335], // Creation date, hour
			&pvd[0x337], // Creation date, minute
			&pvd[0x339], // Creation date, second
			capacity,
			capacity / (0x100000 / SECTOR_LENGTH)
		);
	} else {
		output += sprintf(output, "  Unknown (data read failed)\n");
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
	initSIO0();
	initCDROM_();

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

	// Perform an initial read of the currently inserted CD.
	char info[256];

	printDiscInfo(info);

	uint16_t lastButtons = 0;

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

		// Read the primary volume descriptor again when any button is pressed
		// in order to allow for disc swapping.
		uint16_t buttons = pollController(0) | pollController(1);
		uint16_t pressed = ~lastButtons & buttons;
		lastButtons      = buttons;

		if (pressed)
			printDiscInfo(info);

		// Show information from the last read.
		printString(chain, &font, 16, 32, info);
		printString(
			chain,
			&font,
			16,
			96,
			"Insert a data disc and press any button to update."
		);

		*(chain->nextPacket) = gp0_endTag(0);

		waitForGP0Ready();
		waitForVSync();
		sendGPULinkedList(chain->data);
	}

	return 0;
}
