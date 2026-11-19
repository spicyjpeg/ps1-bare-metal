# ps1-bare-metal - Copyright (C) 2023-2026 spicyjpeg
# SPDX-License-Identifier: MIT

# This is not a completely safe implementation of setjmp() and longjmp() as it
# does not save COP0 and GTE registers, but it is compatible with the BIOS
# kernel's implementation and good enough for most use cases.

.set noreorder

.section .text.setjmp, "ax", @progbits
.global  setjmp
.type    setjmp, @function

setjmp:
	sw    $ra, ( 0 * 4)($a0)
	sw    $sp, ( 1 * 4)($a0)
	sw    $fp, ( 2 * 4)($a0)
	sw    $s0, ( 3 * 4)($a0)
	sw    $s1, ( 4 * 4)($a0)
	sw    $s2, ( 5 * 4)($a0)
	sw    $s3, ( 6 * 4)($a0)
	sw    $s4, ( 7 * 4)($a0)
	sw    $s5, ( 8 * 4)($a0)
	sw    $s6, ( 9 * 4)($a0)
	sw    $s7, (10 * 4)($a0)
	sw    $gp, (11 * 4)($a0)

	# return 0;
	jr    $ra
	li    $v0, 0

.section .text.longjmp, "ax", @progbits
.global  longjmp
.type    longjmp, @function

longjmp:
	lw    $ra, ( 0 * 4)($a0)
	lw    $sp, ( 1 * 4)($a0)
	lw    $fp, ( 2 * 4)($a0)
	lw    $s0, ( 3 * 4)($a0)
	lw    $s1, ( 4 * 4)($a0)
	lw    $s2, ( 5 * 4)($a0)
	lw    $s3, ( 6 * 4)($a0)
	lw    $s4, ( 7 * 4)($a0)
	lw    $s5, ( 8 * 4)($a0)
	lw    $s6, ( 9 * 4)($a0)
	lw    $s7, (10 * 4)($a0)
	lw    $gp, (11 * 4)($a0)

	# return status;
	jr    $ra
	move  $v0, $a1
