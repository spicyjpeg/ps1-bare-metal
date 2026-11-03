# ps1-bare-metal - Copyright (C) 2023-2026 spicyjpeg
# SPDX-License-Identifier: MIT

# This is not a "proper" implementation of setjmp/longjmp as it does not save
# COP0 and GTE registers, but it is compatible with the BIOS kernel's
# implementation and good enough for most use cases.

.set noreorder

.section .text.setjmp, "ax", @progbits
.global  setjmp
.type    setjmp, @function

setjmp:
	sw    $ra, 0x00($a0)
	sw    $sp, 0x04($a0)
	sw    $fp, 0x08($a0)
	sw    $s0, 0x0c($a0)
	sw    $s1, 0x10($a0)
	sw    $s2, 0x14($a0)
	sw    $s3, 0x18($a0)
	sw    $s4, 0x1c($a0)
	sw    $s5, 0x20($a0)
	sw    $s6, 0x24($a0)
	sw    $s7, 0x28($a0)
	sw    $gp, 0x2c($a0)

	# return 0;
	jr    $ra
	li    $v0, 0

.section .text.longjmp, "ax", @progbits
.global  longjmp
.type    longjmp, @function

longjmp:
	lw    $ra, 0x00($a0)
	lw    $sp, 0x04($a0)
	lw    $fp, 0x08($a0)
	lw    $s0, 0x0c($a0)
	lw    $s1, 0x10($a0)
	lw    $s2, 0x14($a0)
	lw    $s3, 0x18($a0)
	lw    $s4, 0x1c($a0)
	lw    $s5, 0x20($a0)
	lw    $s6, 0x24($a0)
	lw    $s7, 0x28($a0)
	lw    $gp, 0x2c($a0)

	# return status;
	jr    $ra
	move  $v0, $a1
