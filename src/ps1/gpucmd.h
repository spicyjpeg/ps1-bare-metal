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

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define DEF(type) static inline type __attribute__((always_inline))

/* DMA tags */

DEF(uint32_t) gp0_tag(size_t length, void *next) {
	return 0
		| (((uint32_t) next   & 0xffffff) <<  0)
		| (((uint32_t) length & 0x0000ff) << 24);
}

DEF(uint32_t) gp0_endTag(size_t length) {
	return gp0_tag(length, (void *) 0xffffff);
}

/* Drawing attributes */

typedef enum {
	GP0_BLEND_BITMASK   = 3,
	GP0_BLEND_SEMITRANS = 0,
	GP0_BLEND_ADD       = 1,
	GP0_BLEND_SUBTRACT  = 2,
	GP0_BLEND_DIV4_ADD  = 3
} GP0BlendMode;

typedef enum {
	GP0_COLOR_BITMASK = 3,
	GP0_COLOR_4BPP    = 0,
	GP0_COLOR_8BPP    = 1,
	GP0_COLOR_16BPP   = 2
} GP0ColorDepth;

DEF(uint16_t) gp0_page(
	unsigned int  x,
	unsigned int  y,
	GP0BlendMode  blendMode,
	GP0ColorDepth colorDepth
) {
	return 0
		| ((x          & 15) <<  0)
		| ((y          &  1) <<  4)
		| ((blendMode  &  3) <<  5)
		| ((colorDepth &  3) <<  7)
		| ((y          &  2) << 10);
}
DEF(uint16_t) gp0_clut(unsigned int x, unsigned int y) {
	return 0
		| ((x & 0x03f) << 0)
		| ((y & 0x3ff) << 6);
}

DEF(uint32_t) gp0_xy(int x, int y) {
	return 0
		| ((x & 0xffff) <<  0)
		| ((y & 0xffff) << 16);
}
DEF(uint32_t) gp0_uv(unsigned int u, unsigned int v, uint16_t attr) {
	return 0
		| ((u    & 0x00ff) <<  0)
		| ((v    & 0x00ff) <<  8)
		| ((attr & 0xffff) << 16);
}
DEF(uint32_t) gp0_rgb(uint8_t r, uint8_t g, uint8_t b) {
	return 0
		| ((r & 0xff) <<  0)
		| ((g & 0xff) <<  8)
		| ((b & 0xff) << 16);
}

/* GP0 (drawing) commands */

typedef enum {
	GP0_CMD_MISC       = 0 << 29,
	GP0_CMD_POLYGON    = 1 << 29,
	GP0_CMD_LINE       = 2 << 29,
	GP0_CMD_RECTANGLE  = 3 << 29,
	GP0_CMD_VRAM_BLIT  = 4 << 29,
	GP0_CMD_VRAM_WRITE = 5 << 29,
	GP0_CMD_VRAM_READ  = 6 << 29,
	GP0_CMD_ATTRIBUTE  = 7 << 29
} GP0Command;

typedef enum {
	GP0_CMD_NOP         = GP0_CMD_MISC | ( 0 << 24),
	GP0_CMD_FLUSH_CACHE = GP0_CMD_MISC | ( 1 << 24),
	GP0_CMD_VRAM_FILL   = GP0_CMD_MISC | ( 2 << 24),
	GP0_CMD_NOP2        = GP0_CMD_MISC | ( 3 << 24),
	GP0_CMD_IRQ         = GP0_CMD_MISC | (31 << 24)
} GP0MiscCommand;

typedef enum {
	GP0_CMD_TEXPAGE    = GP0_CMD_ATTRIBUTE | (1 << 24),
	GP0_CMD_TEXWINDOW  = GP0_CMD_ATTRIBUTE | (2 << 24),
	GP0_CMD_FB_OFFSET1 = GP0_CMD_ATTRIBUTE | (3 << 24),
	GP0_CMD_FB_OFFSET2 = GP0_CMD_ATTRIBUTE | (4 << 24),
	GP0_CMD_FB_ORIGIN  = GP0_CMD_ATTRIBUTE | (5 << 24),
	GP0_CMD_FB_MASK    = GP0_CMD_ATTRIBUTE | (6 << 24)
} GP0AttributeCommand;

