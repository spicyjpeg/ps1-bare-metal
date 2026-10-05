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
 * This example implements a simple controller tester, showing how to send
 * commands to and obtain data from connected controllers.
 *
 * The PS1 has its controllers and memory cards interfaced over a serial bus.
 * Both communicate using a simple packet-based protocol, listening for request
 * packets sent by the console and replying with appropriate responses. Each
 * packet consists of a device address, a command and a series of parameters,
 * while responses will typically contain information about the controller and
 * the current state of its buttons in addition to any command-specific payload.
 *
 * Packets are sent and received through the SIO0 serial interface, similar to
 * the SIO1 interface we saw when setting up logging. All four controller and
 * memory card ports share the same bus, so a combination of per-port signals
 * and packet addressing is used to direct each packet at a specific device. The
 * bus is synchronous and full-duplex, meaning bytes are always sent and
 * received simultaneously - devices can only send us responses while we send
 * them packets. This may sound strange but is in fact a typical way to set up
 * communications between electronics: the PS1's controller bus is in fact a
 * variant of the common SPI protocol.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include "common/font.h"
#include "common/gpu.h"
#include "ps1/gpucmd.h"
#include "ps1/registers.h"

static void delayMicroseconds(int time) {
	// Calculate the approximate number of CPU cycles that need to be burned in
	// a dummy loop assuming a 33.8688 MHz clock:
	//     1 us == 33.8688 cycles ~= 33.875 cycles == (271 / 8) cycles
	// The loop is written in assembly to guarantee correct timing: it consists
	// of a branch to itself and a decrement in its delay slot, so each
	// iteration will burn exactly 2 cycles.
	time = (time * 271 + 4) / 8;

	__asm__ volatile(
		// The .set noreorder directive will prevent the assembler from trying
		// to "hide" the branch instruction's delay slot by shuffling nearby
		// instructions. .set push and .set pop are used to save and restore the
		// assembler's settings respectively, ensuring the noreorder flag will
		// not affect any other code.
		".set push\n"
		".set noreorder\n"
		"bgtz  %0, .\n"
		"addiu %0, -2\n"
		".set pop\n"
		: "+r"(time)
	);
}

static void initControllerIO(void) {
	// Reset SIO0, initialize it with the standard settings used for controller
	// and memory card communication (250000bps, 8 data bits) and configure it
	// to send a signal to the interrupt controller whenever the DSR input is
	// asserted (see waitForAcknowledge()).
	SIO_CR(0) = SIO_CR_INTRST;

	SIO_MR(0) = 0
		| SIO_MR_BR_DIV1
		| SIO_MR_CHLEN_8;
	SIO_BR(0) = F_CPU / 250000;
	SIO_CR(0) = 0
		| SIO_CR_TXEN
		| SIO_CR_RXEN
		| SIO_CR_DSRIEN;

	// As with vsync, prevent DSR pulses from triggering any interrupt handling
	// code in the BIOS kernel or otherwise outside of our control.
	IRQ_MASK &= ~(1 << IRQ_SIO0);
}

static inline void selectControllerPort(int port) {
	// Set or clear the bit that controls which set of controller and memory
	// card ports is going to have its DTR (chip select) signal asserted.
	// Devices will ignore any commands received while the chip select is not
	// active on the port they are plugged into.
	uint16_t cr = port ? SIO_CR_PORT_2 : SIO_CR_PORT_1;
	SIO_CR(0)   = (SIO_CR(0) & ~SIO_CR_PORT_BITMASK) | cr;
}

static uint8_t exchangeByte(uint8_t value) {
	// Start sending our byte once SIO0 reports to be ready.
	while (!(SIO_SR(0) & SIO_SR_TXRDY))
		__asm__ volatile("");

	SIO_DR(0) = value;

	// Wait for SIO0 to process the byte it received while sending ours. This
	// will typically be a reply to the *last* byte we sent (i.e. responses "lag
	// behind" commands by one byte) since the device starts replying before
	// receiving all bits of the current byte.
	while (!(SIO_SR(0) & SIO_SR_RXRDY))
		__asm__ volatile("");

	return SIO_DR(0);
}

#define DTR_DELAY    30
#define DSR_TIMEOUT 100

