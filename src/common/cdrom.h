/*
 * ps1-bare-metal - Copyright (C) 2023-2026 spicyjpeg
 * SPDX-License-Identifier: MIT
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