DEF(uint32_t) _gp0_polygon(
	bool quad,
	bool unshaded,
	bool gouraud,
	bool textured,
	bool blend
) {
	return GP0_CMD_POLYGON
		| ((unshaded & 1) << 24)
		| ((blend    & 1) << 25)
		| ((textured & 1) << 26)
		| ((quad     & 1) << 27)
		| ((gouraud  & 1) << 28);
}
DEF(uint32_t) gp0_triangle(bool textured, bool blend) {
	return _gp0_polygon(false, true, false, textured, blend);
}
DEF(uint32_t) gp0_shadedTriangle(bool gouraud, bool textured, bool blend) {
	return _gp0_polygon(false, false, gouraud, textured, blend);
}
DEF(uint32_t) gp0_quad(bool textured, bool blend) {
	return _gp0_polygon(true, true, false, textured, blend);
}
DEF(uint32_t) gp0_shadedQuad(bool gouraud, bool textured, bool blend) {
	return _gp0_polygon(true, false, gouraud, textured, blend);
}

DEF(uint32_t) gp0_line(bool gouraud, bool blend) {
	return GP0_CMD_LINE
		| ((blend   & 1) << 25)
		| ((gouraud & 1) << 28);
}
DEF(uint32_t) gp0_polyLine(bool gouraud, bool blend) {
	return GP0_CMD_LINE
		| ((blend   & 1) << 25)
		| (1             << 27)
		| ((gouraud & 1) << 28);
}

DEF(uint32_t) _gp0_rectangle(
	uint8_t size,
	bool    textured,
	bool    unshaded,
	bool    blend
) {
	return GP0_CMD_RECTANGLE
		| ((unshaded & 1) << 24)
		| ((blend    & 1) << 25)
		| ((textured & 1) << 26)
		| ((size     & 3) << 27);
}
DEF(uint32_t) gp0_rectangle(bool textured, bool unshaded, bool blend) {
	return _gp0_rectangle(0, textured, unshaded, blend);
}
DEF(uint32_t) gp0_rectangle1x1(bool textured, bool unshaded, bool blend) {
	return _gp0_rectangle(1, textured, unshaded, blend);
}
DEF(uint32_t) gp0_rectangle8x8(bool textured, bool unshaded, bool blend) {
	return _gp0_rectangle(2, textured, unshaded, blend);
}
DEF(uint32_t) gp0_rectangle16x16(bool textured, bool unshaded, bool blend) {
	return _gp0_rectangle(3, textured, unshaded, blend);
}

DEF(uint32_t) gp0_vramBlit(void) {
	return GP0_CMD_VRAM_BLIT;
}
DEF(uint32_t) gp0_vramWrite(void) {
	return GP0_CMD_VRAM_WRITE;
}
DEF(uint32_t) gp0_vramRead(void) {
	return GP0_CMD_VRAM_READ;
}

DEF(uint32_t) gp0_flushCache(void) {
	return GP0_CMD_FLUSH_CACHE;
}
DEF(uint32_t) gp0_vramFill(void) {
	return GP0_CMD_VRAM_FILL;
}
DEF(uint32_t) gp0_irq(void) {
	return GP0_CMD_IRQ;
}

DEF(uint32_t) gp0_texpage(uint16_t page, bool dither, bool unlockFB) {
	return GP0_CMD_TEXPAGE
		| ((page     & 0x9ff) <<  0)
		| ((dither   &     1) <<  9)
		| ((unlockFB &     1) << 10);
}
DEF(uint32_t) gp0_texwindow(
	uint8_t baseX,
	uint8_t baseY,
	uint8_t maskX,
	uint8_t maskY
) {
	return GP0_CMD_TEXWINDOW
		| ((maskX & 0x1f) <<  0)
		| ((maskY & 0x1f) <<  5)
		| ((baseX & 0x1f) << 10)
		| ((baseY & 0x1f) << 15);
}
DEF(uint32_t) gp0_fbOffset1(unsigned int x, unsigned int y) {
	return GP0_CMD_FB_OFFSET1
		| ((x & 0x3ff) <<  0)
		| ((y & 0x3ff) << 10);
}
DEF(uint32_t) gp0_fbOffset2(unsigned int x, unsigned int y) {
	return GP0_CMD_FB_OFFSET2
		| ((x & 0x3ff) <<  0)
		| ((y & 0x3ff) << 10);
}
DEF(uint32_t) gp0_fbOrigin(int x, int y) {
	return GP0_CMD_FB_ORIGIN
		| ((x & 0x7ff) <<  0)
		| ((y & 0x7ff) << 11);
}
DEF(uint32_t) gp0_fbMask(bool setMask, bool useMask) {
	return GP0_CMD_FB_MASK
		| (setMask << 0)
		| (useMask << 1);
}

/* GP1 (display control) commands */

typedef enum {
	GP1_HRES_BITMASK = (3 << 0) | (1 << 6),
	GP1_HRES_256     = 0 << 0, // Dotclock divided by 10
	GP1_HRES_320     = 1 << 0, // Dotclock divided by 8
	GP1_HRES_368     = 1 << 6, // Dotclock divided by 7
	GP1_HRES_512     = 2 << 0, // Dotclock divided by 5
	GP1_HRES_640     = 3 << 0  // Dotclock divided by 4
} GP1HorizontalRes;

