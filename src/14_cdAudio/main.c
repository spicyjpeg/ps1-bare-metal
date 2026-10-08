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
 * The last example covered sending simple commands to the CD-ROM drive and
 * getting responses back. With that in place, we can move onto something more
 * practical: playing CD audio (CD-DA) tracks.
 *
 * As with other consoles and PCs of the era, the PS1's CD-ROM drive is capable
 * of playing CD-DA autonomously in the background with no CPU intervention
 * beyond the initial play command. This facility is used by the CD player built
 * into the BIOS as well as by some games, which stored their music as audio
 * tracks alongside the main data track on the disc. The CD-ROM decoder's audio
 * output is sent to a dedicated auxiliary input on the SPU for mixing into the
 * main output, without taking up any of the 24 BRR channels.
 *
 * This example requires either a music CD or a mixed data/audio disc (e.g. the
 * first Ridge Racer or Wipeout) in the console in order to run. It doesn't have
 * to be an original PS1 game; the drive does not enforce region checks for
 * CD-DA playback, so any pressed or burned disc will work even with no modchip
 * (provided that you have a way to launch the example in the first place). On
 * emulators the "swap disc" option can be used to load a disc image separately
 * from the executable.
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include "common/cdrom.h"
#include "common/font.h"
#include "common/gpu.h"
#include "common/sio0.h"
#include "common/spu.h"
#include "ps1/cdromdef.h"
#include "ps1/ctpdef.h"
#include "ps1/gpucmd.h"
#include "ps1/registers.h"

static void setupCDROMAudio_(bool mono) {
	// Initialize the CD-ROM sector decoder's attenuation matrix in banks 2 and
	// 3. The decoder runs all CD audio through a 2x2 matrix, which we can use
	// to pass the stereo channels through, swap them or downmix to mono (by
	// sending both input channels to both outputs at half volume). The volume
	// coefficients are in 1.7 fixed-point format.
	CDROM_ADDRESS = 2;
	CDROM_ATV0    = mono ? 64 : 128; // L -> L
	CDROM_ATV1    = mono ? 64 :   0; // L -> R
	CDROM_ADDRESS = 3;
	CDROM_ATV2    = mono ? 64 : 128; // R -> R
	CDROM_ATV3    = mono ? 64 :   0; // R -> L
	CDROM_ADPCTL  = CDROM_ADPCTL_CHNGATV;

	// The output from the matrix goes to the SPU's I2S input A. The SPU has its
	// own additional volume and mute controls that need to be set up.
	// NOTE: like reverb volume (and unlike master and channel volume), audio
	// input volume is in 16-bit signed format and has no volume slide flag.
	SPU_ATTR |= SPU_ATTR_I2SA_ENABLE;
	SPU_AVOLL = 0x7fff;
	SPU_AVOLR = 0x7fff;
}

static bool playCDDA_(unsigned int track, uint8_t mode) {
	// Drop any leftover IRQ and response from previous commands.
	checkCDROMIRQ(0, 0);

	// Set the mechacon's "mode" bitfield, which controls several settings such
	// as drive speed. One flag controls whether CD-DA as a whole is enabled, so
	// we'll at the very least set that.
	mode |= CDROM_MODE_CDDA;

	issueCDROMCommand(CDROM_CMD_SETMODE, &mode, sizeof(mode));
	if (waitForCDROMIRQ(0, 0) != CDROM_IRQ_ACKNOWLEDGE)
		return false;

	// Once the mode is set, send the actual play command. This command can be
	// used in three different ways:
	// - with no parameters, to resume playback from where it was last paused;
	// - with no parameters after a CDROM_CMD_SETLOC command, to start playing
	//   from a specific timestamp on the disc (not covered in this example);
	// - with a track number in binary-coded decimal format as its only
	//   parameter, to play from its beginning.
	// The drive starts playing in the background after the acknowledgement.
	// There is no complete IRQ, but the mechacon will fire a "data end" IRQ
	// once playback ends.
	uint8_t bcdTrack = cdrom_encodeBCD(track);

	if (track > 0)
		issueCDROMCommand(CDROM_CMD_PLAY, &bcdTrack, sizeof(bcdTrack));
	else
		issueCDROMCommand(CDROM_CMD_PLAY, 0, 0);
	if (waitForCDROMIRQ(0, 0) != CDROM_IRQ_ACKNOWLEDGE)
		return false;

	return true;
}

static bool pauseCDROMRead_(void) {
	checkCDROMIRQ(0, 0);

	// Send a pause command and wait for it to complete. Unlike CDROM_CMD_PLAY,
	// CDROM_CMD_PAUSE does generate a complete IRQ.
	issueCDROMCommand(CDROM_CMD_PAUSE, 0, 0);
	if (waitForCDROMIRQ(0, 0) != CDROM_IRQ_ACKNOWLEDGE)
		return false;
	if (waitForCDROMIRQ(0, 0) != CDROM_IRQ_COMPLETE)
		return false;

	return true;
}

