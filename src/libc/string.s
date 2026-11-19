# ps1-bare-metal - Copyright (C) 2023-2026 spicyjpeg
# SPDX-License-Identifier: MIT

# This file contains optimized implementations of memset() and memcpy() that
# make use of unrolled loops, Duff's device and unaligned load/store opcodes to
# fill large areas of memory much faster than a simple byte-by-byte loop would.

.set noreorder
.set noat

.set LARGE_FILL_THRESHOLD, 32
.set LARGE_FILL_DUFF_SIZE, 32
.set LARGE_COPY_THRESHOLD, 32
.set LARGE_COPY_DUFF_SIZE, 32

.set temp,      $at
.set destCopy,  $v0
.set remainder, $v1

.set dest,       $a0
.set value,      $a1 # memset()
.set source,     $a1 # memcpy()
.set length,     $a2
.set instLength, $a3 # memcpy()

.set value0,       $t0
.set value1,       $t1
.set value2,       $t2
.set value3,       $t3
.set value4,       $t4
.set value5,       $t5
.set value6,       $t6
.set value7,       $t7
.set loadJumpPtr,  $t8
.set storeJumpPtr, $t9

.section .text.memset, "ax", @progbits
.global  memset
.type    memset, @function

memset:
	# If more than 32 bytes have to be written then take the "large" path,
	# otherwise perform a byte-by-byte fill (which will typically end up faster
	# due to the loop being able to run entirely from the instruction cache).
	addiu temp, length, -LARGE_FILL_THRESHOLD
	bgtz  temp, .LlargeFill
	move  destCopy, dest

	blez  length, .LsmallFillDone
	nop

.LsmallFillLoop: # for (; length > 0; length--) {
	# *(dest++) = value;
	sb    value, 0(dest)
	addiu length, -1
	bgtz  length, .LsmallFillLoop
	addiu dest, 1

.LsmallFillDone: # }
	# return destCopy;
	jr    $ra
	nop

.LlargeFill:
	# Initialize fast filling by repeating the fill byte 4 times, so it can be
	# written 32 bits at a time.

	# value &= 0xff;
	# value |= value <<  8;
	# value |= value << 16;
	andi  value, 0xff
	sll   temp, value, 8
	or    value, temp
	sll   temp, value, 16
	or    value, temp

	# Fill the first 1-4 bytes quickly using an unaligned store.
	swr   value, 0(dest)

	# int filled = 4 - (dest % 4);
	# dest      += filled;
	# length    -= filled;
	andi  temp, dest, 3
	addiu temp, -4
	subu  dest, temp
	addu  length, temp

	# int remainder = length % 4;
	# length       -= remainder;
	andi  remainder, length, 3
	subu  length, remainder

	la    storeJumpPtr, .LlargeFillDuff

.LlargeFillLoop: # while (length > 0) {
	# If 32 bytes or more still have to be written, skip calculating the jump
	# offset and execute the whole block of sw opcodes. Otherwise, adjust the
	# destination pointer and jump into the middle of the block, using the
	# length as an index. No multiplication or division is needed as each
	# instruction is 4 bytes long and also writes 4 bytes.
	addiu length, -LARGE_FILL_DUFF_SIZE
	bgez  length, .LlargeFillDuff
	subu  temp, storeJumpPtr, length

	# dest -= LARGE_FILL_DUFF_SIZE - length;
	# goto &largeFillDuff[(LARGE_FILL_DUFF_SIZE - length) / 4 * 4];
	jr    temp
	addu  dest, length

.LlargeFillDuff:
	sw    value, (0 * 4)(dest)
	sw    value, (1 * 4)(dest)
	sw    value, (2 * 4)(dest)
	sw    value, (3 * 4)(dest)
	sw    value, (4 * 4)(dest)
	sw    value, (5 * 4)(dest)
	sw    value, (6 * 4)(dest)
	sw    value, (7 * 4)(dest)

	# length -= LARGE_FILL_DUFF_SIZE;
	# dest   += LARGE_FILL_DUFF_SIZE;
	# }
	bgtz  length, .LlargeFillLoop
	addiu dest, LARGE_FILL_DUFF_SIZE

	# Fill the remaining 1-4 bytes using another unaligned store.
	addu  dest, remainder

	# return destCopy;
	jr    $ra
	swl   value, -1(dest)

.section .text.memcpy, "ax", @progbits
.global  memcpy
.type    memcpy, @function

memcpy:
	# If more than 32 bytes have to be written then take the "large" path,
	# otherwise perform a byte-by-byte copy.
	addiu temp, length, -LARGE_COPY_THRESHOLD
	bgtz  temp, .LlargeCopy
	move  destCopy, dest

	blez  length, .LsmallCopyDone
	nop

.LsmallCopyLoop: # for (; length > 0; length--) {
	# *(dest++) = *(source++);
	lbu   value0, 0(source)
	addiu length, -1
	sb    value0, 0(dest)
	addiu source, 1
	bgtz  length, .LsmallCopyLoop
	addiu dest, 1

.LsmallCopyDone: # }
	# return destCopy;
	jr    $ra
	nop

