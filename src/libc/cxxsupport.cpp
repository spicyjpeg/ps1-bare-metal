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

#include <stddef.h>
#include <stdlib.h>

/* Allocating new/delete operators */

extern "C" void *__builtin_new(size_t size) {
	return malloc(size);
}

extern "C" void __builtin_delete(void *ptr) {
	free(ptr);
}

void *operator new(size_t size) noexcept {
	return malloc(size);
}

void *operator new[](size_t size) noexcept {
	return malloc(size);
}

void operator delete(void *ptr) noexcept {
	free(ptr);
}

void operator delete[](void *ptr) noexcept {
	free(ptr);
}

void operator delete(void *ptr, size_t size) noexcept {
	free(ptr);
}

void operator delete[](void *ptr, size_t size) noexcept {
	free(ptr);
}

/* Placement new/delete operators */

void *operator new(size_t size, void *ptr) noexcept {
	return ptr;
}

void *operator new[](size_t size, void *ptr) noexcept {
	return ptr;
}

void operator delete(void *ptr, void *place) noexcept {}

void operator delete[](void *ptr, void *place) noexcept {}
