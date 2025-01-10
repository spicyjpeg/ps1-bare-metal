# ps1-bare-metal - (C) 2023-2025 spicyjpeg
#
# Permission to use, copy, modify, and/or distribute this software for any
# purpose with or without fee is hereby granted, provided that the above
# copyright notice and this permission notice appear in all copies.
#
# THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES WITH
# REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY
# AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY SPECIAL, DIRECT,
# INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM
# LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR
# OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
# PERFORMANCE OF THIS SOFTWARE.

.set noreorder

# This is a minimal implementation of a function to flush the CPU's instruction
# cache. While a similar function is already present in the BIOS ROM, this
# version is several orders of magnitude faster as it only clears the tags (the
# BIOS implementation unnecessarily zeroes out the contents of every cache line)
# and is not subject to the waitstates imposed by the ROM's 8-bit bus.

.set KSEG1_BASE, 0xa000
.set CPU_BASE,   0xfffe

.set CPU_BCC,     0x130
.set CPU_BCC_TAG, 1 <<  2
.set CPU_BCC_DS,  1 <<  7
.set CPU_BCC_IS1, 1 << 11

.set COP0_STATUS,     $12
.set COP0_STATUS_IsC, 1 << 16

.set ptr,         $a0
.set temp,        $a1
.set savedStatus, $a2
.set savedBCC,    $a3

.section .text.flushCache, "ax", @progbits
.global flushCache
.type flushCache, @function

flushCache:
	# Call _flushCacheInner() through the uncached KSEG1 mirror of main RAM,
	# ensuring the CPU will not attempt to use the cache while it is being
	# cleared. This jump must be performed using a la/jr combo as immediate
	# jumps (j) only update the program counter's bottommost 28 bits.
	la    ptr, _flushCacheInner
	lui   temp, KSEG1_BASE
	or    ptr, temp

	jr    ptr
	lui   ptr, CPU_BASE

.section .text._flushCacheInner, "ax", @progbits
.type _flushCacheInner, @function

_flushCacheInner:
	# Save the current state of the BCC and COP0 status registers so that they
	# can be restored later.
	mfc0  savedStatus, COP0_STATUS
	lw    savedBCC, CPU_BCC(ptr)

	# Disable interrupts and the scratchpad, put the instruction cache into "tag
	# test" mode and proceed to map it directly to the CPU's address space by
	# setting the COP0 "isolate cache" flag.
	mtc0  $0, COP0_STATUS

	# CPU_BCC = (CPU_BCC & ~CPU_BCC_DS) | CPU_BCC_TAG | CPU_BCC_IS1;
	li    temp, ~CPU_BCC_DS
	and   temp, savedBCC
	ori   temp, CPU_BCC_TAG | CPU_BCC_IS1
	sw    temp, CPU_BCC(ptr)

	li    temp, COP0_STATUS_IsC
	mtc0  temp, COP0_STATUS

	# Use an unrolled loop to clear all tags, thus invalidating the cache's
	# contents. "Tag test" mode maps each tag into memory at the offset of its
	# respective 16-byte cache line.
	li    temp, 0x1000 - 256

.LclearLoop: # for (int i = 0x1000 - 256; i >= 0; i -= 256) {
	sw    $0, 0x00(temp)
	sw    $0, 0x10(temp)
	sw    $0, 0x20(temp)
	sw    $0, 0x30(temp)
	sw    $0, 0x40(temp)
	sw    $0, 0x50(temp)
	sw    $0, 0x60(temp)
	sw    $0, 0x70(temp)
	sw    $0, 0x80(temp)
	sw    $0, 0x90(temp)
	sw    $0, 0xa0(temp)
	sw    $0, 0xb0(temp)
	sw    $0, 0xc0(temp)
	sw    $0, 0xd0(temp)
	sw    $0, 0xe0(temp)
	sw    $0, 0xf0(temp)

	# }
	bgtz  temp, .LclearLoop
	addiu temp, -256

	# Clear the "isolate cache" bit, restore the previously saved state and
	# return.
	mtc0  $0, COP0_STATUS
	nop
	sw    savedBCC, CPU_BCC(ptr)
	mtc0  savedStatus, COP0_STATUS

	jr    $ra
	nop