static bool waitForAcknowledge(void) {
	// Devices will acknowledge received bytes and request more data by briefly
	// pulsing the DSR line, which will cause SIO0 to set its respective
	// interrupt flag in the IRQ controller (and leave it set until we clear
	// it). This will obviously not happen if there is no device or no more data
	// to exchange, so we'll impose a ~100 us timeout on receiving the pulse.
	for (int timeout = DSR_TIMEOUT; timeout > 0; timeout -= 5) {
		if (IRQ_STAT & (1 << IRQ_SIO0)) {
			// Wait for the pulse to end if it hasn't yet, in order to prevent
			// SIO0 from triggering another IRQ for the same pulse after we
			// clear the current one.
			while (SIO_SR(0) & SIO_SR_DSR)
				__asm__ volatile("");

			// Reset the interrupt flag at both sides (IRQ controller and SIO0)
			// so it can be triggered again.
			IRQ_STAT   = ~(1 << IRQ_SIO0);
			SIO_CR(0) |= SIO_CR_ERRRST;

			return true;
		}

		delayMicroseconds(5);
	}

	return false;
}

// Each controller port shares its chip select with the respective memory card
// slot, so a secondary addressing mechanism is used to specify whether a packet
// is targeted at the controller or memory card. The first byte we need to send
// is thus the "address" of the device that shall handle it.
typedef enum {
	SIO0_ADDR_CONTROLLER  = 0x01,
	SIO0_ADDR_MEMORY_CARD = 0x81
} SIO0DeviceAddress;

static size_t exchangeSIO0Packet(
	SIO0DeviceAddress address,
	const uint8_t     *request,
	uint8_t           *response,
	size_t            reqLength,
	size_t            maxRespLength
) {
	// Drain any leftover bytes from SIO0's receive queue.
	while (SIO_SR(0) & SIO_SR_RXRDY)
		SIO_DR(0);

	// Clear any leftover interrupt flags and set DTR to mark the start of a new
	// packet. Devices may take some time to prepare for incoming commands, so a
	// small delay is needed afterwards.
	IRQ_STAT   = ~(1 << IRQ_SIO0);
	SIO_CR(0) |= SIO_CR_DTR | SIO_CR_ERRRST;
	delayMicroseconds(DTR_DELAY);

	// Send the address byte (and ignore any data received with it; devices
	// won't respond until they have received and validated the full address)
	// before the request payload.
	exchangeByte(address);

	size_t respLength = 0;

	for (; respLength < maxRespLength; respLength++) {
		// Wait for the device to request the next byte via an acknowledgement
		// pulse. The end of the packet is signalled by the lack of a pulse
		// after the last exchange.
		// NOTE: if no device is present we'll get no acknowledgement after the
		// address, so the function will simply return a zero-length response.
		if (!waitForAcknowledge())
			break;

		// If the response ends up being longer than the request payload, keep
		// sending dummy bytes to receive it.
		if (reqLength > 0) {
			*(response++) = exchangeByte(*(request++));
			reqLength--;
		} else {
			*(response++) = exchangeByte(0);
		}
	}

	// Release DSR to terminate the packet exchange and let the device go idle.
	SIO_CR(0) &= ~SIO_CR_DTR;

	return respLength;
}

// After the address, the first byte of the request payload holds the command
// code. The only command supported by all controller types is SIO0_PAD_POLL.
// More advanced controllers also have a "configuration mode" which grants
// access to rumble and other functionality, but we'll only use the basic poll
// command in this example.
typedef enum {
	// Controller commands
	SIO0_PAD_POLL        = 'B', // Read controller state
	SIO0_PAD_CONFIG_MODE = 'C', // Enter or exit configuration mode

	// Configuration mode commands
	SIO0_CFG_INIT_PRESSURE  = '@', // Initialize DualShock 2 pressure sensors
	SIO0_CFG_SET_ANALOG     = 'D', // Set analog mode/LED state
	SIO0_CFG_GET_ANALOG     = 'E', // Get analog mode/LED state
	SIO0_CFG_GET_ACT_INFO   = 'F', // Get information about a feedback actuator
	SIO0_CFG_GET_ACT_LIST   = 'G', // Get list of all feedback actuators
	SIO0_CFG_GET_ACT_STATE  = 'H', // Get current state of feedback actuators
	SIO0_CFG_GET_MODE       = 'L', // Get list of all supported modes
	SIO0_CFG_REQUEST_SETUP  = 'M', // Configure poll request format
	SIO0_CFG_RESPONSE_SETUP = 'O', // Configure poll response format

	// Memory card commands
	SIO0_CARD_READ     = 'R', // Read 128-byte sector
	SIO0_CARD_GET_SIZE = 'S', // Retrieve size information
	SIO0_CARD_WRITE    = 'W'  // Write 128-byte sector
} SIO0DeviceCommand;

