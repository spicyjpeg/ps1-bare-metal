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

#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

typedef enum {
	ISO_NO_ERROR      = 0,
	ISO_READ_ERROR    = 1,
	ISO_ALLOC_FAILED  = 2,
	ISO_NO_FILESYSTEM = 3,
	ISO_NOT_FOUND     = 4,
	ISO_NOT_A_FILE    = 5,
	ISO_NOT_A_FOLDER  = 6
} ISOError;

typedef struct {
	void   *ptr;
	size_t length;
} ISOData;

typedef struct {
	ISOData  root;
	uint32_t capacity;
	char     volumeName[32];
} ISOFilesystem;

typedef struct {
	uint32_t lba;
	size_t   length;
	uint8_t  flags;
} ISORecordInfo;

typedef enum {
	ISO_RECORD_HIDDEN       = 1 << 0,
	ISO_RECORD_DIRECTORY    = 1 << 1,
	ISO_RECORD_ASSOCIATED   = 1 << 2,
	ISO_RECORD_EXTENDED     = 1 << 3,
	ISO_RECORD_PROTECTION   = 1 << 4,
	ISO_RECORD_MULTI_EXTENT = 1 << 7
} ISORecordFlag;

#ifdef __cplusplus
extern "C" {
#endif

static inline void destroyISOData(ISOData *handle) {
	if (handle->ptr) {
		free(handle->ptr);

		handle->ptr    = 0;
		handle->length = 0;
	}
}

ISOError readISOData(ISOData *handle, unsigned int lba, size_t length);

ISOError initISOFilesystem  (ISOFilesystem *fs);
void     destroyISOFileystem(ISOFilesystem *fs);

ISOError getISORecordInfo(
	const ISOFilesystem *fs,
	ISORecordInfo       *record,
	const char          *path
);
ISOError loadISOFile(
	const ISOFilesystem *fs,
	ISOData             *handle,
	const char          *path
);

#ifdef __cplusplus
}
#endif
