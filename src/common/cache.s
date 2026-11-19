# ps1-bare-metal - Copyright (C) 2023-2026 spicyjpeg
# SPDX-License-Identifier: MIT

# This file implements a function to flush the CPU's instruction cache. This is
# necessary when loading or modifying code in RAM, as the cache is not otherwise
# aware of memory writes. The BIOS kernel provides its own cache flushing API as
# well (exposed as kernel_flushCache() in ps1/kernel.h), but this implementation
# is over 20 times faster by virtue of skipping superfluous steps and running
# from main RAM, which has significantly lower waitstates than the BIOS ROM.

.set noreorder

.set KSEG0_BASE, 0x80000000
.set KSEG1_BASE, 0xa0000000
.set CPU_BCC,    0xfffe0130

.set CPU_BCC_TAG, (1 <<  2)
.set CPU_BCC_IS1, (1 << 11)

.set COP0_STATUS, $12

.set COP0_STATUS_IsC, (1 << 16)

.set ptr,         $v0
.set status,      $v0
.set bcc,         $v1
.set bccPtr,      $a0
.set savedStatus, $a1
.set savedBCC,    $a2

.section .text.flushCache, "ax", @progbits
.global  flushCache
.type    flushCache, @function

flushCache:
	# Jump to the uncached mirror of flushCacheInner() under KSEG1, ensuring the
	# CPU will not attempt to write to the cache while we clear it. The jump
	# must be performed indirectly through a la/jr combo since immediate jumps
	# only update the program counter's bottommost 28 bits.
	# NOTE: the semantically correct way to write the expression below would be
	# "(flushCacheInner & ~KSEG0_BASE) | KSEG1_BASE", but due to limitations
	# of the MIPS ELF format any expression that references a symbol (global
	# variable or function) may only add or subtract an integer offset to it,
	# with no other operators allowed.
	la    ptr, flushCacheInner + (KSEG1_BASE - KSEG0_BASE)
	jr    ptr
	lui   bccPtr, %hi(CPU_BCC)

.section .text.flushCacheInner, "ax", @progbits
.type    flushCacheInner, @function

flushCacheInner:
	# Start a critical section to prevent the CPU from trying to run the ISR
	# while memory is inaccessible.
	mfc0  savedStatus, COP0_STATUS
	mtc0  $0, COP0_STATUS

	# Save the current state of the BIU and cache configuration (BCC) register,
	# then prepare to access the instruction cache by making sure it is enabled
	# and entering "tag test" mode.
	lw    savedBCC, %lo(CPU_BCC)(bccPtr)
	li    status, COP0_STATUS_IsC
	ori   bcc, savedBCC, CPU_BCC_TAG | CPU_BCC_IS1
	sw    bcc, %lo(CPU_BCC)(bccPtr)

	# Set the "isolate cache" bit in the COP0 status register to rewire the
	# CPU's memory bus directly to the cache, allowing us to manually write to
	# tags. This disconnects main RAM and all memory-mapped I/O, so it must be
	# done after setting up BCC.
	mtc0  status, COP0_STATUS

	# Use an unrolled loop to clear all tags, thus invalidating the cache's
	# contents. In tag test mode each tag is 32 bits wide and mapped at the
	# address of its respective 16-byte cache line.
	li    ptr, 4096 - 16 * 16

.LclearLoop:
	sw    $0, ( 0 * 16)(ptr)
	sw    $0, ( 1 * 16)(ptr)
	sw    $0, ( 2 * 16)(ptr)
	sw    $0, ( 3 * 16)(ptr)
	sw    $0, ( 4 * 16)(ptr)
	sw    $0, ( 5 * 16)(ptr)
	sw    $0, ( 6 * 16)(ptr)
	sw    $0, ( 7 * 16)(ptr)
	sw    $0, ( 8 * 16)(ptr)
	sw    $0, ( 9 * 16)(ptr)
	sw    $0, (10 * 16)(ptr)
	sw    $0, (11 * 16)(ptr)
	sw    $0, (12 * 16)(ptr)
	sw    $0, (13 * 16)(ptr)
	sw    $0, (14 * 16)(ptr)
	sw    $0, (15 * 16)(ptr)

	bgtz  ptr, .LclearLoop
	addiu ptr, -(16 * 16)

	# Disable cache isolation to reinstate access to memory-mapped I/O and wait
	# for the change to take effect.
	mtc0  $0, COP0_STATUS
	nop

	# Restore the saved BCC state, terminate the critical section and return.
	sw    savedBCC, %lo(CPU_BCC)(bccPtr)
	jr    $ra
	mtc0  savedStatus, COP0_STATUS
