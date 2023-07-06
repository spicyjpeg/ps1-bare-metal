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
 * This is a fast lookup-table-less implementation of fixed-point sine and
 * cosine, based on the isin_S4 implementation from:
 *     https://www.coranac.com/2009/07/sines
 */

#include "trig.h"

#define A (1 << 12)
#define B 19900
#define	C 3516

int isin(int x) {
	int c = x << (30 - ISIN_SHIFT);
	x    -= 1 << ISIN_SHIFT;

	x <<= 31 - ISIN_SHIFT;
	x >>= 31 - ISIN_SHIFT;
	x  *= x;
	x >>= 2 * ISIN_SHIFT - 14;

	int y = B - (x * C >> 14);
	y     = A - (x * y >> 16);

	return (c >= 0) ? y : (-y);
}

int isin2(int x) {
	int c = x << (30 - ISIN2_SHIFT);
	x    -= 1 << ISIN2_SHIFT;

	x <<= 31 - ISIN2_SHIFT;
	x >>= 31 - ISIN2_SHIFT;
	x  *= x;
	x >>= 2 * ISIN2_SHIFT - 14;

	int y = B - (x * C >> 14);
	y     = A - (x * y >> 16);

	return (c >= 0) ? y : (-y);
}
