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
 * Having taken a preliminary look at ISO 9660, we can now add the last piece we
 * need to load files from the CD-ROM: path traversal. We must be able to
 * resolve a path such as "/ASSETS/IMAGES/TITLE.DAT", splitting it into its
 * components and searching each component for the next until we find the record
 * that describes our target file. We'll rely on the heap handle system we
 * introduced in the last example to temporarily load, parse and deallocate each
 * component's directory descriptor, as well as to manage the root directory
 * descriptor (kept in memory to cut down on CD-ROM accesses, since all path
 * lookups go through it) and the files we load.
 *
 * Note that this is not the only way to navigate an ISO 9660 filesystem. The
 * primary volume descriptor links to a "path table" that maps absolute paths
 * directly to folders, removing the need for traversal entirely, but it only
 * really makes sense to use it on discs with deeply nested subfolders. We are
 * not going to cover parsing the path table.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include "common/cdrom.h"
#include "common/font.h"
#include "common/gpu.h"
#include "common/iso9660.h"
#include "common/sio0.h"
#include "ps1/ctpdef.h"
#include "ps1/gpucmd.h"
#include "ps1/registers.h"

#define SECTOR_LENGTH 2048

static inline uint16_t parseUint16(const uint8_t *data) {
	return data[0] | (data[1] << 8);
}
static inline uint32_t parseUint32(const uint8_t *data) {
	return data[0] | (data[1] << 8) | (data[2] << 16) | (data[3] << 24);
}

static const char *compareRecordName(
	const char *path,
	const char *recordName,
	size_t     nameLength
) {
	// Skip any leading separators in the given path, then check if the record
	// is a "." (current folder) or ".." (parent folder) entry and match it
	// against the respective strings.
	while ((*path == '/') || (*path == '\\'))
		path++;

	switch (*recordName) {
		case 0x00:
			if (*path != '.')
				return 0;

			path++;
			break;

		case 0x01:
			if ((path[0] != '.') || (path[1] != '.'))
				return 0;

			path += 2;
			break;

		default:
			for (; nameLength > 0; nameLength--) {
				// If not, compare the two names byte-by-byte. ISO 9660 file
				// names have a trailing version number (almost always ";1", or
				// ".;1" if the file has no extension) which we are going to
				// interpret as a terminator.
				if (*recordName == ';')
					break;
				if ((recordName[0] == '.') && (recordName[1] == ';'))
					break;

				// Perform case-insensitive comparison. The specification
				// requires all record names to be uppercase (in theory removing
				// the need for the second toupper() call), but some CD-ROM
				// authoring tools may violate this rule.
				if (__builtin_toupper(*path) != __builtin_toupper(*recordName))
					return 0;

				path++;
				recordName++;
			}
	}

	// Require either a null terminator or a path separator after the name. If
	// everything matches return a pointer to the next path component, which can
	// be passed to this function again to compare it against other records.
	if ((*path == '/') || (*path == '\\') || !(*path))
		return path;
	else
		return 0;
}

// We'll use this structure to return information about a record we found, so
// that it persists after we deallocate the directory the record is in.
typedef struct {
	uint32_t lba;
	size_t   length;
	uint8_t  flags;
} ISORecordInfo_;

static const char *findRecord(
	const ISOData  *directory,
	const char     *path,
	ISORecordInfo_ *output
) {
	const uint8_t *ptr = directory->ptr;
	const uint8_t *end = &ptr[directory->length];

	while (ptr < end) {
		// Iterate over all records in the directory, skipping any padding.
		if (!ptr[0x00]) {
			ptr++;
			continue;
		}

		uint8_t    recordLength = ptr[0x00];
		uint32_t   lba          = parseUint32(&ptr[0x02]);
		uint32_t   length       = parseUint32(&ptr[0x0a]);
		uint8_t    flags        = ptr[0x19];
		uint8_t    nameLength   = ptr[0x20];
		const char *recordName  = (const char *) &ptr[0x21];

		// If the record's name matches the first path component, return its
		// fields and a pointer to the remainder of the path.
		const char *nextPath = compareRecordName(path, recordName, nameLength);

		if (nextPath) {
			output->lba    = lba;
			output->length = length;
			output->flags  = flags;

			return nextPath;
		}

		ptr += recordLength;
	}

	return 0;
}

