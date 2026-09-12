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
 * We saw how to initialize the GPU and get basic graphics on screen in the last
 * tutorial. It's now time to add motion to the mix: we're going to draw a
 * square bouncing around the screen, DVD player screensaver style. This may
 * sound simple, but there are a few caveats we'll have to look out for.
 *
 * We need some sort of timer for our animation, synchronized to the display
 * output so that the position of our square is only updated once per frame;
 * moreover, we shouldn't draw to the same frame that is currently being sent to
 * the display (doing so would introduce screen tearing and flicker). We are
 * thus going to keep *two* frames in VRAM, drawing one while while the other is
 * being displayed, then swap them and run our update logic each time the GPU is
 * done sending the previous frame (i.e. during the vertical sync/blanking
 * interval). This will also cap our frame rate to the display's refresh rate
 * (50 or 60 Hz depending on video mode).
 *
 * This is a common practice known as page flipping or double buffering. You can
 * read more about it here if you are not familiar with it:
 *     https://gameprogrammingpatterns.com/double-buffer.html
 *
 * NOTE: from this example onwards, any GPU function introduced in a previous
 * example will be omitted and the respective copy from common/gpu.c will be
 * used instead. To prevent conflicts with gpu.c definitions, some newly
 * introduced (such as waitForVSync() below) or modified (such as setupGPU())
 * functions will have to be suffixed with an underscore.
 */

#include <stdbool.h>
#include <stdio.h>
#include "common/gpu.h"
#include "ps1/gpucmd.h"
#include "ps1/registers.h"

static void setupGPU_(
	GP1VideoMode     mode,
	GP1HorizontalRes horizontalRes,
	GP1VerticalRes   verticalRes,
	int              width,
	int              height
) {
	int x = 0x760;
	int y = (mode == GP1_MODE_PAL) ? 0xa3 : 0x88;

	int offsetX = (width  * gp1_clockMultiplierH(horizontalRes)) / 2;
	int offsetY = (height / gp1_clockDividerV(verticalRes))      / 2;

	GPU_GP1 = gp1_resetGPU();
	GPU_GP1 = gp1_fbRangeH(x - offsetX, x + offsetX);
	GPU_GP1 = gp1_fbRangeV(y - offsetY, y + offsetY);
	GPU_GP1 = gp1_fbMode(
		horizontalRes,
		verticalRes,
		mode,
		verticalRes == GP1_VRES_512,
		GP1_COLOR_16BPP
	);
	GPU_GP1 = gp1_dispBlank(false);

	// The GPU won't tell us directly whenever it is done sending a frame to the
	// display, but it will send a signal to another peripheral known as the
	// interrupt request (IRQ) controller. The IRQ controller may also force the
	// CPU to jump to handling code outside of our control (typically in the
	// console's BIOS kernel) when that happens, making us unable to reliably
	// tell if it happened (especially if said code resets the IRQ flag before
	// we ever see it). To prevent that we can "mask" the signal: the IRQ
	// controller will still track it, but it will no longer invoke the handler.
	IRQ_MASK &= ~(1 << IRQ_VSYNC);
}

static void waitForVSync_(void) {
	// With the vsync IRQ masked, we can safely poll its respective flag and
	// wait until it gets set, then reset (acknowledge) it so that it can be set
	// again by the GPU.
	while (!(IRQ_STAT & (1 << IRQ_VSYNC)))
		__asm__ volatile("");

	IRQ_STAT = ~(1 << IRQ_VSYNC);
}

#define SCREEN_HRES   GP1_HRES_320
#define SCREEN_VRES   GP1_VRES_256
#define SCREEN_WIDTH  320
#define SCREEN_HEIGHT 240

int main(int argc, const char **argv) {
	(void) argc;
	(void) argv;

	initSerialIO(115200);
	setupGPU_(
		getCurrentVideoMode(),
		SCREEN_HRES,
		SCREEN_VRES,
		SCREEN_WIDTH,
		SCREEN_HEIGHT
	);

	int x = 0, velocityX = 1;
	int y = 0, velocityY = 1;

	bool usingSecondFrame = false;

	for (;;) {
		// Determine the VRAM location of the current frame. We're going to
		// place the two buffers next to each other in VRAM, at (0, 0) and
		// (SCREEN_WIDTH, 0) respectively.
		int frameX = usingSecondFrame ? SCREEN_WIDTH : 0;
		int frameY = 0;

		usingSecondFrame = !usingSecondFrame;

		// Tell the GPU which area of VRAM belongs to the frame we're going to
		// use and enable dithering.
		waitForGP0Ready();
		GPU_GP0 = gp0_setPage(0, true, false);
		GPU_GP0 = gp0_fbOffset1(frameX, frameY);
		GPU_GP0 = gp0_fbOffset2(
			frameX + SCREEN_WIDTH  - 1,
			frameY + SCREEN_HEIGHT - 1
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

		// Wait for the GPU to reach the sync window, then tell it to start
		// outputting the newly drawn frame and loop to the next frame.
		waitForGP0Ready();
		waitForVSync_();

		GPU_GP1 = gp1_fbOffset(frameX, frameY);
	}

	return 0;
}
