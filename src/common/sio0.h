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
