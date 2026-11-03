# ps1-bare-metal - Copyright (C) 2023-2026 spicyjpeg
# SPDX-License-Identifier: MIT

.set noreorder

.macro KERNEL_FUNC entryPoint, id, name
	.section .text.\name, "ax", @progbits
	.global  \name
	.type    \name, @function

	\name:
		# Kernel APIs are invoked by calling one of the stubs the kernel places
		# at 0xa0, 0xb0 and 0xc0 in RAM, passing the function number in $t1 and
		# any arguments in $a0-$a3. The latter are already in place here per the
		# MIPS calling convention, so we can use a direct jump (tail call) to
		# the stub and set $t1 in its delay slot.
		j     \entryPoint
		li    $t1, \id
.endm

KERNEL_FUNC 0x800000a0, 0x3f, kernel_printf
KERNEL_FUNC 0x800000a0, 0x44, kernel_flushCache

KERNEL_FUNC 0x800000b0, 0x17, kernel_returnFromException
KERNEL_FUNC 0x800000b0, 0x18, kernel_resetEntryInt
KERNEL_FUNC 0x800000b0, 0x19, kernel_hookEntryInt
KERNEL_FUNC 0x800000b0, 0x34, kernel_read
KERNEL_FUNC 0x800000b0, 0x35, kernel_write
KERNEL_FUNC 0x800000b0, 0x37, kernel_ioctl
KERNEL_FUNC 0x800000b0, 0x51, kernel_krom2RawAdd
KERNEL_FUNC 0x800000b0, 0x5b, kernel_changeClearPad

KERNEL_FUNC 0x800000c0, 0x0a, kernel_changeClearRCnt
