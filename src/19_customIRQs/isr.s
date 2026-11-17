# ps1-bare-metal - Copyright (C) 2023-2026 spicyjpeg
# SPDX-License-Identifier: MIT

# When executing a jump, call or branch instruction, a MIPS processor will
# always execute the instruction that immediately follows it before jumping.
# This behavior creates a "delay slot" after each of those instructions, which
# must be filled by reordering nearby code or otherwise with a nop. The
# assembler fills delay slots automatically by default, but we are going to opt
# out using the .set noreorder directive in order to get precise control over
# what executes when in our ISR.
.set noreorder

# Similarly, some MIPS instructions are not implemented in hardware and instead
# assemble to equivalent sequences of multiple instructions, some of which may
# clobber a CPU register reserved for this purpose: $at. Our ISR needs to
# manipulate $at directly, but the assembler will only let us access it if we
# use the .set noat directive to forbid use of pseudo-instructions that depend
# on it.
.set noat

# .set can also be used to define simple macros. As there is no way to use C
# headers from assembly, we'll duplicate some COP0 definitions we need from
# ps1/cop0.h.
.set COP0_CAUSE, $13
.set COP0_EPC,   $14

.set COP0_CAUSE_EXC_BITMASK, 31 << 2

# Declare isrStub() as a global function (visible to main.c) and place it in
# the .text section that holds all code in the executable.
.section .text.isrStub, "ax", @progbits
.global  isrStub
.type    isrStub, @function

isrStub:
	# Once this function's body is relocated to 0x80000080 in RAM, the CPU will
	# disable interrupts and jump to it whenever any exception occurs. We need
	# to be completely invisible to code that gets interrupted by the jump, so
	# we cannot modify any CPU register without restoring it afterwards. To
	# satisfy this requirement we are going to temporarily "borrow" the stack of
	# the interrupted function and push three sections onto it:
	# - a 4-word safety margin to avoid corrupting any data saved below the
	#   stack pointer;
	# - a 22-word buffer for the state of all registers we might clobber. This
	#   includes $at, return values, arguments and temporaries, but not saved
	#   registers ($s0-$s7 and $fp) all function calls are already required to
	#   preserve;
	# - a 4-word "argument" section for use as temporary storage by functions we
	#   call, as specified by the MIPS calling convention.
	addiu $sp, -(4 + 22 + 4) * 4

	# We only have 8 instructions' worth of space in kernel RAM for our copy of
	# isrStub(). We'll save the first few registers, then jump to isrBody() in
	# our executable (see below) and let it save the rest. Notice how $a2 is
	# saved in the jump's delay slot.
	sw    $at, (4 * 4)($sp)
	sw    $v0, (5 * 4)($sp)
	sw    $v1, (6 * 4)($sp)
	sw    $a0, (7 * 4)($sp)
	sw    $a1, (8 * 4)($sp)
	j     isrBody
	sw    $a2, (9 * 4)($sp)

.section .text.isrBody, "ax", @progbits
.type    isrBody, @function

isrBody:
	sw    $a3, (10 * 4)($sp)
	sw    $t0, (11 * 4)($sp)
	sw    $t1, (12 * 4)($sp)
	sw    $t2, (13 * 4)($sp)
	sw    $t3, (14 * 4)($sp)
	sw    $t4, (15 * 4)($sp)
	sw    $t5, (16 * 4)($sp)
	sw    $t6, (17 * 4)($sp)
	sw    $t7, (18 * 4)($sp)
	sw    $t8, (19 * 4)($sp)
	sw    $t9, (20 * 4)($sp)
	sw    $sp, (21 * 4)($sp)
	sw    $ra, (22 * 4)($sp)

	# Save the multiplier's result registers. Saving them last gives the
	# multiplier enough time to finish any pending computation in the background
	# without stalling the ISR.
	mfhi  $v0
	mflo  $v1
	sw    $v0, (23 * 4)($sp)
	sw    $v1, (24 * 4)($sp)

	# Obtain information about the exception from the COP0 cause register and a
	# pointer to the interrupted instruction from the exception program counter
	# (EPC) register. Like jumps, memory and coprocessor reads have a delay
	# slot: any instruction following one will still see the previous register
	# value. Since the next step will access $v0, the EPC read doubles as a wait
	# for it to be ready.
	mfc0  $v0, COP0_CAUSE
	mfc0  $v1, COP0_EPC

	# Check cause register bits 6:2 to determine what triggered the exception. A
	# value of zero represents an interrupt while other values are used to
	# indicate an invalid memory access, unaligned pointers and so on. $v1 will
	# have loaded by the time the branch executes, so we can fetch the
	# instruction at EPC in the branch's delay slot in preparation for the next
	# step.
	andi  $v0, COP0_CAUSE_EXC_BITMASK
	bnez  $v0, .LnotInterrupt
	lw    $a0, 0($v1)

	# Due to a hardware bug in the CPU, EPC will "lag behind" the interrupted
	# instruction if it followed a GTE command. If the exception was caused by
	# an interrupt, work around the bug by manually checking if EPC points to a
	# COP2 opcode (identifiable by its topmost 7 bits) and incrementing it as
	# needed. The first instruction here doubles once again as a delay to give
	# $a0 time to update.
	li    $a1, 0x4a000000 >> 25
	srl   $a0, 25
	bne   $a0, $a1, .LskipIncrement
	nop

	addiu $v1, 4

.LskipIncrement:
	# Invoke the interrupt handler defined in main.c, using the delay slot to
	# save the adjusted EPC to the stack first.
	jal   interruptHandler
	sw    $v1, (25 * 4)($sp)

	# Restore the initial state of all registers once the handler returns.
	lw    $v0, (23 * 4)($sp)
	lw    $v1, (24 * 4)($sp)
	mthi  $v0
	mtlo  $v1

	lw    $at, ( 4 * 4)($sp)
	lw    $v0, ( 5 * 4)($sp)
	lw    $v1, ( 6 * 4)($sp)
	lw    $a0, ( 7 * 4)($sp)
	lw    $a1, ( 8 * 4)($sp)
	lw    $a2, ( 9 * 4)($sp)
	lw    $a3, (10 * 4)($sp)
	lw    $t0, (11 * 4)($sp)
	lw    $t1, (12 * 4)($sp)
	lw    $t2, (13 * 4)($sp)
	lw    $t3, (14 * 4)($sp)
	lw    $t4, (15 * 4)($sp)
	lw    $t5, (16 * 4)($sp)
	lw    $t6, (17 * 4)($sp)
	lw    $t7, (18 * 4)($sp)
	lw    $t8, (19 * 4)($sp)
	lw    $t9, (20 * 4)($sp)
	lw    $sp, (21 * 4)($sp)
	lw    $ra, (22 * 4)($sp)

	# Undo our stack allocations, reinstate COP0 interrupt handling via the
	# "return from exception" instruction and jump to the previously saved EPC
	# to resume main loop execution. The only registers we may safely clobber at
	# this point are $k0 and $k1, which the MIPS calling convention reserves for
	# use exclusively by the ISR.
	lw    $k0, (25 * 4)($sp)
	addiu $sp, (4 + 22 + 4) * 4
	jr    $k0
	rfe

.LnotInterrupt:
	# Exceptions other than interrupts are typically caused by faulty code or
	# memory corruption. The safest way to handle them is by aborting program
	# execution entirely, in this case by entering an endless loop.
	b     .LnotInterrupt
	nop