static ISOError getISORecordInfo_(
	const ISOFilesystem *fs,
	ISORecordInfo_      *record,
	const char          *path
) {
	// Search the cached root directory for the first component. If a match is
	// found and the path has no other components, we can return it right away
	// without blocking on a CD-ROM read.
	path = findRecord(&fs->root, path, record);

	if (!path)
		return ISO_NOT_FOUND;
	else if (!(*path))
		return ISO_NO_ERROR;
	else if (!(record->flags & ISO_RECORD_DIRECTORY))
		return ISO_NOT_A_FOLDER;

	// If the path has trailing components (and the first component matched a
	// folder) we need to traverse subfolders until that is no longer the case,
	// taking care to destroy each descriptor we read after parsing it.
	ISOData directory = {};

	for (;;) {
		ISOError error = readISOData(&directory, record->lba, record->length);

		if (error)
			return error;

		path = findRecord(&directory, path, record);
		destroyISOData(&directory);

		if (!path)
			return ISO_NOT_FOUND;
		else if (!(*path))
			return ISO_NO_ERROR;
		else if (!(record->flags & ISO_RECORD_DIRECTORY))
			return ISO_NOT_A_FOLDER;
	}
}

// For our basic use case of loading a file in its entirety on the heap, we can
// streamline the process of locating it, allocating a buffer and performing the
// actual read into one function call.
static ISOError loadISOFile_(
	const ISOFilesystem *fs,
	ISOData             *handle,
	const char          *path
) {
	ISORecordInfo_ record;
	ISOError       error = getISORecordInfo_(fs, &record, path);

	if (error)
		return error;
	else if (record.flags & ISO_RECORD_DIRECTORY)
		return ISO_NOT_A_FILE;
	else
		return readISOData(handle, record.lba, record.length);
}

static void printTextFile(const char *path, char *output, size_t maxLength) {
	output += sprintf(output, "Contents of %s:\n", path);

	// As with the last example we will make the filesystem and data handle
	// objects short-lived to keep the code simple, but nothing would stop us
	// from keeping loaded files allocated across functions for as long as
	// needed.
	ISOFilesystem fs    = {};
	ISOError      error = initISOFilesystem(&fs);

	if (error) {
		output +=
			sprintf(output, "  Initialization failed (error code %d)\n", error);
		return;
	}

	ISOData file = {};
	error        = loadISOFile_(&fs, &file, path);

	if (!error) {
		// Limit the number of displayed characters to avoid overflowing the
		// text buffer or display list.
		if (file.length > maxLength)
			output += sprintf(
				output,
				"%.*s\n"
				"(more data not shown...)\n",
				maxLength,
				file.ptr
			);
		else
			output += sprintf(output, "%.*s\n", file.length, file.ptr);

		destroyISOData(&file);
	} else {
		output +=
			sprintf(output, "  File loading failed (error code %d)\n", error);
	}

	destroyISOFileystem(&fs);
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

	// Load and display the contents of SYSTEM.CNF from the root of the disc.
	// This is a text file present in almost all retail PS1 games and most
	// homebrew discs, used to configure the BIOS kernel and point it at an
	// executable to launch.
	char text[1024];

	printTextFile("/SYSTEM.CNF", text, 800);

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

		// Non-PS1 discs and some homebrew games lack SYSTEM.CNF, so we'll give
		// the option to load another text file commonly found on CD-ROMs.
		if (pressed & BTN_PAD_CROSS)
			printTextFile("/SYSTEM.CNF", text, 800);
		else if (pressed & BTN_PAD_CIRCLE)
			printTextFile("/README.TXT", text, 800);

		printString(
			chain,
			&font,
			16,
			32,
			"Controls:\n"
			"  Cross:\tload SYSTEM.CNF (if any)\n"
			"  Circle:\tload README.TXT (if any)"
		);
		printString(chain, &font, 16, 96, text);

		*(chain->nextPacket) = gp0_endTag(0);

		waitForGP0Ready();
		waitForVSync();
		sendGPULinkedList(chain->data);
	}

	return 0;
}
