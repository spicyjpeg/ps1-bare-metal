/*
 * ps1-bare-metal - (C) 2023-2025 spicyjpeg
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
 * We saw how to initialize the GPU and get basic graphics on screen in the last
 * tutorial. It's now time to add motion to the mix: we're going to draw a
 * square which, in true DVD player screensaver fashion, will bounce around on
 * the screen.
 *
 * This may sound simple in theory, but there are a few caveats we'll have to
 * look out for. First of all we will need some sort of timer for our animation,
 * ideally something synchronized to the display output in order to avoid
 * updating the position of our square while the picture is still being sent by
 * the GPU to the monitor (and stabilize the frame rate). We'll also have to
 * ensure the frame we are sending in the first place is not actively being
 * updated by the GPU, otherwise screen tearing will be prominent. Hiding a
 * frame while it is being drawn may sound tricky, but there is a very simple
 * way to accomplish it: we are going to keep *two* frames in VRAM, and draw one
 * while the other is being displayed. Once drawing is done and the other frame
 * has been fully sent to the display, we're going to swap the buffers (so that
 * the newly rendered frame will be displayed) and start over.
 *
 * This is an extremely common practice (the device you are looking at right now
 * is no doubt using it) known as double buffering, and you can read more about
 * it here:
 *     https://gameprogrammingpatterns.com/double-buffer.html
 */

#include <stdbool.h>
#include <stdio.h>
#include "ps1/gpucmd.h"
#include "ps1/registers.h"

static void setupGPU(GP1VideoMode mode, int width, int height) {
	int x = 0x760;
	int y = (mode == GP1_MODE_PAL) ? 0xa3 : 0x88;

	GP1HorizontalRes horizontalRes = GP1_HRES_320;
	GP1VerticalRes   verticalRes   = GP1_VRES_256;

	int offsetX = (width  * gp1_clockMultiplierH(horizontalRes)) / 2;
	int offsetY = (height / gp1_clockDividerV(verticalRes))      / 2;

	GPU_GP1 = gp1_resetGPU();
	GPU_GP1 = gp1_fbRangeH(x - offsetX, x + offsetX);
	GPU_GP1 = gp1_fbRangeV(y - offsetY, y + offsetY);
	GPU_GP1 = gp1_fbMode(
		horizontalRes,
		verticalRes,
		mode,
		false,
		GP1_COLOR_16BPP
	);
}

static void waitForGP0Ready(void) {
	while (!(GPU_GP1 & GP1_STAT_CMD_READY))
		__asm__ volatile("");
}

static void waitForVSync(void) {
	// The GPU won't tell us directly whenever it is done sending a frame to the
	// display, but it will send a signal to another peripheral known as the
	// interrupt controller (which will be covered in a future tutorial). We can
	// thus wait until the interrupt controller's vertical blank flag gets set,
	// then reset (acknowledge) it so that it can be set again by the GPU.
	while (!(IRQ_STAT & (1 << IRQ_VSYNC)))
		__asm__ volatile("");

	IRQ_STAT = ~(1 << IRQ_VSYNC);
}

#define SCREEN_WIDTH  320
#define SCREEN_HEIGHT 240

int main(int argc, const char **argv) {
	initSerialIO(115200);

	if ((GPU_GP1 & GP1_STAT_FB_MODE_BITMASK) == GP1_STAT_FB_MODE_PAL) {
		puts("Using PAL mode");
		setupGPU(GP1_MODE_PAL, SCREEN_WIDTH, SCREEN_HEIGHT);
	} else {
		puts("Using NTSC mode");
		setupGPU(GP1_MODE_NTSC, SCREEN_WIDTH, SCREEN_HEIGHT);
	}

	// Turn on the video output.
	GPU_GP1 = gp1_dispBlank(false);

	int x = 0, velocityX = 1;
	int y = 0, velocityY = 1;

	bool usingSecondFrame = false;

	for (;;) {
		// Determine the VRAM location of the current frame. We're going to
		// place the two frames next to each other in VRAM, at (0, 0) and
		// (320, 0) respectively.
		int frameX = usingSecondFrame ? SCREEN_WIDTH : 0;
		int frameY = 0;

		usingSecondFrame = !usingSecondFrame;

		// Tell the GPU which area of VRAM belongs to the frame we're going to
		// use and enable dithering.
		waitForGP0Ready();
		GPU_GP0 = gp0_texpage(0, true, false);
		GPU_GP0 = gp0_fbOffset1(frameX, frameY);
		GPU_GP0 = gp0_fbOffset2(
			frameX + SCREEN_WIDTH  - 1,
			frameY + SCREEN_HEIGHT - 2
		);
		GPU_GP0 = gp0_fbOrigin(frameX, frameY);

		// Fill the framebuffer with solid gray.
		waitForGP0Ready();
		GPU_GP0 = gp0_rgb(64, 64, 64) | gp0_vramFill();
		GPU_GP0 = gp0_xy(frameX, frameY);
		GPU_GP0 = gp0_xy(SCREEN_WIDTH, SCREEN_HEIGHT);

		// Draw the yellow bouncing square using a rectangle command.
		waitForGP0Ready();
		GPU_GP0 = gp0_rgb(255, 255, 0) | gp0_rectangle(false, false, false);
		GPU_GP0 = gp0_xy(x, y);
		GPU_GP0 = gp0_xy(32, 32);

		// Update the position of the bouncing square.
		x += velocityX;
		y += velocityY;

		if ((x <= 0) || (x >= (SCREEN_WIDTH - 32)))
			velocityX = -velocityX;
		if ((y <= 0) || (y >= (SCREEN_HEIGHT - 32)))
			velocityY = -velocityY;

		// Wait for the GPU to finish drawing and displaying the contents of the
		// previous frame, then tell it to start sending the newly drawn frame
		// to the video output.
		waitForGP0Ready();
		waitForVSync();

		GPU_GP1 = gp1_fbOffset(frameX, frameY);
	}

	return 0;
}
