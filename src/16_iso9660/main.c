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
 * We saw how to read spans of data sectors from the CD-ROM, but we still don't
 * know which sectors map to which files. A solution often used by PS1 games is
 * to hardcode a list of all assets, their starting LBAs and sizes in the
 * executable, but this approach would introduce a circular dependency between
 * our executable and the CD-ROM image it would be part of.
 *
 * A more flexible (if less efficient) alternative is to parse and interpret the
 * actual filesystem on the disc. PS1 CD-ROM data tracks are always formatted
 * with the ISO 9660 filesystem, which maps file paths to sector spans
 * hierarchically using a tree of "directory descriptor" structures. Each
 * descriptor lists the contents of a certain folder and is stored as if it were
 * any other file. At the root of this tree is the descriptor for the disc's
 * root folder, which we are going to parse and display in this example.
 *
 * The ISO 9660 specification is freely available under the name of ECMA-119. An
 * overview of the main data structures can also be found here:
 *     https://wiki.osdev.org/ISO_9660
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include "common/cdrom.h"
#include "common/font.h"
#include "common/gpu.h"
#include "common/sio0.h"
#include "ps1/cdromdef.h"
#include "ps1/gpucmd.h"
#include "ps1/registers.h"

#define SECTOR_LENGTH   2048
#define ISO9660_PVD_LBA   16

typedef enum {
	ISO_NO_ERROR      = 0,
	ISO_READ_ERROR    = 1,
	ISO_ALLOC_FAILED  = 2,
	ISO_NO_FILESYSTEM = 3,
	ISO_NOT_FOUND     = 4,
	ISO_NOT_A_FILE    = 5,
	ISO_NOT_A_FOLDER  = 6
} ISOError;

// As directory descriptors and file contents can span multiple sectors and be
// arbitrarily large, it makes sense to allocate buffers for them dynamically on
// the heap rather than trying to fit them in a fixed-size array. To make it
// easier to manage these buffers, we'll wrap their pointers in a "data handle"
// structure and define helper functions to allocate, populate and destroy them.
typedef struct {
	void   *ptr;
	size_t length;
} ISOData;

static ISOError readISOData(ISOData *handle, unsigned int lba, size_t length) {
	// Determine how many CD-ROM sectors the requested data spans and attempt to
	// allocate a buffer of the appropriate size.
	// NOTE: even if the size is not an exact multiple of 2048, we must still
	// round it up to the nearest one before allocation. Allocating the exact
	// length requested would result in readCDROMSectorsBlocking() overflowing
	// the buffer and corrupting memory when reading the last sector.
	size_t numSectors = (length + SECTOR_LENGTH - 1) / SECTOR_LENGTH;
	void   *ptr       = malloc(numSectors * SECTOR_LENGTH);

	if (!ptr)
		return ISO_ALLOC_FAILED;

	// Populate the buffer, or undo the allocation and return an error in case
	// of failure.
	uint8_t mode = 0
		| CDROM_MODE_SIZE_2048
		| CDROM_MODE_SPEED_2X;

	if (readCDROMSectorsBlocking(ptr, lba, numSectors, mode) != numSectors) {
		free(ptr);
		return ISO_READ_ERROR;
	}

	// If the handle was already used before, destroy its existing buffer before
	// saving the pointer to the new one. This prevents memory leaks when not
	// calling destroyISOData() before readISOData(), but will only work
	// properly if all handles are manually zeroed prior to first use.
	if (handle->ptr)
		free(handle->ptr);

	handle->ptr    = ptr;
	handle->length = length;

	return ISO_NO_ERROR;
}

static inline void destroyISOData(ISOData *handle) {
	// Deallocate the buffer and clear the pointer to prevent a double free if
	// this function is called again.
	if (handle->ptr) {
		free(handle->ptr);

		handle->ptr    = 0;
		handle->length = 0;
	}
}

// We are going to gather information about the filesystem once on startup and
// cache it in this structure for later use, avoiding redundant CD-ROM reads.
typedef struct {
	ISOData  root;
	uint32_t capacity;
	char     volumeName[32];
} ISOFilesystem;

// ISO 9660 stores some values in unaligned, unsigned 16- or 32-bit both-endian
// format (little-endian followed by a big-endian copy). We can define some
// helpers to parse them.
static inline uint16_t parseUint16(const uint8_t *data) {
	return data[0] | (data[1] << 8);
}
static inline uint32_t parseUint32(const uint8_t *data) {
	return data[0] | (data[1] << 8) | (data[2] << 16) | (data[3] << 24);
}

