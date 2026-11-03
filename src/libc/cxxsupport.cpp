/*
 * ps1-bare-metal - Copyright (C) 2023-2026 spicyjpeg
 * SPDX-License-Identifier: MIT
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
	(void) size;

	free(ptr);
}

void operator delete[](void *ptr, size_t size) noexcept {
	(void) size;

	free(ptr);
}

/* Placement new/delete operators */

void *operator new(size_t size, void *ptr) noexcept {
	(void) size;

	return ptr;
}

void *operator new[](size_t size, void *ptr) noexcept {
	(void) size;

	return ptr;
}

void operator delete(void *ptr, void *place) noexcept {
	(void) ptr;
	(void) place;
}

void operator delete[](void *ptr, void *place) noexcept {
	(void) ptr;
	(void) place;
}
