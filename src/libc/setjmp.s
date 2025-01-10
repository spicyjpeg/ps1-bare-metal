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

# This is not a "proper" implementation of setjmp/longjmp as it does not save
# COP0 and GTE registers, but it is good enough for most use cases.

.section .text.setjmp, "ax", @progbits
.global setjmp
.type setjmp, @function

setjmp:
	sw    $ra, 0x00($a0)
	sw    $s0, 0x04($a0)
	sw    $s1, 0x08($a0)
	sw    $s2, 0x0c($a0)
	sw    $s3, 0x10($a0)
	sw    $s4, 0x14($a0)
	sw    $s5, 0x18($a0)
	sw    $s6, 0x1c($a0)
	sw    $s7, 0x20($a0)
	sw    $gp, 0x24($a0)
	sw    $sp, 0x28($a0)
	sw    $fp, 0x2c($a0)

	# return 0;
	jr    $ra
	li    $v0, 0

.section .text.longjmp, "ax", @progbits
.global longjmp
.type longjmp, @function

longjmp:
	lw    $ra, 0x00($a0)
	lw    $s0, 0x04($a0)
	lw    $s1, 0x08($a0)
	lw    $s2, 0x0c($a0)
	lw    $s3, 0x10($a0)
	lw    $s4, 0x14($a0)
	lw    $s5, 0x18($a0)
	lw    $s6, 0x1c($a0)
	lw    $s7, 0x20($a0)
	lw    $gp, 0x24($a0)
	lw    $sp, 0x28($a0)
	lw    $fp, 0x2c($a0)

	# return status;
	jr    $ra
	move  $v0, $a1
