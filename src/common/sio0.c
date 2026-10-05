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

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "common/sio0.h"
#include "ps1/ctpdef.h"
#include "ps1/delay.h"
#include "ps1/registers.h"

#define BAUD_RATE   250000
#define DTR_DELAY       30
#define DSR_TIMEOUT    100

void initSIO0(void) {
	SIO_CR(0) = SIO_CR_INTRST;

	SIO_MR(0) = 0
		| SIO_MR_BR_DIV1
		| SIO_MR_CHLEN_8;
	SIO_BR(0) = F_CPU / BAUD_RATE;
	SIO_CR(0) = 0
		| SIO_CR_TXEN
		| SIO_CR_RXEN
		| SIO_CR_DSRIEN;

	IRQ_MASK &= ~(1 << IRQ_SIO0);
}

static uint8_t exchangeByte(uint8_t value) {
	while (!(SIO_SR(0) & SIO_SR_TXRDY))
		__asm__ volatile("");

	SIO_DR(0) = value;

	while (!(SIO_SR(0) & SIO_SR_RXRDY))
		__asm__ volatile("");

	return SIO_DR(0);
}

static bool waitForAcknowledge(void) {
	for (int timeout = DSR_TIMEOUT; timeout > 0; timeout -= 5) {
		if (IRQ_STAT & (1 << IRQ_SIO0)) {
			while (SIO_SR(0) & SIO_SR_DSR)
				__asm__ volatile("");

			IRQ_STAT   = ~(1 << IRQ_SIO0);
			SIO_CR(0) |= SIO_CR_ERRRST;

			return true;
		}

		delayMicroseconds(5);
	}

	return false;
}

size_t exchangeCTPPacket(
	CTPDeviceAddress address,
	const uint8_t    *request,
	uint8_t          *response,
	size_t           reqLength,
	size_t           maxRespLength
) {
	while (SIO_SR(0) & SIO_SR_RXRDY)
		SIO_DR(0);

	IRQ_STAT   = ~(1 << IRQ_SIO0);
	SIO_CR(0) |= SIO_CR_DTR | SIO_CR_ERRRST;
	delayMicroseconds(DTR_DELAY);

	exchangeByte(address);

	size_t respLength = 0;

	for (; respLength < maxRespLength; respLength++) {
		if (!waitForAcknowledge())
			break;

		if (reqLength > 0) {
			*(response++) = exchangeByte(*(request++));
			reqLength--;
		} else {
			*(response++) = exchangeByte(0);
		}
	}

	SIO_CR(0) &= ~SIO_CR_DTR;

	return respLength;
}

uint16_t pollController(int port) {
	uint8_t request[4], response[8];

	request[0] = CTP_PAD_READ_DATA; // Command
	request[1] = CTP_TAP_NONE;      // Multitap port
	request[2] = 0;                 // Rumble/actuator 1 control
	request[3] = 0;                 // Rumble/actuator 2 control

	selectSIO0Port(port);
	size_t respLength = exchangeCTPPacket(
		CTP_ADDR_CONTROLLER,
		request,
		response,
		sizeof(request),
		sizeof(response)
	);

	if (respLength < 4)
		return 0;

	uint16_t buttons = (response[2] | (response[3] << 8)) ^ 0xffff;

	switch (response[0] >> 4) {
		case CTP_TYPE_DIGITAL:
		case CTP_TYPE_ANALOG_STICK:
		case CTP_TYPE_ANALOG:
		case CTP_TYPE_JOGCON:
			return buttons;

		case CTP_TYPE_NEGCON:
			// Remap I, II and L (which are returned as analog axes only) to
			// cross, square and L1 respectively. The other buttons are already
			// returned by the neGcon in the same bit order as a standard
			// controller.
			if (respLength >= 8) {
				if (response[5] >= 128)
					buttons |= BTN_PAD_CROSS;
				if (response[6] >= 128)
					buttons |= BTN_PAD_SQUARE;
				if (response[7] >= 128)
					buttons |= BTN_PAD_L1;
			}

			return buttons;

		default:
			// Ignore controllers with non-standard button layouts.
			return 0;
	}
}
