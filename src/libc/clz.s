# ps1-bare-metal - Copyright (C) 2023-2026 spicyjpeg
# SPDX-License-Identifier: MIT

# GCC calls two functions to count the number of leading zeroes in an integer:
# __clzsi2() (32-bit) and __clzdi2() (64-bit). Both are normally provided by
# libgcc, but we can replace them with much smaller implementations that
# leverage the GTE's LZCS/LZCR registers. Note that the GTE must be enabled in
# order for these to work; our startup code takes care of that.

.set noreorder

.set GTE_LZCS, $30 # Leading zero count input
.set GTE_LZCR, $31 # Leading zero count output

.section .text.__clzsi2, "ax", @progbits
.global  __clzsi2
.type    __clzsi2, @function

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
.global  __clzdi2
.type    __clzdi2, @function

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