static ISOError initISOFilesystem(ISOFilesystem *fs) {
	// Fetch, validate and parse the "primary volume descriptor" structure at
	// LBA 16. This is the starting point for parsing the filesystem: we already
	// saw how it contains basic disc metadata, but it also holds the LBA and
	// size of the root directory descriptor.
	uint8_t pvd[SECTOR_LENGTH];
	uint8_t mode = 0
		| CDROM_MODE_SIZE_2048
		| CDROM_MODE_SPEED_2X;

	if (readCDROMSectorsBlocking(pvd, ISO9660_PVD_LBA, 1, mode) != 1)
		return ISO_READ_ERROR;

	const uint8_t *magic       = &pvd[0x000];
	const uint8_t *volumeID    = &pvd[0x028];
	uint32_t      capacity     = parseUint32(&pvd[0x050]);
	uint16_t      sectorLength = parseUint16(&pvd[0x080]);
	uint32_t      rootLBA      = parseUint32(&pvd[0x09c + 0x02]);
	uint32_t      rootLength   = parseUint32(&pvd[0x09c + 0x0a]);

	if (__builtin_memcmp(magic, "\x01" "CD001" "\x01", 7))
		return ISO_NO_FILESYSTEM;
	if (sectorLength != SECTOR_LENGTH)
		return ISO_NO_FILESYSTEM;

	// Allocate a buffer for and load the root directory descriptor, then fill
	// in the other fields of the filesystem object.
	ISOError error = readISOData(&fs->root, rootLBA, rootLength);

	if (error)
		return error;

	fs->capacity = capacity;
	__builtin_memcpy(fs->volumeName, volumeID, sizeof(fs->volumeName));

	return ISO_NO_ERROR;
}

static void destroyISOFileystem(ISOFilesystem *fs) {
	destroyISOData(&fs->root);

	fs->capacity      = 0;
	fs->volumeName[0] = 0;
}

typedef enum {
	ISO_RECORD_HIDDEN       = 1 << 0,
	ISO_RECORD_DIRECTORY    = 1 << 1,
	ISO_RECORD_ASSOCIATED   = 1 << 2,
	ISO_RECORD_EXTENDED     = 1 << 3,
	ISO_RECORD_PROTECTION   = 1 << 4,
	ISO_RECORD_MULTI_EXTENT = 1 << 7
} ISORecordFlag;

static void printDirectory(const ISOData *dir, char *output, size_t maxLines) {
	// Directory descriptors are made up of a series of variable-length records,
	// each describing a file or subfolder and containing its name, starting
	// LBA, size, flags and so on. We'll iterate over all records in the
	// directory, stopping earlier if we run out of lines on screen.
	const uint8_t *ptr = dir->ptr;
	const uint8_t *end = &ptr[dir->length];

	while ((ptr < end) && (maxLines > 0)) {
		// Skip any empty padding (inserted between records by authoring tools
		// for alignment purposes) and parse some fields from the next record.
		if (!ptr[0x00]) {
			ptr++;
			continue;
		}

		uint8_t    recordLength = ptr[0x00];
		uint32_t   length       = parseUint32(&ptr[0x0a]);
		uint8_t    flags        = ptr[0x19];
		uint8_t    nameLength   = ptr[0x20];
		const char *recordName  = (const char *) &ptr[0x21];

		// Show information about the record. Ignore "." (current folder) and
		// ".." (parent folder) entries, to which ISO 9660 assigns the non-ASCII
		// names "\x00" and "\x01" respectively.
		if ((*recordName != 0x00) && (*recordName != 0x01)) {
			const char *isHidden = (flags & ISO_RECORD_HIDDEN)
				? " (hidden)"
				: "";

			// Record names are length-prefixed rather than null-terminated, so
			// we must use the %.*s specifier to explicitly pass their lengths
			// to sprintf().
			if (flags & ISO_RECORD_DIRECTORY)
				output += sprintf(
					output,
					"  %.*s (folder)%s\n",
					nameLength,
					recordName,
					isHidden
				);
			else
				output += sprintf(
					output,
					"  %.*s (%d KB)%s\n",
					nameLength,
					recordName,
					length / 1024,
					isHidden
				);

			maxLines--;
		}

		// Move past the record we parsed.
		ptr += recordLength;
	}

	if (ptr < end)
		output += sprintf(output, "  (more entries not shown...)\n");
}

static void printDiscRoot(char *output, size_t maxLines) {
	output += sprintf(output, "CD-ROM root directory listing:\n");

	// In order to keep this example simple we'll destroy the parser immediately
	// after fetching the root directory. A real world application would instead
	// hold onto it and reuse it indefinitely (or until the drive lid is opened,
	// if you want to allow the user to swap discs).
	ISOFilesystem fs    = {};
	ISOError      error = initISOFilesystem(&fs);

	if (!error) {
		printDirectory(&fs.root, output, maxLines);
		destroyISOFileystem(&fs);
	} else {
		output +=
			sprintf(output, "  Initialization failed (error code %d)\n", error);
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

	char root[1024];

	printDiscRoot(root, 13);

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

		uint16_t buttons = pollController(0) | pollController(1);
		uint16_t pressed = ~lastButtons & buttons;
		lastButtons      = buttons;

		if (pressed)
			printDiscRoot(root, 13);

		printString(chain, &font, 16, 32, root);
		printString(
			chain,
			&font,
			16,
			192,
			"Insert a data disc and press any button to update."
		);

		*(chain->nextPacket) = gp0_endTag(0);

		waitForGP0Ready();
		waitForVSync();
		sendGPULinkedList(chain->data);
	}

	return 0;
}
