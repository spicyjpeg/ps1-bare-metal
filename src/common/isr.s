# ps1-bare-metal - Copyright (C) 2023-2026 spicyjpeg
# SPDX-License-Identifier: MIT

.set noreorder
.set noat

.set COP0_CAUSE, $13
.set COP0_EPC,   $14

.set COP0_CAUSE_EXC_BITMASK, 31 << 2

.section .text.isrStub, "ax", @progbits
.global  isrStub
.type    isrStub, @function

isrStub:
	addiu $k0, $sp, -(20 + 4) * 4

	sw    $at, (0 * 4)($k0)
	sw    $v0, (1 * 4)($k0)
	sw    $v1, (2 * 4)($k0)
	sw    $a0, (3 * 4)($k0)
	sw    $a1, (4 * 4)($k0)
	j     isrBody
	sw    $a2, (5 * 4)($k0)

.section .text.isrBody, "ax", @progbits
.type    isrBody, @function

isrBody:
	sw    $a3, ( 6 * 4)($k0)
	sw    $t0, ( 7 * 4)($k0)
	sw    $t1, ( 8 * 4)($k0)
	sw    $t2, ( 9 * 4)($k0)
	sw    $t3, (10 * 4)($k0)
	sw    $t4, (11 * 4)($k0)
	sw    $t5, (12 * 4)($k0)
	sw    $t6, (13 * 4)($k0)
	sw    $t7, (14 * 4)($k0)
	sw    $t8, (15 * 4)($k0)
	sw    $t9, (16 * 4)($k0)
	sw    $ra, (17 * 4)($k0)

	mfhi  $v0
	mflo  $v1
	sw    $v0, (18 * 4)($k0)
	sw    $v1, (19 * 4)($k0)

	mfc0  $a0, COP0_CAUSE
	mfc0  $k1, COP0_EPC

	# uint32_t cause = cop0_getReg(COP0_CAUSE) & COP0_CAUSE_EXC_BITMASK;
	# if (cause == COP0_CAUSE_EXC_INT) {
	andi  $a0, COP0_CAUSE_EXC_BITMASK
	bnez  $a0, .LnotInterrupt
	lw    $v0, 0($k1)

	# const uint32_t *epc = (const uint32_t *) cop0_getReg(COP0_EPC);
	# if ((*epc >> 25) == (0x4a000000 >> 25))
	#     epc++;
	li    $v1, 0x4a000000 >> 25
	srl   $v0, 25
	bne   $v0, $v1, .LskipIncrement
	nop

	addiu $k1, 4

.LskipIncrement:
	jal   interruptHandler
	addiu $sp, -(4 + 20 + 4) * 4
	addiu $sp,  (4 + 20 + 4) * 4

	lw    $v0, (18 * 4)($k0)
	lw    $v1, (19 * 4)($k0)
	mthi  $v0
	mtlo  $v1

	lw    $at, ( 0 * 4)($k0)
	lw    $v0, ( 1 * 4)($k0)
	lw    $v1, ( 2 * 4)($k0)
	lw    $a0, ( 3 * 4)($k0)
	lw    $a1, ( 4 * 4)($k0)
	lw    $a2, ( 5 * 4)($k0)
	lw    $a3, ( 6 * 4)($k0)
	lw    $t0, ( 7 * 4)($k0)
	lw    $t1, ( 8 * 4)($k0)
	lw    $t2, ( 9 * 4)($k0)
	lw    $t3, (10 * 4)($k0)
	lw    $t4, (11 * 4)($k0)
	lw    $t5, (12 * 4)($k0)
	lw    $t6, (13 * 4)($k0)
	lw    $t7, (14 * 4)($k0)
	lw    $t8, (15 * 4)($k0)
	lw    $t9, (16 * 4)($k0)
	lw    $ra, (17 * 4)($k0)

	jr    $k1
	rfe

.LnotInterrupt: # } else {
	# Extend the buffer on the stack and save EPC plus the registers we
	# previously skipped, so that otherExceptionHandler() can dump them before
	# locking up. Since the pointer to the extended buffer is stored in $a1,
	# otherExceptionHandler() will receive it as its second argument alongside
	# the cause code in $a0.
	addiu $a1, $k0, -12 * 4
	sw    $s0, ( 0 * 4)($a1)
	sw    $s1, ( 1 * 4)($a1)
	sw    $s2, ( 2 * 4)($a1)
	sw    $s3, ( 3 * 4)($a1)
	sw    $s4, ( 4 * 4)($a1)
	sw    $s5, ( 5 * 4)($a1)
	sw    $s6, ( 6 * 4)($a1)
	sw    $s7, ( 7 * 4)($a1)
	sw    $gp, ( 8 * 4)($a1)
	sw    $sp, ( 9 * 4)($a1)
	sw    $fp, (10 * 4)($a1)
	sw    $k1, (11 * 4)($a1)

	# otherExceptionHandler(cause >> 2, registers);
	srl   $a0, 2
	j     otherExceptionHandler
	addiu $sp, -(4 + 12 + 20 + 4) * 4
	# }
