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
 * In this tutorial we're going to initialize the GPU, set it up and display
 * some simple hardware-rendered graphics (namely, a shaded triangle).
 *
 * While the PS1's GPU may appear complicated and daunting, its principle of
 * operation is in actual fact very simple. At a high level it is simply a
 * "rasterization machine" capable of drawing triangles, quads, rectangles and
 * lines in 2D space (with 3D transformations being entirely the CPU's
 * responsibility) and of displaying a rectangular cutout of its 1024x512x16bpp
 * framebuffer, which resides in dedicated memory (VRAM). It is controlled using
 * only two registers, one (GP0) for drawing commands and the other (GP1) for
 * display control and other commands; we're going to see how to configure the
 * video output and draw our triangle by writing the right commands to these
 * registers.
 *
 * This tutorial will use the ps1/gpucmd.h header file I wrote, which contains
 * enumerations and inline functions for all commands supported by the GPU. If
 * you wish to write such a header yourself, you'll want to check out the
 * GPU register and command documentation at:
 *     https://psx-spx.consoledev.net/graphicsprocessingunitgpu
 */

#include <stdio.h>
#include "ps1/gpucmd.h"
#include "ps1/registers.h"

static void setupGPU(GP1VideoMode mode, int width, int height) {
	// Set the origin of the displayed framebuffer. These "magic" values,
	// derived from the GPU's internal clocks, will center the picture on most
	// displays and upscalers.
	int x = 0x760;
	int y = (mode == GP1_MODE_PAL) ? 0xa3 : 0x88;

	// Set the resolution. The GPU provides a number of fixed horizontal (256,
	// 320, 368, 512, 640) and vertical (240-256, 480-512) resolutions to pick
	// from, which affect how fast pixels are output and thus how "stretched"
	// the framebuffer will appear.
	GP1HorizontalRes horizontalRes = GP1_HRES_320;
	GP1VerticalRes   verticalRes   = GP1_VRES_256;

	// Set the number of displayed rows and columns. These values are in GPU
	// clock units rather than pixels, thus they are dependent on the selected
	// resolution.
	int offsetX = (width  * gp1_clockMultiplierH(horizontalRes)) / 2;
	int offsetY = (height / gp1_clockDividerV(verticalRes))      / 2;

	// Hand all parameters over to the GPU by sending GP1 commands.
	GPU_GP1 = gp1_resetGPU();
	GPU_GP1 = gp1_fbRangeH(x - offsetX, x + offsetX);
	GPU_GP1 = gp1_fbRangeV(y - offsetY, y + offsetY);
	GPU_GP1 = gp1_fbMode(
		horizontalRes, verticalRes, mode, false, GP1_COLOR_16BPP
	);
}

static void waitForGP0Ready(void) {
	// Block until the GPU reports to be ready to accept commands through its
	// status register (which has the same address as GP1 but is read-only).
	while (!(GPU_GP1 & GP1_STAT_CMD_READY))
		__asm__ volatile("");
}

#define SCREEN_WIDTH  320
#define SCREEN_HEIGHT 240

int main(int argc, const char **argv) {
	// Initialize the serial interface. The initSerialIO() function is defined
	// in src/libc/misc.c and does basically the same things we did in the
	// previous example. Afterwards, we'll be able to use puts(), printf() and
	// a few other standard I/O functions as they are declared in the libc
	// directory (with printf() being provided by a third-party library).
	initSerialIO(115200);

	// Read the GPU's status register to check if it was left in PAL or NTSC
	// mode by the BIOS/loader.
	if ((GPU_GP1 & GP1_STAT_FB_MODE_BITMASK) == GP1_STAT_FB_MODE_PAL) {
		puts("Using PAL mode");
		setupGPU(GP1_MODE_PAL, SCREEN_WIDTH, SCREEN_HEIGHT);
	} else {
		puts("Using NTSC mode");
		setupGPU(GP1_MODE_NTSC, SCREEN_WIDTH, SCREEN_HEIGHT);
	}

	// Wait for the GPU to become ready, then send some GP0 commands to tell it
	// which area of the framebuffer we want to draw to and enable dithering.
	waitForGP0Ready();
	GPU_GP0 = gp0_texpage(0, true, false);
	GPU_GP0 = gp0_fbOffset1(0, 0);
	GPU_GP0 = gp0_fbOffset2(SCREEN_WIDTH - 1, SCREEN_HEIGHT - 1);
	GPU_GP0 = gp0_fbOrigin(0, 0);

	// Send a VRAM fill command to quickly fill our area with solid gray. Note
	// that the coordinates passed to this specific command are *not* relative
	// to the ones we've just sent to the GPU!
	waitForGP0Ready();
	GPU_GP0 = gp0_rgb(64, 64, 64) | gp0_vramFill();
	GPU_GP0 = gp0_xy(0, 0);
	GPU_GP0 = gp0_xy(SCREEN_WIDTH, SCREEN_HEIGHT);

	// Tell the GPU to draw a Gouraud shaded triangle whose vertices are red,
	// green and blue respectively at the center of our drawing area.
	waitForGP0Ready();
	GPU_GP0 = gp0_rgb(255, 0, 0) | gp0_shadedTriangle(true, false, false);
	GPU_GP0 = gp0_xy(SCREEN_WIDTH / 2, 32);
	GPU_GP0 = gp0_rgb(0, 255, 0);
	GPU_GP0 = gp0_xy(32, SCREEN_HEIGHT - 32);
	GPU_GP0 = gp0_rgb(0, 0, 255);
	GPU_GP0 = gp0_xy(SCREEN_WIDTH - 32, SCREEN_HEIGHT - 32);

	// Send two GP1 commands to set the origin of the area we want to display
	// and switch on the display output.
	GPU_GP1 = gp1_fbOffset(0, 0);
	GPU_GP1 = gp1_dispBlank(false);

	// Continue by doing nothing.
	for (;;)
		__asm__ volatile("");

	return 0;
}