// We are going to implement a simple menu to control the drive, with its
// internal state stored in this structure.
typedef struct {
	uint16_t lastButtons;
	uint8_t  selectedTrack;
} MenuState;

static void printMenu(const MenuState *state, char *output) {
	sprintf(
		output,
		"Controls:\n"
		"  Cross:\tplay track %u (left/right to change)\n"
		"  Circle:\tpause playback\n"
		"  Triangle:\tresume playback\n"
		"  Data tracks (e.g. track 1 on PS1 games) will be silent.",
		state->selectedTrack
	);
}

static void updateMenu(MenuState *state, uint16_t buttons) {
	// Determine which buttons were pressed (i.e. are held down now, but were
	// not during the last update). We'll do this for all 16 buttons at once
	// using bitwise math.
	uint16_t pressed   = ~state->lastButtons & buttons;
	state->lastButtons = buttons;

	// Handle track changes. Since track numbers are in binary-coded decimal
	// format, CDs are only allowed to have up to 99 tracks.
	if ((pressed & BTN_PAD_LEFT) && (state->selectedTrack > 1))
		state->selectedTrack--;
	else if ((pressed & BTN_PAD_RIGHT) && (state->selectedTrack < 99))
		state->selectedTrack++;

	// Handle the other controls. When starting playback, we'll set the drive
	// speed to 1x to match the CD-DA data rate and enable the "auto pause" mode
	// flag to stop at the end of the track, rather than the default behavior of
	// playing through to the end of the disc.
	uint8_t mode = 0
		| CDROM_MODE_AUTO_PAUSE
		| CDROM_MODE_SPEED_1X;

	if (pressed & BTN_PAD_CROSS)
		playCDDA_(state->selectedTrack, mode);
	else if (pressed & BTN_PAD_TRIANGLE)
		playCDDA_(0, mode);
	else if (pressed & BTN_PAD_CIRCLE)
		pauseCDROMRead_();
}

static void printCDROMStatus(char *output) {
	char *ptr = output;
	ptr      += sprintf(ptr, "Current CD-ROM status:\n");

	checkCDROMIRQ(0, 0);

	uint8_t status = 0;

	issueCDROMCommand(CDROM_CMD_NOP, 0, 0);
	CDROMIRQType irq = waitForCDROMIRQ(&status, sizeof(status));

	if (irq == CDROM_IRQ_ACKNOWLEDGE)
		ptr += sprintf(
			ptr,
			"  Status byte:\t%02X\n"
			"  Spindle motor:\t%s\n"
			"  Lid status:\t\t%s\n"
			"  Busy seeking:\t%s\n"
			"  Busy playing:\t%s\n",
			status,
			(status & CDROM_CMDSTAT_SPINDLE_ON) ? "on"   : "off",
			(status & CDROM_CMDSTAT_LID_OPEN)   ? "open" : "closed",
			(status & CDROM_CMDSTAT_SEEKING)    ? "yes"  : "no",
			(status & CDROM_CMDSTAT_PLAYING)    ? "yes"  : "no"
		);
	else
		ptr += sprintf(ptr, "  Unknown (got IRQ %d)\n", irq);
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

	// The SPU doesn't technically need to be fully initialized: we're only
	// relying on it to pass audio from the CD-ROM through, so simply unmuting
	// it and setting the master volume would be enough. Initialization is
	// however useful to get rid of the startup sound and its reverb tail when
	// the executable is launched directly by the BIOS.
	initSerialIO(115200);
	setupGPU(
		getCurrentVideoMode(),
		SCREEN_HRES,
		SCREEN_VRES,
		SCREEN_WIDTH,
		SCREEN_HEIGHT
	);
	initSIO0();
	initSPU();
	initCDROM();
	setupCDROMAudio_(false);

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

	char status[256];
	int  nextPoll = 0;

	printCDROMStatus(status);

	MenuState state = {
		.lastButtons   = 0,
		.selectedTrack = 1
	};

	GPUDMAChain dmaChains[2];
	bool        usingSecondFrame = false;
	int         frameCounter     = 0;

	for (;;) {
		int bufferX = usingSecondFrame ? SCREEN_WIDTH : 0;
		int bufferY = 0;

		GPUDMAChain *chain = &dmaChains[usingSecondFrame];
		usingSecondFrame   = !usingSecondFrame;
		frameCounter++;

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

		// Take inputs from either controller port, update the menu accordingly
		// and fetch the current CD-ROM status periodically (same as the last
		// example).
		updateMenu(&state, pollController(0) | pollController(1));

		if (frameCounter > nextPoll) {
			printCDROMStatus(status);
			nextPoll = frameCounter + 15;
		}

		// Display the menu and last polled status.
		char menu[256];

		printMenu(&state, menu);
		printString(chain, &font, 16, 32, menu);
		printString(chain, &font, 16, 96, status);

		*(chain->nextPacket) = gp0_endTag(0);

		waitForGP0Ready();
		waitForVSync();
		sendGPULinkedList(chain->data);
	}

	return 0;
}