typedef enum {
	GP1_VRES_BITMASK = 1,
	GP1_VRES_256     = 0,
	GP1_VRES_512     = 1
} GP1VerticalRes;

typedef enum {
	GP1_MODE_BITMASK = 1,
	GP1_MODE_NTSC    = 0,
	GP1_MODE_PAL     = 1
} GP1VideoMode;

typedef enum {
	GP1_COLOR_BITMASK = 1,
	GP1_COLOR_16BPP   = 0,
	GP1_COLOR_24BPP   = 1
} GP1ColorDepth;

typedef enum {
	GP1_DREQ_BITMASK   = 3,
	GP1_DREQ_NONE      = 0,
	GP1_DREQ_FIFO      = 1,
	GP1_DREQ_GP0_WRITE = 2,
	GP1_DREQ_GP0_READ  = 3
} GP1DMARequestMode;

typedef enum {
	GP1_VRAM_BITMASK = 1,
	GP1_VRAM_1MB     = 0,
	GP1_VRAM_2MB     = 1
} GP1VRAMSize;

typedef enum {
	GP1_CMD_RESET_GPU   =  0 << 24,
	GP1_CMD_RESET_FIFO  =  1 << 24,
	GP1_CMD_ACKNOWLEDGE =  2 << 24,
	GP1_CMD_DISP_BLANK  =  3 << 24,
	GP1_CMD_DREQ_MODE   =  4 << 24,
	GP1_CMD_FB_OFFSET   =  5 << 24,
	GP1_CMD_FB_RANGE_H  =  6 << 24,
	GP1_CMD_FB_RANGE_V  =  7 << 24,
	GP1_CMD_FB_MODE     =  8 << 24,
	GP1_CMD_VRAM_SIZE   =  9 << 24,
	GP1_CMD_GET_INFO    = 16 << 24
} GP1Command;

DEF(uint32_t) gp1_clockMultiplierH(GP1HorizontalRes horizontalRes) {
	switch (horizontalRes) {
		case GP1_HRES_256:
			return 10;
		case GP1_HRES_320:
			return 8;
		case GP1_HRES_368:
			return 7;
		case GP1_HRES_512:
			return 5;
		case GP1_HRES_640:
			return 4;
		default:
			return 0;
	}
}

DEF(uint32_t) gp1_clockDividerV(GP1VerticalRes verticalRes) {
	switch (verticalRes) {
		case GP1_VRES_256:
			return 1;
		case GP1_VRES_512:
			return 2;
		default:
			return 0;
	}
}

DEF(uint32_t) gp1_resetGPU(void) {
	return GP1_CMD_RESET_GPU;
}
DEF(uint32_t) gp1_resetFIFO(void) {
	return GP1_CMD_RESET_FIFO;
}
DEF(uint32_t) gp1_acknowledge(void) {
	return GP1_CMD_ACKNOWLEDGE;
}
DEF(uint32_t) gp1_dispBlank(bool blank) {
	return GP1_CMD_DISP_BLANK
		| (blank & 1);
}
DEF(uint32_t) gp1_dmaRequestMode(GP1DMARequestMode mode) {
	return GP1_CMD_DREQ_MODE
		| (mode & 3);
}
DEF(uint32_t) gp1_fbOffset(unsigned int x, unsigned int y) {
	return GP1_CMD_FB_OFFSET
		| ((x & 0x3ff) <<  0)
		| ((y & 0x3ff) << 10);
}
DEF(uint32_t) gp1_fbRangeH(unsigned int low, unsigned int high) {
	return GP1_CMD_FB_RANGE_H
		| ((low  & 0xfff) <<  0)
		| ((high & 0xfff) << 12);
}
DEF(uint32_t) gp1_fbRangeV(unsigned int low, unsigned int high) {
	return GP1_CMD_FB_RANGE_V
		| ((low  & 0x3ff) <<  0)
		| ((high & 0x3ff) << 10);
}
DEF(uint32_t) gp1_fbMode(
	GP1HorizontalRes horizontalRes,
	GP1VerticalRes   verticalRes,
	GP1VideoMode     videoMode,
	bool             interlace,
	GP1ColorDepth    colorDepth
) {
	return GP1_CMD_FB_MODE
		| ((horizontalRes & 0x47) << 0)
		| ((verticalRes   &    1) << 2)
		| ((videoMode     &    1) << 3)
		| ((colorDepth    &    1) << 4)
		| ((interlace     &    1) << 5);
}
DEF(uint32_t) gp1_vramSize(GP1VRAMSize size) {
	return GP1_CMD_VRAM_SIZE
		| (size & 1);
}

#undef DEF
