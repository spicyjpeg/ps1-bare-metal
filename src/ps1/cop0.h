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

#include <stdint.h>

#define DEF(type) static inline type __attribute__((always_inline))

/* Register definitions */

typedef enum {
	COP0_BPC      =  3, // Breakpoint program counter
	COP0_BDA      =  5, // Breakpoint data address
	COP0_DCIC     =  7, // Debug and cache invalidation control
	COP0_BADVADDR =  8, // Bad virtual address
	COP0_BDAM     =  9, // Breakpoint data address mask
	COP0_BPCM     = 11, // Breakpoint program counter mask
	COP0_STATUS   = 12, // Status register
	COP0_CAUSE    = 13, // Exception cause
	COP0_EPC      = 14, // Exception program counter
	COP0_PRID     = 15  // Processor identifier
} COP0Register;

typedef enum {
	COP0_DCIC_DB  = 1 <<  0, // Debug event pending
	COP0_DCIC_PC  = 1 <<  1, // Program counter breakpoint pending
	COP0_DCIC_DA  = 1 <<  2, // Data address breakpoint pending
	COP0_DCIC_R   = 1 <<  3, // Data address read breakpoint pending
	COP0_DCIC_W   = 1 <<  4, // Data address write breakpoint pending
	COP0_DCIC_T   = 1 <<  5, // Trace event pending
	COP0_DCIC_DE  = 1 << 23, // Debug enable
	COP0_DCIC_PCE = 1 << 24, // Program counter breakpoint enable
	COP0_DCIC_DAE = 1 << 25, // Data address breakpoint enable
	COP0_DCIC_DR  = 1 << 26, // Data address read breakpoint enable
	COP0_DCIC_DW  = 1 << 27, // Data address write breakpoint enable
	COP0_DCIC_TE  = 1 << 28, // Trace enable
	COP0_DCIC_KD  = 1 << 29, // Kernel debug enable
	COP0_DCIC_UD  = 1 << 30, // User debug enable
	COP0_DCIC_TR  = 1 << 31  // Debug event trap enable
} COP0DCICFlag;

typedef enum {
	COP0_STATUS_IEc = 1 <<  0, // Current interrupt enable
	COP0_STATUS_KUc = 1 <<  1, // Current privilege level
	COP0_STATUS_IEp = 1 <<  2, // Previous interrupt enable
	COP0_STATUS_KUp = 1 <<  3, // Previous privilege level
	COP0_STATUS_IEo = 1 <<  4, // Old interrupt enable
	COP0_STATUS_KUo = 1 <<  5, // Old privilege level
	COP0_STATUS_Im0 = 1 <<  8, // IRQ mask 0 (software interrupt)
	COP0_STATUS_Im1 = 1 <<  9, // IRQ mask 1 (software interrupt)
	COP0_STATUS_Im2 = 1 << 10, // IRQ mask 2 (hardware interrupt)
	COP0_STATUS_IsC = 1 << 16, // Isolate cache
	COP0_STATUS_BEV = 1 << 22, // Boot exception vector location
	COP0_STATUS_CU0 = 1 << 28, // Coprocessor 0 privilege level
	COP0_STATUS_CU2 = 1 << 30  // Coprocessor 2 enable
} COP0StatusFlag;

typedef enum {
	COP0_CAUSE_EXC_BITMASK = 31 <<  2,
	COP0_CAUSE_EXC_INT     =  0 <<  2, // Interrupt
	COP0_CAUSE_EXC_AdEL    =  4 <<  2, // Load address error
	COP0_CAUSE_EXC_AdES    =  5 <<  2, // Store address error
	COP0_CAUSE_EXC_IBE     =  6 <<  2, // Instruction bus error
	COP0_CAUSE_EXC_DBE     =  7 <<  2, // Data bus error
	COP0_CAUSE_EXC_SYS     =  8 <<  2, // Syscall
	COP0_CAUSE_EXC_BP      =  9 <<  2, // Breakpoint or break instruction
	COP0_CAUSE_EXC_RI      = 10 <<  2, // Reserved instruction
	COP0_CAUSE_EXC_CpU     = 11 <<  2, // Coprocessor unusable
	COP0_CAUSE_EXC_Ov      = 12 <<  2, // Arithmetic overflow
	COP0_CAUSE_Ip0         =  1 <<  8, // IRQ 0 pending (software interrupt)
	COP0_CAUSE_Ip1         =  1 <<  9, // IRQ 1 pending (software interrupt)
	COP0_CAUSE_Ip2         =  1 << 10, // IRQ 2 pending (hardware interrupt)
	COP0_CAUSE_CE_BITMASK  =  3 << 28,
	COP0_CAUSE_BD          =  1 << 30  // Exception occurred in delay slot
} COP0CauseFlag;

// Note that reg must be a constant value known at compile time, as the
// mtc0/mfc0 instructions only support addressing coprocessor registers directly
// through immediates.
DEF(void) cop0_setReg(const COP0Register reg, uint32_t value) {
	__asm__ volatile("mtc0 %0, $%1\n" :: "r"(value), "i"(reg));
}
DEF(uint32_t) cop0_getReg(const COP0Register reg) {
	uint32_t value;

	__asm__ volatile("mfc0 %0, $%1\n" : "=r"(value) : "i"(reg));
	return value;
}

#undef DEF
