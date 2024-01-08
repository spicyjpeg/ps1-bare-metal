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
 *
 * This code is based on psyqo's malloc implementation, available here:
 * https://github.com/grumpycoders/pcsx-redux/blob/main/src/mips/psyqo/src/alloc.c
 */

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

#define _align(x, n) (((x) + ((n) - 1)) & ~((n) - 1))
#define _updateHeapUsage(incr)

/* Internal state */

typedef struct _Block {
	struct _Block *prev, *next;

	void   *ptr;
	size_t size;
} Block;

static void  *_mallocStart;
static Block *_mallocHead, *_mallocTail;

/* Allocator implementation */

static Block *_findBlock(Block *head, size_t size) {
	Block *prev = head;

	for (; prev; prev = prev->next) {
		if (prev->next) {
			uintptr_t nextBot = (uintptr_t) prev->next;
			nextBot          -= (uintptr_t) prev->ptr + prev->size;

			if (nextBot >= size)
				return prev;
		}
	}

	return prev;
}

void *malloc(size_t size) {
	if (!size)
		return 0;

	size_t _size = _align(size + sizeof(Block), 8);

	// Nothing's initialized yet? Let's just initialize the bottom of our heap,
	// flag it as allocated.
	if (!_mallocHead) {
		if (!_mallocStart)
			_mallocStart = sbrk(0);

		Block *new = (Block *) sbrk(_size);
		if (!new)
			return 0;

		void *ptr = (void *) &new[1];
		new->ptr  = ptr;
		new->size = _size - sizeof(Block);
		new->prev = 0;
		new->next = 0;

		_mallocHead = new;
		_mallocTail = new;

		_updateHeapUsage(size);
		return ptr;
	}

	// We *may* have the bottom of our heap that has shifted, because of a free.
	// So let's check first if we have free space there, because I'm nervous
	// about having an incomplete data structure.
	if (((uintptr_t) _mallocStart + _size) < ((uintptr_t) _mallocHead)) {
		Block *new = (Block *) _mallocStart;

		void *ptr = (void *) &new[1];
		new->ptr  = ptr;
		new->size = _size - sizeof(Block);
		new->prev = 0;
		new->next = _mallocHead;

		_mallocHead->prev = new;
		_mallocHead       = new;

		_updateHeapUsage(size);
		return ptr;
	}

	// No luck at the beginning of the heap, let's walk the heap to find a fit.
	Block *prev = _findBlock(_mallocHead, _size);
	if (prev) {
		Block *new = (Block *) ((uintptr_t) prev->ptr + prev->size);

		void *ptr = (void *)((uintptr_t) new + sizeof(Block));
		new->ptr  = ptr;
		new->size = _size - sizeof(Block);
		new->prev = prev;
		new->next = prev->next;

		(new->next)->prev = new;
		prev->next        = new;

		_updateHeapUsage(size);
		return ptr;
	}

	// Time to extend the size of the heap.
	Block *new = (Block *) sbrk(_size);
	if (!new)
		return 0;

	void *ptr = (void *) &new[1];
	new->ptr  = ptr;
	new->size = _size - sizeof(Block);
	new->prev = _mallocTail;
	new->next = 0;

	_mallocTail->next = new;
	_mallocTail       = new;

	_updateHeapUsage(size);
	return ptr;
}

void *calloc(size_t num, size_t size) {
	return malloc(num * size);
}

void *realloc(void *ptr, size_t size) {
	if (!size) {
		free(ptr);
		return 0;
	}
	if (!ptr)
		return malloc(size);

	size_t _size = _align(size + sizeof(Block), 8);
	Block  *prev = (Block *) ((uintptr_t) ptr - sizeof(Block));

	// New memory block shorter?
	if (prev->size >= _size) {
		_updateHeapUsage(size - prev->size);
		prev->size = _size;

		if (!prev->next)
			sbrk((ptr - sbrk(0)) + _size);

		return ptr;
	}

	// New memory block larger; is it the last one?
	if (!prev->next) {
		void *new = sbrk(_size - prev->size);
		if (!new)
			return 0;

		_updateHeapUsage(size - prev->size);
		prev->size = _size;
		return ptr;
	}

	// Do we have free memory after it?
	if (((prev->next)->ptr - ptr) > _size) {
		_updateHeapUsage(size - prev->size);
		prev->size = _size;
		return ptr;
	}

	// No luck.
	void *new = malloc(size);
	if (!new)
		return 0;

	__builtin_memcpy(new, ptr, prev->size);
	free(ptr);
	return new;
}

void free(void *ptr) {
	if (!ptr || !_mallocHead)
		return;

	// First block; bumping head ahead.
	if (ptr == _mallocHead->ptr) {
		size_t size = _mallocHead->size;
		size       += (uintptr_t) _mallocHead->ptr - (uintptr_t) _mallocHead;
		_mallocHead = _mallocHead->next;

		if (_mallocHead) {
			_mallocHead->prev = 0;
		} else {
			_mallocTail = 0;
			sbrk(-size);
		}

		_updateHeapUsage(-(_mallocHead->size));
		return;
	}

	// Finding the proper block
	Block *cur = _mallocHead;

	for (cur = _mallocHead; ptr != cur->ptr; cur = cur->next) {
		if (!cur->next)
			return;
	}

	if (cur->next) {
		// In the middle, just unlink it
		(cur->next)->prev = cur->prev;
	} else {
		// At the end, shrink heap
		void  *top  = sbrk(0);
		size_t size = (top - (cur->prev)->ptr) - (cur->prev)->size;
		_mallocTail = cur->prev;

		sbrk(-size);
	}

	_updateHeapUsage(-(cur->size));
	(cur->prev)->next = cur->next;
}