static const char *const controllerTypes[] = {
	"unknown",            // ID 0x0
	"mouse",              // ID 0x1
	"neGcon",             // ID 0x2
	"Konami Justifier",   // ID 0x3
	"digital controller", // ID 0x4
	"analog stick",       // ID 0x5
	"Guncon",             // ID 0x6
	"analog controller",  // ID 0x7
	"multitap",           // ID 0x8
	"keyboard",           // ID 0x9
	"unknown",            // ID 0xa
	"unknown",            // ID 0xb
	"unknown",            // ID 0xc
	"unknown",            // ID 0xd
	"Jogcon",             // ID 0xe
	"configuration mode"  // ID 0xf
};

static const char *const buttonNames[] = {
	"Select",   // Bit  0
	"L3",       // Bit  1
	"R3",       // Bit  2
	"Start",    // Bit  3
	"Up",       // Bit  4
	"Right",    // Bit  5
	"Down",     // Bit  6
	"Left",     // Bit  7
	"L2",       // Bit  8
	"R2",       // Bit  9
	"L1",       // Bit 10
	"R1",       // Bit 11
	"Triangle", // Bit 12
	"Circle",   // Bit 13
	"X",        // Bit 14
	"Square"    // Bit 15
};

static void printControllerInfo(int port, char *output) {
	char *ptr = output;
	ptr      += sprintf(ptr, "Port %d:\n", port + 1);

	// Build the request data for the poll command.
	uint8_t request[4], response[8];

	request[0] = SIO0_PAD_POLL; // Command
	request[1] = 0;             // Multitap port number (0 for no multitap)
	request[2] = 0;             // Rumble/actuator 1 control
	request[3] = 0;             // Rumble/actuator 2 control

	// Send the packet to the specified controller port. Note that this is a
	// relatively slow process and should be done only once per frame, unless
	// higher polling rates are desired.
	selectControllerPort(port);
	size_t respLength = exchangeSIO0Packet(
		SIO0_ADDR_CONTROLLER,
		request,
		response,
		sizeof(request),
		sizeof(response)
	);

	if (respLength < 4) {
		// All controllers reply to the poll command with at least 4 bytes.
		ptr += sprintf(ptr, "  No controller connected");
		return;
	}

	// Byte 0 of the response contains a 4-bit controller type ID in the upper
	// nibble, plus the response payload's length in 2-byte units in the lower
	// nibble (ignored here).
	ptr += sprintf(
		ptr,
		"  Controller type:\t%s\n"
		"  Buttons pressed:\t",
		controllerTypes[response[0] >> 4]
	);

	// Bytes 2 and 3 hold a bitfield representing the state all buttons. The
	// bits are active-low (zero means the button is pressed), so we'll invert
	// the entire field prior to checking which bits are set.
	uint16_t buttons = (response[2] | (response[3] << 8)) ^ 0xffff;

	for (int i = 0; i < 16; i++) {
		if ((buttons >> i) & 1)
			ptr += sprintf(ptr, "%s ", buttonNames[i]);
	}

	// Controllers with analog inputs such as the DualShock or PS1 mouse return
	// an extra byte for each axis. We'll display the raw response payload to
	// demonstrate that.
	ptr += sprintf(ptr, "\n  Response data:\t");

	for (size_t i = 0; i < respLength; i++)
		ptr += sprintf(ptr, "%02X ", response[i]);
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
	initControllerIO();

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

		// Poll both controller ports once per frame.
		for (int i = 0; i < 2; i++) {
			char info[256];

			printControllerInfo(i, info);
			printString(chain, &font, 16, 32 + 64 * i, info);
		}

		*(chain->nextPacket) = gp0_endTag(0);

		waitForGP0Ready();
		waitForVSync();
		sendGPULinkedList(chain->data);
	}

	return 0;
}
