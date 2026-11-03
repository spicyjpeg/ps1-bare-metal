/*
 * ps1-bare-metal - Copyright (C) 2023-2026 spicyjpeg
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stddef.h>
#include <stdint.h>
#include "ps1/ctpdef.h"
#include "ps1/registers.h"

#ifdef __cplusplus
extern "C" {
#endif

static inline void selectSIO0Port(int port) {
	uint16_t cr = port ? SIO_CR_PORT_2 : SIO_CR_PORT_1;
	SIO_CR(0)   = (SIO_CR(0) & ~SIO_CR_PORT_BITMASK) | cr;
}

void initSIO0(void);
size_t exchangeCTPPacket(
	CTPDeviceAddress address,
	const uint8_t    *request,
	uint8_t          *response,
	size_t           reqLength,
	size_t           maxRespLength
);

uint16_t pollController(int port);

#ifdef __cplusplus
}
#endif
