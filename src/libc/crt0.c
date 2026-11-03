/*
 * ps1-bare-metal - (C) 2023-2026 spicyjpeg
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

#include <stddef.h>
#include <stdint.h>
#include "ps1/cop0.h"

typedef void (*Function)(void);

/* Linker symbols */

// These are defined by the linker script. Note that these are not variables,
// they are virtual symbols whose location matches their value. The simplest way
// to turn them into pointers is to declare them as arrays.
extern char _bssStart[], _bssEnd[];

extern const Function _initArrayStart[], _initArrayEnd[];
extern const Function _finiArrayStart[], _finiArrayEnd[];

/* Heap API (used by malloc) */

static uintptr_t heapEnd   = (uintptr_t) _bssEnd;
static uintptr_t heapLimit = 0x80200000; // TODO: add a way to change this

void *sbrk(ptrdiff_t incr) {
	uintptr_t end    = heapEnd;
	uintptr_t newEnd = heapEnd + incr + 7;
	newEnd          -= newEnd % 8;

	if (newEnd >= heapLimit)
		return 0;

	heapEnd = newEnd;
	return (void *) end;
}

/* Program entry point */

int main(int argc, const char **argv);

int _start(int argc, const char **argv) {
	// Set $gp to point to the middle of the .sdata/.sbss sections, ensuring
	// variables placed in those sections can be quickly accessed. See the
	// linker script for more details.
	__asm__ volatile("la $gp, _gp\n");

	// Ensure the GTE is accessible in order for the intrinsics defined in clz.s
	// to work properly even if main() does not initialize it.
	cop0_setReg(COP0_STATUS, cop0_getReg(COP0_STATUS) | COP0_STATUS_CU2);

	// Set all uninitialized variables to zero by clearing the BSS section.
	__builtin_memset(_bssStart, 0, _bssEnd - _bssStart);

	// Invoke all global constructors if any, then main() and finally all global
	// destructors.
	for (const Function *ctor = _initArrayStart; ctor < _initArrayEnd; ctor++)
		(*ctor)();

	int returnValue = main(argc, argv);

	for (const Function *dtor = _finiArrayStart; dtor < _finiArrayEnd; dtor++)
		(*dtor)();

	return returnValue;
}
