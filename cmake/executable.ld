/*
 * ps1-bare-metal - (C) 2023 spicyjpeg
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES WITH
 * REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY
 * AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY SPECIAL, DIRECT,
 * INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM
 * LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR
 * OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 * PERFORMANCE OF THIS SOFTWARE.
 */

OUTPUT_FORMAT(elf32-littlemips)
ENTRY(_start)

MEMORY {
	KERNEL_RAM (rwx) : ORIGIN = 0x80000000, LENGTH = 0x010000
	APP_RAM    (rwx) : ORIGIN = 0x80010000, LENGTH = 0x7f0000
}

SECTIONS {
	/* Code sections */

	.text : {
		_textStart = .;

		*(.text .text.* .gnu.linkonce.t.*)
		*(.plt .MIPS.stubs)
	} > APP_RAM

	.rodata : {
		*(.rodata .rodata.* .gnu.linkonce.r.*)

		_textEnd = .;
	} > APP_RAM

	/* Global constructor/destructor arrays */

	.preinit_array : {
		. = ALIGN((. != 0) ? 4 : 1);
		_preinitArrayStart = .;

		KEEP(*(.preinit_array))

		_preinitArrayEnd = .;
	} > APP_RAM

	.init_array : {
		. = ALIGN((. != 0) ? 4 : 1);
		_initArrayStart = .;

		KEEP(*(SORT(.init_array.*) SORT(.ctors.*)))
		KEEP(*(.init_array .ctors))

		_initArrayEnd = .;
	} > APP_RAM

	.fini_array : {
		. = ALIGN((. != 0) ? 4 : 1);
		_finiArrayStart = .;

		KEEP(*(.fini_array .dtors))
		KEEP(*(SORT(.fini_array.*) SORT(.dtors.*)))

		_finiArrayEnd = .;
	} > APP_RAM

	/* Data sections */

	.data : {
		*(.data .data.* .gnu.linkonce.d.*)
	} > APP_RAM

	/*
	 * Set _gp (copied to $gp) to point to the beginning of .sdata plus 0x8000,
	 * so anything within .sdata and .sbss can be accessed using the $gp
	 * register as base plus a signed 16-bit immediate.
	 */
	.sdata : {
		_gp = ALIGN(16) + 0x7ff0;

		*(.sdata .sdata.* .gnu.linkonce.s.*)
	} > APP_RAM

	.sbss (NOLOAD) : {
		. = ALIGN((. != 0) ? 4 : 1);
		_bssStart = .;

		*(.sbss .sbss.* .gnu.linkonce.sb.*)
		*(.scommon)
	} > APP_RAM

	.bss (NOLOAD) : {
		*(.bss .bss.* .gnu.linkonce.b.*)
		*(COMMON)

		. = ALIGN((. != 0) ? 4 : 1);
		_bssEnd = .;
	} > APP_RAM

	/* Dummy sections */

	.dummy (NOLOAD) : {
		KEEP(*(.dummy))
	} > APP_RAM

	/DISCARD/ : {
		*(.gnu.lto_*)
	}
}
