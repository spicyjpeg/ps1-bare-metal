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
 * This header defines (alongside kernel.s) a minimal subset of BIOS kernel API
 * stubs for basic TTY I/O and interrupt handling. This is the bare minimum
 * needed to interoperate with tools that inject themselves into the kernel such
 * as debuggers, cheat engines or TTY patches. Most of the other kernel APIs not
 * exposed here are buggy, unstable and/or slow to the point of being unusable.
 */

#pragma once

#include <setjmp.h>
#include <stdbool.h>
#include <stdint.h>

typedef enum {
	KERNEL_FD_STDIN  = 0,
	KERNEL_FD_STDOUT = 1
} KernelFileDescriptor;

typedef enum {
	KERNEL_RCNT_TIMER0 = 0,
	KERNEL_RCNT_TIMER1 = 1,
	KERNEL_RCNT_TIMER2 = 2,
	KERNEL_RCNT_VSYNC  = 3
} KernelRootCounter;

/* A0 functions */

// These two functions are slower and less efficient than ps1-bare-metal's own
// counterparts, but can be used in their place to reduce executable size. Note
// that using assert() in debug builds will still pull in our own printf
// implementation, nullifying any size savings unless modified not to.
int kernel_printf(const char *format, ...);

void kernel_flushCache(void);

/* B0 functions */

void kernel_returnFromException(void);

void kernel_resetEntryInt(void);
void kernel_hookEntryInt (jmp_buf env);

int kernel_fileRead (int fd, void       *data, int length);
int kernel_fileWrite(int fd, const void *data, int length);

const uint8_t *kernel_krom2RawAdd(uint16_t code);

void kernel_changeClearPad(bool clear);

/* C0 functions */

void kernel_changeClearRCnt(KernelRootCounter rcnt, bool clear);