.LlargeCopy:
	# Copy the first 4 bytes and realign the source pointer to the closest
	# 4-byte boundary.
	lwr   value0, 0(source)
	lwl   value0, 3(source)
	andi  temp, source, 3
	swr   value0, 0(dest)
	swl   value0, 3(dest)

	# int copied = 4 - (source % 4);
	# source    += copied;
	# dest      += copied;
	# length    -= copied;
	addiu temp, -4
	subu  source, temp
	subu  dest, temp
	addu  length, temp

	# int remainder = length % 4;
	# length       -= remainder;
	andi  remainder, length, 3
	subu  length, remainder

	# Check if the adjusted destination pointer is also aligned. If not, use the
	# alternate code block that performs unaligned stores in place of aligned
	# ones.

	# void *storeJumpPtr = (dest % 4)
	#     ? largeUnalignedStoreDuff
	#     : largeAlignedStoreDuff;
	# int  instLength    = (dest % 4) ? 8 : 4;
	la    loadJumpPtr, .LlargeLoadDuff
	addiu storeJumpPtr, loadJumpPtr, .LlargeAlignedStoreDuff - .LlargeLoadDuff
	andi  temp, dest, 3
	beqz  temp, .LlargeCopyLoop
	li    instLength, 0

	addiu storeJumpPtr, loadJumpPtr, .LlargeUnalignedStoreDuff - .LlargeLoadDuff
	li    instLength, 1

.LlargeCopyLoop: # while (length > 0) {
	# If 32 bytes or more still have to be written, skip calculating the jump
	# offset and execute the whole block of lw opcodes. Otherwise, adjust the
	# destination pointer and jump into the middle of the block, using the
	# length as an index.
	addiu length, -LARGE_COPY_DUFF_SIZE
	bgez  length, .LlargeLoadDuff
	subu  temp, loadJumpPtr, length

	# source -= LARGE_COPY_DUFF_SIZE - length;
	# goto &LlargeLoadDuff[(LARGE_COPY_DUFF_SIZE - length) / 4 * 4];
	jr    temp
	addu  source, length

.LlargeLoadDuff:
	lw    value0, (0 * 4)(source)
	lw    value1, (1 * 4)(source)
	lw    value2, (2 * 4)(source)
	lw    value3, (3 * 4)(source)
	lw    value4, (4 * 4)(source)
	lw    value5, (5 * 4)(source)
	lw    value6, (6 * 4)(source)
	lw    value7, (7 * 4)(source)

	# Do the same for the block of store instructions. An extra branch is needed
	# here for the >=32 byte case due to there being no conditional variant of
	# the jr instruction.
	bgez  length, .LjumpToLargeStoreDuff
	addiu source, LARGE_COPY_DUFF_SIZE

	# dest -= LARGE_COPY_DUFF_SIZE - length;
	# goto &storeJumpPtr[(LARGE_COPY_DUFF_SIZE - length) / 4 * instLength];
	sllv  temp, length, instLength
	subu  temp, storeJumpPtr, temp
	jr    temp
	addu  dest, length

.LjumpToLargeStoreDuff:
	jr    storeJumpPtr
	nop

.LlargeUnalignedStoreDuff:
	swr   value0, (0 * 4 + 0)(dest)
	swl   value0, (0 * 4 + 3)(dest)
	swr   value1, (1 * 4 + 0)(dest)
	swl   value1, (1 * 4 + 3)(dest)
	swr   value2, (2 * 4 + 0)(dest)
	swl   value2, (2 * 4 + 3)(dest)
	swr   value3, (3 * 4 + 0)(dest)
	swl   value3, (3 * 4 + 3)(dest)
	swr   value4, (4 * 4 + 0)(dest)
	swl   value4, (4 * 4 + 3)(dest)
	swr   value5, (5 * 4 + 0)(dest)
	swl   value5, (5 * 4 + 3)(dest)
	swr   value6, (6 * 4 + 0)(dest)
	swl   value6, (6 * 4 + 3)(dest)
	swr   value7, (7 * 4 + 0)(dest)
	swl   value7, (7 * 4 + 3)(dest)

	bgtz  length, .LlargeCopyLoop
	addiu dest, LARGE_COPY_DUFF_SIZE

	b     .LlargeCopyDone
	addu  source, remainder

.LlargeAlignedStoreDuff:
	sw    value0, (0 * 4)(dest)
	sw    value1, (1 * 4)(dest)
	sw    value2, (2 * 4)(dest)
	sw    value3, (3 * 4)(dest)
	sw    value4, (4 * 4)(dest)
	sw    value5, (5 * 4)(dest)
	sw    value6, (6 * 4)(dest)
	sw    value7, (7 * 4)(dest)

	# length -= LARGE_COPY_DUFF_SIZE;
	# source += LARGE_COPY_DUFF_SIZE;
	# dest   += LARGE_COPY_DUFF_SIZE;
	# }
	bgtz  length, .LlargeCopyLoop
	addiu dest, LARGE_COPY_DUFF_SIZE

	addu  source, remainder

.LlargeCopyDone:
	# Copy the last 4 bytes. This may end up overlapping the last word copied,
	# but it is still the fastest way to flush any remaining bytes.
	lwr   value0, -4(source)
	lwl   value0, -1(source)
	addu  dest, remainder
	swr   value0, -4(dest)

	# return destCopy;
	jr    $ra
	swl   value0, -1(dest)
