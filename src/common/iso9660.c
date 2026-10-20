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

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include "common/cdrom.h"
#include "common/iso9660.h"
#include "ps1/cdromdef.h"

#define DEF(type)     static inline type __attribute__((always_inline))
#define DIV(num, den) (((num) + (den) - 1) / (den))

#define SECTOR_LENGTH   2048
#define ISO9660_PVD_LBA   16

DEF(uint16_t) parseUint16(const uint8_t *data) {
	return data[0] | (data[1] << 8);
}
DEF(uint32_t) parseUint32(const uint8_t *data) {
	return data[0] | (data[1] << 8) | (data[2] << 16) | (data[3] << 24);
}

ISOError readISOData(ISOData *handle, unsigned int lba, size_t length) {
	size_t numSectors = DIV(length, SECTOR_LENGTH);
	void   *ptr       = malloc(numSectors * SECTOR_LENGTH);

	if (!ptr)
		return ISO_ALLOC_FAILED;

	uint8_t mode = 0
		| CDROM_MODE_SIZE_2048
		| CDROM_MODE_SPEED_2X;

	if (readCDROMSectorsBlocking(ptr, lba, numSectors, mode) != numSectors) {
		free(ptr);
		return ISO_READ_ERROR;
	}

	if (handle->ptr)
		free(handle->ptr);

	handle->ptr    = ptr;
	handle->length = length;

	return ISO_NO_ERROR;
}

ISOError initISOFilesystem(ISOFilesystem *fs) {
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

	ISOError error = readISOData(&fs->root, rootLBA, rootLength);

	if (error)
		return error;

	fs->capacity = capacity;
	__builtin_memcpy(fs->volumeName, volumeID, sizeof(fs->volumeName));

	return ISO_NO_ERROR;
}

void destroyISOFileystem(ISOFilesystem *fs) {
	destroyISOData(&fs->root);

	fs->capacity      = 0;
	fs->volumeName[0] = 0;
}

static const char *compareRecordName(
	const char *path,
	const char *recordName,
	size_t     nameLength
) {
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
				if (*recordName == ';')
					break;
				if ((recordName[0] == '.') && (recordName[1] == ';'))
					break;

				if (__builtin_toupper(*path) != __builtin_toupper(*recordName))
					return 0;

				path++;
				recordName++;
			}
	}

	if ((*path == '/') || (*path == '\\') || !(*path))
		return path;
	else
		return 0;
}

static const char *findRecord(
	const ISOData *directory,
	const char    *path,
	ISORecordInfo *output
) {
	const uint8_t *ptr = directory->ptr;
	const uint8_t *end = &ptr[directory->length];

	while (ptr < end) {
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

ISOError getISORecordInfo(
	const ISOFilesystem *fs,
	ISORecordInfo       *record,
	const char          *path
) {
	path = findRecord(&fs->root, path, record);

	if (!path)
		return ISO_NOT_FOUND;
	else if (!(*path))
		return ISO_NO_ERROR;
	else if (!(record->flags & ISO_RECORD_DIRECTORY))
		return ISO_NOT_A_FOLDER;

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

ISOError loadISOFile(
	const ISOFilesystem *fs,
	ISOData             *handle,
	const char          *path
) {
	ISORecordInfo record;
	ISOError      error = getISORecordInfo(fs, &record, path);

	if (error)
		return error;
	else if (record.flags & ISO_RECORD_DIRECTORY)
		return ISO_NOT_A_FILE;
	else
		return readISOData(handle, record.lba, record.length);
}

#undef DEF
#undef DIV
