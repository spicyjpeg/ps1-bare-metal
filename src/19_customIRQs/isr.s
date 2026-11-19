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

.set COP0_CAUSE_EXC_BITMASK, (31 << 2)

# Declare isrStub() as a global function (visible to main.c) and place it in
# the .text section that holds all code in the executable.
.section .text.isrStub, "ax", @progbits
.global  isrStub
.type    isrStub, @function

isrStub:
	# Once the stub's body is relocated to 0x80000080 in RAM, the CPU will
	# disable interrupts and jump to it whenever any exception occurs. We need
	# to be completely invisible to code that gets interrupted by the jump, so
	# we cannot modify most CPU registers without restoring them afterwards. To
	# satisfy this requirement we are going to temporarily "borrow" the stack of
	# the interrupted function and push two sections onto it:
	# - a 4-word safety margin to avoid corrupting any data saved below the
	#   stack pointer;
	# - a 20-word buffer for the state of all registers we might clobber,
	#   including $at, return values, arguments and temporaries but not saved
	#   registers ($s0-$s7 and $fp) all function calls are already required to
	#   preserve.
	# The only registers we are allowed to overwrite freely $k0 and $k1, which
	# the MIPS calling convention reserves for use exclusively by the ISR. We'll
	# start by setting up $k0 to point to the buffer.
	addiu $k0, $sp, -(20 + 4) * 4

	# We only have 8 instructions' worth of space in kernel RAM for our copy of
	# isrStub(). We'll save the first few registers, then jump to isrBody() in
	# our executable and let it save the rest. Notice how $a2 is saved in the
	# jump's delay slot.
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

	# Save the multiplier's result registers. Saving them last gives the
	# multiplier enough time to finish any pending computation in the background
	# without stalling the ISR.
	mfhi  $v0
	mflo  $v1
	sw    $v0, (18 * 4)($k0)
	sw    $v1, (19 * 4)($k0)

	# Obtain information about the exception from the COP0 cause register and a
	# pointer to the interrupted instruction from the exception program counter
	# (EPC) register. Like jumps, memory and coprocessor reads have a delay
	# slot: any instruction following one will still see the previous register
	# value. Since the next step will access $v0, the EPC read doubles as a wait
	# for it to be ready.
	mfc0  $a0, COP0_CAUSE
	mfc0  $k1, COP0_EPC

	# Check cause register bits 6:2 to determine what triggered the exception. A
	# value of zero represents an interrupt while other values are used to
	# indicate an invalid memory access, unaligned pointers and so on. $v1 will
	# have loaded by the time the branch executes, so we can fetch the
	# instruction at EPC in the branch's delay slot in preparation for the next
	# step.
	andi  $a0, COP0_CAUSE_EXC_BITMASK
	bnez  $a0, .LnotInterrupt
	lw    $v0, 0($k1)

	# Due to a hardware bug in the CPU, EPC will "lag behind" the interrupted
	# instruction if it followed a GTE command. If the exception was caused by
	# an interrupt, work around the bug by manually checking if EPC points to a
	# COP2 opcode (identifiable by its topmost 7 bits) and incrementing it as
	# needed; the adjusted EPC is stored in $k1 for later use. The first
	# instruction here doubles once again as a delay to give $a0 time to update.
	li    $v1, 0x4a000000 >> 25
	srl   $v0, 25
	bne   $v0, $v1, .LskipIncrement
	nop

	addiu $k1, 4

.LskipIncrement:
	# Invoke the interrupt handler defined in main.c. Before doing so we must
	# move the yet-untouched stack pointer below our buffer, then allocate an
	# additional 4-word "argument" section (even when passing no arguments) as
	# specified by the MIPS calling convention.
	jal   interruptHandler
	addiu $sp, -(4 + 20 + 4) * 4

	# Restore the initial stack pointer and all previously saved registers. This
	# assumes interruptHandler() did not clobber $k0 nor $k1, which is a
	# reasonable assumption.
	addiu $sp, (4 + 20 + 4) * 4

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

	# Reinstate COP0 interrupt handling via the "return from exception"
	# instruction and jump to the adjusted EPC to resume main loop execution.
	jr    $k1
	rfe

.LnotInterrupt:
	# Exceptions other than interrupts are typically caused by faulty code or
	# memory corruption. The safest way to handle them is by aborting program
	# execution entirely, in this case by entering an endless loop.
	b     .LnotInterrupt
	nop
