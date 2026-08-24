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

#pragma once

#include "ps1/registers.h"

/* DTL-H2000/H2700 host I/O and IRQ10 controller */

typedef enum {
	DTLH_IRQ_HOST    = 0,
	DTLH_IRQ_PIO     = 1,
	DTLH_IRQ_UNKNOWN = 4,
	DTLH_IRQ_RX8     = 5
} DTLHIRQChannel;

typedef enum {
	DTLH_STAT_RX16_NOT_EMPTY = 1 << 0,
	DTLH_STAT_TX16_NOT_FULL  = 1 << 2,
	DTLH_STAT_TX8_NOT_FULL   = 1 << 3,
	DTLH_STAT_RX8_NOT_EMPTY  = 1 << 4
} DTLHStatusFlag;

#define DTLH_STAT     _MMIO8 (DEV8_BASE | 0x00)
#define DTLH_DATA8    _MMIO8 (DEV8_BASE | 0x02)
#define DTLH_DATA16   _MMIO16(DEV8_BASE | 0x04)
#define DTLH_IRQ_STAT _MMIO8 (DEV8_BASE | 0x30)
#define DTLH_IRQ_MASK _MMIO8 (DEV8_BASE | 0x32)

/* no$psx emulator API */

typedef enum {
	NOCASH_TURBO_CDROM       = 1 << 0,
	NOCASH_TURBO_MEMORY_CARD = 1 << 1,
	NOCASH_TURBO_CONTROLLER  = 1 << 2
} NocashTurboFlag;

#define NOCASH_MAGIC  _MMIO32(DEV8_BASE | 0x60)
#define NOCASH_ENABLE _MMIO16(DEV8_BASE | 0x64)
#define NOCASH_HALT   _MMIO8 (DEV8_BASE | 0x66)
#define NOCASH_TURBO  _MMIO8 (DEV8_BASE | 0x67)

/* PCSX-Redux emulator API */

#define PCSX_MAGIC        _MMIO32(DEV8_BASE | 0x80)
#define PCSX_PUTC         _MMIO8 (DEV8_BASE | 0x80)
#define PCSX_EXEC_SLOT    _MMIO8 (DEV8_BASE | 0x81)
#define PCSX_EXIT         _MMIO16(DEV8_BASE | 0x82)
#define PCSX_MESSAGE      _MMIO32(DEV8_BASE | 0x84)
#define PCSX_KERNEL_CHECK _MMIO8 (DEV8_BASE | 0x88)
