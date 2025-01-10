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

# libgcc provides two functions used internally by GCC to count the number of
# leading zeroes in a value, __clzsi2() (32-bit) and __clzdi2() (64-bit). We're
# going to override them with smaller implementations that make use of the GTE's
# LZCS/LZCR registers.

.set GTE_LZCS, $30 # Leading zero count input
.set GTE_LZCR, $31 # Leading zero count output

.section .text.__clzsi2, "ax", @progbits
.global __clzsi2
.type __clzsi2, @function

__clzsi2:
	# if (value & (1 << 31))
	#     return 0;
	mtc2  $a0, GTE_LZCS
	bltz  $a0, .Lreturn
	li    $v0, 0

	# else
	#     return countLeadingZeroes(value);
	mfc2  $v0, GTE_LZCR

.Lreturn:
	jr    $ra
	nop

.section .text.__clzdi2, "ax", @progbits
.global __clzdi2
.type __clzdi2, @function

__clzdi2:
	# if (msb & (1 << 31))
	#     return 0;
	mtc2  $a1, GTE_LZCS
	bltz  $a1, .Lreturn2
	li    $v0, 0

	# else if (msb)
	#     return countLeadingZeroes(msb);
	bnez  $a1, .LreturnMSB
	nop

.LnoMSB:
	# else if (lsb & (1 << 31))
	#     return 32;
	mtc2  $a0, GTE_LZCS
	bltz  $a0, .Lreturn2
	li    $v0, 32

	# else
	#     return 32 + countLeadingZeroes(lsb);
	mfc2  $v0, GTE_LZCR

	jr    $ra
	addiu $v0, 32

.LreturnMSB:
	mfc2  $v0, GTE_LZCR

.Lreturn2:
	jr    $ra
	nop
