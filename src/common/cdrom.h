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

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "ps1/cdromdef.h"

#ifdef __cplusplus
extern "C" {
#endif

void initCDROM(void);
void setupCDROMAudio(bool mono);
void waitForCDROMDMADone(void);

void receiveCDROMSector(void *data, size_t length);

void issueCDROMCommand(
	CDROMCommand  command,
	const uint8_t *parameters,
	size_t        paramLength
);
CDROMIRQType checkCDROMIRQ  (uint8_t *response, size_t maxRespLength);
CDROMIRQType waitForCDROMIRQ(uint8_t *response, size_t maxRespLength);

bool playCDDA      (unsigned int track, uint8_t mode);
bool startCDROMRead(unsigned int lba,   uint8_t mode, bool retry);
bool pauseCDROMRead(void);

size_t readCDROMSectorsBlocking(
	void         *data,
	unsigned int lba,
	size_t       numSectors,
	uint8_t      mode
);

#ifdef __cplusplus
}
#endif
