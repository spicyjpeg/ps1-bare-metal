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
 * In this tutorial we're going to initialize the GPU, set it up and display
 * some simple hardware-rendered graphics (namely, a shaded triangle).
 *
 * While the PS1's GPU may appear complicated and daunting, its principle of
 * operation is in actual fact very simple. At a high level it is simply a
 * "rasterization machine" capable of drawing triangles, quads, rectangles and
 * lines in 2D screen space and displaying a rectangular cutout of its
 * 1024x512x16bpp framebuffer, which resides in dedicated VRAM (not directly
 * exposed to the CPU). It is controlled using two I/O registers, one for "slow"
 * drawing commands (GP0, backed by a 16-word queue) and one for control
 * commands that take effect immediately (GP1). We're going to see how to
 * configure the video output and draw our triangle by writing the appropriate
 * commands to these registers.
 *
 * NOTE: in order to keep things simple this tutorial uses the ps1/gpucmd.h
 * header, which contains definitions and inline functions to quickly construct
 * GPU commands. The commands are documented in detail here:
 *     https://psx-spx.consoledev.net/graphicsprocessingunitgpu
 */

#include <stdio.h>
#include "ps1/gpucmd.h"
#include "ps1/registers.h"

static void setupGPU(
	GP1VideoMode     mode,
	GP1HorizontalRes horizontalRes,
	GP1VerticalRes   verticalRes,
	unsigned int     width,
	unsigned int     height
) {
	// Set the origin of the displayed framebuffer relative to the GPU's
	// internal video clocks. The values below will center the picture on most
	// modern displays and upscalers, but may need adjustment on older CRTs.
	int x = 0x760;
	int y = (mode == GP1_MODE_PAL) ? 0xa3 : 0x88;

	// Set the exact number of pixels that will be sent to the display
	// horizontally and vertically around the origin. These values are again in
	// GPU clock units rather than pixels, so they are dependent on the selected
	// resolution (see below).
	int offsetX = (width  * gp1_clockMultiplierH(horizontalRes)) / 2;
	int offsetY = (height / gp1_clockDividerV(verticalRes))      / 2;

	// Reset the GPU and apply the calculated display range using GP1 commands.
	GPU_GP1 = gp1_resetGPU();
	GPU_GP1 = gp1_fbRangeH(x - offsetX, x + offsetX);
	GPU_GP1 = gp1_fbRangeV(y - offsetY, y + offsetY);

	// Set the video mode and resolution. This setting is separate from the
	// display range and controls how fast pixels are output (thus affecting the
	// aspect ratio) rather than just cropping the image. The GPU provides a
	// number of fixed horizontal (256, 320, 368, 512, 640) and vertical
	// (240-256 progressive or interlaced, 480-512 interlaced only) resolutions
	// to pick from.
	GPU_GP1 = gp1_fbMode(
		horizontalRes,
		verticalRes,
		mode,
		verticalRes == GP1_VRES_512,
		GP1_COLOR_16BPP
	);

	// Unblank (turn on) the video output, as resetting the GPU blanks it by
	// default.
	GPU_GP1 = gp1_dispBlank(false);
}

static void waitForGP0Ready(void) {
	// Block until the GPU reports that its GP0 queue is empty through its
	// status register, accessible by reading from the same address as GP1. Once
	// the FIFO is empty, up to 16 command words can be written in a row to GP0
	// before having to wait for the GPU to drain it again.
	while (!(GPU_STAT & GPU_STAT_WFEP))
		__asm__ volatile("");
}

#define SCREEN_HRES   GP1_HRES_320
#define SCREEN_VRES   GP1_VRES_256
#define SCREEN_WIDTH  320
#define SCREEN_HEIGHT 240

int main(int argc, const char **argv) {
	(void) argc;
	(void) argv;

	// Initialize the serial interface so that functions such as putchar(),
	// puts(), printf() and so on can be used for logging. The initSerialIO()
	// function is defined in libc/misc.c and does basically what did in the
	// last example.
	initSerialIO(115200);

	// Read the GPU's status register before resetting it to check if it was
	// left in PAL or NTSC mode by the BIOS or loader, in order to keep using
	// the same mode.
	bool isPAL = ((GPU_STAT & GPU_STAT_NPB_BITMASK) == GPU_STAT_NPB_PAL);

	setupGPU(
		isPAL ? GP1_MODE_PAL : GP1_MODE_NTSC,
		SCREEN_HRES,
		SCREEN_VRES,
		SCREEN_WIDTH,
		SCREEN_HEIGHT
	);

	// Wait for the GPU to become ready for command words, then send some basic
	// GP0 commands to enable dithering and set up the scissoring region (i.e.
	// where in VRAM we'll be allowed to draw). All X/Y coordinates will also be
	// relative to the origin point set here.
	waitForGP0Ready();
	GPU_GP0 = gp0_setPage(0, true, false);
	GPU_GP0 = gp0_fbOffset1(0, 0);
	GPU_GP0 = gp0_fbOffset2(SCREEN_WIDTH - 1, SCREEN_HEIGHT - 1);
	GPU_GP0 = gp0_fbOrigin(0, 0);

	// Send a 3-word VRAM fill command to quickly fill our drawing area with
	// solid gray. Note that this is one of the few commands that bypass the
	// origin setting and always use absolute VRAM coordinates.
	waitForGP0Ready();
	GPU_GP0 = gp0_rgb(64, 64, 64) | gp0_vramFill();
	GPU_GP0 = gp0_xy(0, 0);
	GPU_GP0 = gp0_xy(SCREEN_WIDTH, SCREEN_HEIGHT);

	// Draw a Gouraud shaded triangle whose vertices are red, green and blue
	// respectively at the center of our area. The length of a triangle command
	// varies depending on whether Gouraud shading and/or texturing are enabled;
	// if we were to draw a flat shaded triangle instead, it would shorten to 4
	// words as we wouldn't have to provide the second and third vertex colors.
	waitForGP0Ready();
	GPU_GP0 = gp0_rgb(255, 0, 0) | gp0_shadedTriangle(true, false, false);
	GPU_GP0 = gp0_xy(SCREEN_WIDTH / 2, 32);
	GPU_GP0 = gp0_rgb(0, 255, 0);
	GPU_GP0 = gp0_xy(32, SCREEN_HEIGHT - 32);
	GPU_GP0 = gp0_rgb(0, 0, 255);
	GPU_GP0 = gp0_xy(SCREEN_WIDTH - 32, SCREEN_HEIGHT - 32);

	// Point the display side of the GPU at what we've just rendered, then halt.
	GPU_GP1 = gp1_fbOffset(0, 0);

	for (;;)
		__asm__ volatile("");

	return 0;
}
