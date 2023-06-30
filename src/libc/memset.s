# ps1-bare-metal - (C) 2023 spicyjpeg
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

# This is an optimized implementation of memset() that makes use of Duff's
# device and unaligned store instructions to fill large areas of memory much
# faster than a simple byte-by-byte loop would.

.section .text.memset, "ax", @progbits
.global memset
.type memset, @function

memset:
	# If more than 16 bytes have to be written then take the "large" path,
	# otherwise use the code below.
	addiu $t0, $a2, -16
	bgtz  $t0, .LlargeFill
	move  $v0, $a0 # returnValue = dest

	# Jump to one of the sb opcodes below. This is basically a cut-down Duff's
	# device implementation with no looping.
	la    $t0, .LsmallDuff + 0x40 # addr = &smallDuff[(16 - count) * 4]
	sll   $t1, $a2, 2
	subu  $t0, $t1
	addu  $a0, $a2 # dest -= 16 - count
	jr    $t0
	addiu $a0, -16

.LsmallDuff:
	sb    $a1, 0x0($a0)
	sb    $a1, 0x1($a0)
	sb    $a1, 0x2($a0)
	sb    $a1, 0x3($a0)
	sb    $a1, 0x4($a0)
	sb    $a1, 0x5($a0)
	sb    $a1, 0x6($a0)
	sb    $a1, 0x7($a0)
	sb    $a1, 0x8($a0)
	sb    $a1, 0x9($a0)
	sb    $a1, 0xa($a0)
	sb    $a1, 0xb($a0)
	sb    $a1, 0xc($a0)
	sb    $a1, 0xd($a0)
	sb    $a1, 0xe($a0)
	sb    $a1, 0xf($a0)
	jr    $ra
	nop

.LlargeFill:
	# Initialize fast filling by repeating the fill byte 4 times, so it can be
	# written 32 bits at a time.
	andi  $a1, 0xff # ch &= 0xff
	sll   $t0, $a1, 8 # ch |= (ch << 8) | (ch << 16) | (ch << 24)
	or    $a1, $t0
	sll   $t0, $a1, 16
	or    $a1, $t0

	# Fill the first 1-4 bytes (here the swr instruction does all the magic)
	# and update dest and count accordingly.
	swr   $a1, 0($a0)
	andi  $t0, $a0, 3 # align = 4 - (dest % 4)
	addiu $t0, -4
	addu  $a2, $t0 # count -= align
	subu  $a0, $t0 # dest += align

	la    $t1, .LlargeDuff
	andi  $t2, $a2, 3 # remainder = count % 4
	subu  $a2, $t2 # count -= remainder

.LlargeFillLoop:
	# If 128 bytes or more still have to be written, skip calculating the jump
	# offset and execute the whole block of sw opcodes.
	addiu $a2, -0x80 # count -= 0x80
	bgez  $a2, .LlargeDuff
	#nop

	# Jump to one of the sw opcodes below. This is the "full" Duff's device.
	subu  $t0, $t1, $a2 # addr = &largeDuff[0x80 - (count + 0x80)]
	jr    $t0
	addu  $a0, $a2 # dest -= 0x80 - (count + 0x80)

.LlargeDuff:
	sw    $a1, 0x00($a0)
	sw    $a1, 0x04($a0)
	sw    $a1, 0x08($a0)
	sw    $a1, 0x0c($a0)
	sw    $a1, 0x10($a0)
	sw    $a1, 0x14($a0)
	sw    $a1, 0x18($a0)
	sw    $a1, 0x1c($a0)
	sw    $a1, 0x20($a0)
	sw    $a1, 0x24($a0)
	sw    $a1, 0x28($a0)
	sw    $a1, 0x2c($a0)
	sw    $a1, 0x30($a0)
	sw    $a1, 0x34($a0)
	sw    $a1, 0x38($a0)
	sw    $a1, 0x3c($a0)
	sw    $a1, 0x40($a0)
	sw    $a1, 0x44($a0)
	sw    $a1, 0x48($a0)
	sw    $a1, 0x4c($a0)
	sw    $a1, 0x50($a0)
	sw    $a1, 0x54($a0)
	sw    $a1, 0x58($a0)
	sw    $a1, 0x5c($a0)
	sw    $a1, 0x60($a0)
	sw    $a1, 0x64($a0)
	sw    $a1, 0x68($a0)
	sw    $a1, 0x6c($a0)
	sw    $a1, 0x70($a0)
	sw    $a1, 0x74($a0)
	sw    $a1, 0x78($a0)
	sw    $a1, 0x7c($a0)

	bgtz  $a2, .LlargeFillLoop
	addiu $a0, 0x80 # dest += 0x80

	# Fill the remaining 1-4 bytes, using (again) an unaligned store.
	addu  $a0, $t2 # lastByte = dest + remainder - 1
	jr    $ra
	swl   $a1, -1($a0)
