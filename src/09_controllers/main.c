/*
 * ps1-bare-metal - (C) 2023 spicyjpeg
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
 *
 *
 * This example implements a simple controller tester, showing how to send
 * commands to and obtain data from connected controllers.
 *
 * Compared to other consoles where the state of all inputs is sometimes easily
 * accessible through registers, the PS1 has its controllers and memory cards
 * interfaced via a serial bus (similar to SPI). Both communicate using a simple
 * packet-based protocol, listening for request packets sent by the console and
 * replying with appropriate responses. Each packet consists of an address, a
 * command and a series of parameters, while responses will typically contain
 * information about the controller and the current state of its buttons in
 * addition to any data returned by the command.
 *
 * Communication is done over the SIO0 serial interface, similar to the SIO1
 * interface we used in the hello world example. All ports share the same bus,
 * so an addressing system is used to specify which device shall respond to each
 * packet. Understanding this example may require some basic familiarity with
 * serial ports and their usage, although I tried to add as many comments as I
 * could to explain what is going on under the hood.
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include "font.h"
#include "gpu.h"
#include "ps1/gpucmd.h"
#include "ps1/registers.h"

static void delayMicroseconds(int time) {
	// Calculate the approximate number of CPU cycles that need to be burned,
	// assuming a 33.8688 MHz clock (1 us = 33.8688 = ~33.875 cycles). The loop
	// consists of a branch and a decrement, thus each iteration will burn 2 CPU
	// cycles.
	time = ((time * 271) + 4) / 8;

	__asm__ volatile(
		".set noreorder\n"
		"bgtz  %0, .\n"
		"addiu %0, -2\n"
		".set reorder\n"
		: "+r"(time)
	);
}

static void initControllerBus(void) {
	// Reset the serial interface, initialize it with the settings used by
	// controllers and memory cards (250000bps, 8 data bits) and configure it to
	// send a signal to the interrupt controller whenever the DSR input is
	// pulsed (see below).
	SIO_CTRL(0) = SIO_CTRL_RESET;

	SIO_MODE(0) = SIO_MODE_BAUD_DIV1 | SIO_MODE_DATA_8;
	SIO_BAUD(0) = F_CPU / 250000;
	SIO_CTRL(0) =
		SIO_CTRL_TX_ENABLE | SIO_CTRL_RX_ENABLE | SIO_CTRL_DSR_IRQ_ENABLE;
}

static bool waitForAcknowledge(int timeout) {
	// Controllers and memory cards will acknowledge bytes received by sending
	// short pulses over the DSR line, which will be forwarded by the serial
	// interface to the interrupt controller. This is not guaranteed to happen
	// (it will not if e.g. no device is connected), so we have to implement a
	// timeout to avoid waiting forever in such cases.
	for (; timeout > 0; timeout -= 10) {
		if (IRQ_STAT & (1 << IRQ_SIO0)) {
			// Reset the interrupt controller and serial interface's flags to
			// ensure the interrupt can be triggered again.
			IRQ_STAT     = ~(1 << IRQ_SIO0);
			SIO_CTRL(0) |= SIO_CTRL_ACKNOWLEDGE;

			return true;
		}

		delayMicroseconds(10);
	}

	return false;
}

// As the controller bus is shared with memory cards, an addressing mechanism is
// used to ensure packets are processed by a single device at a time. The first
// byte of each request packet is thus the "address" of the peripheral that
// shall respond to it.
typedef enum {
	ADDR_CONTROLLER  = 0x01,
	ADDR_MEMORY_CARD = 0x81
} DeviceAddress;

// The address is followed by a command byte and any required parameters. The
// only command used in this example (and supported by all controllers) is
// CMD_POLL, however some controllers additionally support a "configuration
// mode" which grants access to an extended command set.
typedef enum {
	CMD_INIT_PRESSURE   = '@', // Initialize DualShock pressure sensors (config)
	CMD_POLL            = 'B', // Read controller state
	CMD_CONFIG_MODE     = 'C', // Enter or exit configuration mode
	CMD_SET_ANALOG      = 'D', // Set analog mode/LED state (config)
	CMD_GET_ANALOG      = 'E', // Get analog mode/LED state (config)
	CMD_GET_MOTOR_INFO  = 'F', // Get information about a motor (config)
	CMD_GET_MOTOR_LIST  = 'G', // Get list of all motors (config)
	CMD_GET_MOTOR_STATE = 'H', // Get current state of vibration motors (config)
	CMD_GET_MODE        = 'L', // Get list of all supported modes (config)
	CMD_REQUEST_CONFIG  = 'M', // Configure poll request format (config)
	CMD_RESPONSE_CONFIG = 'O', // Configure poll response format (config)
	CMD_CARD_READ       = 'R', // Read 128-byte memory card sector
	CMD_CARD_IDENTIFY   = 'S', // Retrieve memory card size information
	CMD_CARD_WRITE      = 'W'  // Write 128-byte memory card sector
} DeviceCommand;

#define DTR_DELAY   60
#define DSR_TIMEOUT 120

static void selectPort(int port) {
	// Set or clear the bit that controls which set of controller and memory
	// card ports is going to have its DTR (port select) signal asserted. The
	// actual serial bus is shared between all ports, however devices will not
	// process packets if DTR is not asserted on the port they are plugged into.
	if (port)
		SIO_CTRL(0) |= SIO_CTRL_CS_PORT_2;
	else
		SIO_CTRL(0) &= ~SIO_CTRL_CS_PORT_2;
}

static uint8_t exchangeByte(uint8_t value) {
	// Wait until the interface is ready to accept a byte to send, then wait for
	// it to finish receiving the byte sent by the device.
	while (!(SIO_STAT(0) & SIO_STAT_TX_NOT_FULL))
		__asm__ volatile("");

	SIO_DATA(0) = value;

	while (!(SIO_STAT(0) & SIO_STAT_RX_NOT_EMPTY))
		__asm__ volatile("");

	return SIO_DATA(0);
}

static int exchangePacket(
	DeviceAddress address, const uint8_t *request, uint8_t *response,
	int reqLength, int maxRespLength
) {
	// Reset the interrupt flag and assert the DTR signal to tell the controller
	// or memory card that we're about to send a packet. Devices may take some
	// time to prepare for incoming bytes so we need a small delay here.
	IRQ_STAT     = ~(1 << IRQ_SIO0);
	SIO_CTRL(0) |= SIO_CTRL_DTR | SIO_CTRL_ACKNOWLEDGE;
	delayMicroseconds(DTR_DELAY);

	int respLength = 0;

	// Send the address byte and wait for the device to respond with a pulse on
	// the DSR line. If no response is received assume no device is connected,
	// otherwise make sure the serial interface's data buffer is empty to
	// prepare for the actual packet transfer.
	SIO_DATA(0) = address;

	if (waitForAcknowledge(DSR_TIMEOUT)) {
		while (SIO_STAT(0) & SIO_STAT_RX_NOT_EMPTY)
			SIO_DATA(0);

		// Send and receive the packet simultaneously one byte at a time,
		// padding it with zeroes if the packet we are receiving is longer than
		// the data being sent.
		while (respLength < maxRespLength) {
			if (reqLength > 0) {
				*(response++) = exchangeByte(*(request++));
				reqLength--;
			} else {
				*(response++) = exchangeByte(0);
			}

			respLength++;

			// The device will keep sending DSR pulses as long as there is more
			// data to transfer. If no more pulses are received, terminate the
			// transfer.
			if (!waitForAcknowledge(DSR_TIMEOUT))
				break;
		}
	}

	// Release DSR, allowing the device to go idle.
	delayMicroseconds(DTR_DELAY);
	SIO_CTRL(0) &= ~SIO_CTRL_DTR;

	return respLength;
}

// All packets sent by controllers in response to a poll command include a 4-bit
// device type identifier as well as a bitfield describing the state of up to 16
// buttons.
static const char *const controllerTypes[] = {
	"Unknown",            // ID 0x0
	"Mouse",              // ID 0x1
	"neGcon",             // ID 0x2
	"Konami Justifier",   // ID 0x3
	"Digital controller", // ID 0x4
	"Analog stick",       // ID 0x5
	"Guncon",             // ID 0x6
	"Analog controller",  // ID 0x7
	"Multitap",           // ID 0x8
	"Keyboard",           // ID 0x9
	"Unknown",            // ID 0xa
	"Unknown",            // ID 0xb
	"Unknown",            // ID 0xc
	"Unknown",            // ID 0xd
	"Jogcon",             // ID 0xe
	"Configuration mode"  // ID 0xf
};

static const char *const buttonNames[] = {
	"Select",   // Bit 0
	"L3",       // Bit 1
	"R3",       // Bit 2
	"Start",    // Bit 3
	"Up",       // Bit 4
	"Right",    // Bit 5
	"Down",     // Bit 6
	"Left",     // Bit 7
	"L2",       // Bit 8
	"R2",       // Bit 9
	"L1",       // Bit 10
	"R1",       // Bit 11
	"Triangle", // Bit 12
	"Circle",   // Bit 13
	"X",        // Bit 14
	"Square"    // Bit 15
};

static void printControllerInfo(int port, char *output) {
	// Build the request packet.
	uint8_t request[4], response[8];
	char    *ptr = output;

	request[0] = CMD_POLL; // Command
	request[1] = 0x00;     // Multitap address
	request[2] = 0x00;     // Rumble motor control 1
	request[3] = 0x00;     // Rumble motor control 2

	// Send the request to the specified controller port and grab the response.
	// Note that this is a relatively slow process and should be done only once
	// per frame, unless higher polling rates are desired.
	selectPort(port);
	int respLength = exchangePacket(
		ADDR_CONTROLLER, request, response, sizeof(request), sizeof(response)
	);

	ptr += sprintf(ptr, "Port %d:\n", port + 1);

	if (respLength < 4) {
		// All controllers reply with at least 4 bytes of data.
		ptr += sprintf(ptr, "  No controller connected");
		return;
	}

	// The first byte of the response contains the device type ID in the upper
	// nibble, as well as the length of the packet's payload in 2-byte units in
	// the lower nibble.
	ptr += sprintf(
		ptr,
		"  Controller type:\t%s\n"
		"  Buttons pressed:\t",
		controllerTypes[response[0] >> 4]
	);

	// Bytes 2 and 3 hold a bitfield representing the state all buttons. As each
	// bit is active low (i.e. a zero represents a button being pressed), the
	// entire field must be inverted.
	uint16_t buttons = (response[2] | (response[3] << 8)) ^ 0xffff;

	for (int i = 0; i < 16; i++) {
		if ((buttons >> i) & 1)
			ptr += sprintf(ptr, "%s ", buttonNames[i]);
	}

	ptr += sprintf(ptr, "\n  Response data:\t");

	for (int i = 0; i < respLength; i++)
		ptr += sprintf(ptr, "%02X ", response[i]);
}

#define SCREEN_WIDTH     320
#define SCREEN_HEIGHT    240
#define FONT_WIDTH       96
#define FONT_HEIGHT      56
#define FONT_COLOR_DEPTH GP0_COLOR_4BPP

extern const uint8_t fontTexture[], fontPalette[];

int main(int argc, const char **argv) {
	initSerialIO(115200);
	initControllerBus();

	if ((GPU_GP1 & GP1_STAT_MODE_BITMASK) == GP1_STAT_MODE_PAL) {
		puts("Using PAL mode");
		setupGPU(GP1_MODE_PAL, SCREEN_WIDTH, SCREEN_HEIGHT);
	} else {
		puts("Using NTSC mode");
		setupGPU(GP1_MODE_NTSC, SCREEN_WIDTH, SCREEN_HEIGHT);
	}

	DMA_DPCR |= DMA_DPCR_ENABLE << (DMA_GPU * 4);

	GPU_GP1 = gp1_dmaRequestMode(GP1_DREQ_GP0_WRITE);
	GPU_GP1 = gp1_dispBlank(false);

	TextureInfo font;

	uploadIndexedTexture(
		&font, fontTexture, fontPalette, SCREEN_WIDTH * 2, 0, SCREEN_WIDTH * 2,
		FONT_HEIGHT, FONT_WIDTH, FONT_HEIGHT, FONT_COLOR_DEPTH
	);

	DMAChain dmaChains[2];
	bool     usingSecondFrame = false;

	for (;;) {
		int bufferX = usingSecondFrame ? SCREEN_WIDTH : 0;
		int bufferY = 0;

		DMAChain *chain  = &dmaChains[usingSecondFrame];
		usingSecondFrame = !usingSecondFrame;

		uint32_t *ptr;

		GPU_GP1 = gp1_fbOffset(bufferX, bufferY);

		chain->nextPacket = chain->data;

		ptr    = allocatePacket(chain, 4);
		ptr[0] = gp0_texpage(0, true, false);
		ptr[1] = gp0_fbOffset1(bufferX, bufferY);
		ptr[2] = gp0_fbOffset2(
			bufferX + SCREEN_WIDTH - 1, bufferY + SCREEN_HEIGHT - 2
		);
		ptr[3] = gp0_fbOrigin(bufferX, bufferY);

		ptr    = allocatePacket(chain, 3);
		ptr[0] = gp0_rgb(64, 64, 64) | gp0_vramFill();
		ptr[1] = gp0_xy(bufferX, bufferY);
		ptr[2] = gp0_xy(SCREEN_WIDTH, SCREEN_HEIGHT);

		// Poll both controller ports once per frame. Memory cards are ignored
		// in this example.
		for (int i = 0; i < 2; i++) {
			char buffer[256];

			printControllerInfo(i, buffer);
			printString(chain, &font, 16, 32 + 64 * i, buffer);
		}

		*(chain->nextPacket) = gp0_endTag(0);

		waitForGP0Ready();
		waitForVSync();
		sendLinkedList(chain->data);
	}

	return 0;
}
