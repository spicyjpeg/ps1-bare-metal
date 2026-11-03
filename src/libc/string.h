/*
 * ps1-bare-metal - Copyright (C) 2023-2026 spicyjpeg
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

void *memset (void             *dest, int                    ch,  size_t count);
void *memcpy (void *__restrict dest,  const void *__restrict src, size_t count);
void *memccpy(
	void *__restrict       dest,
	const void *__restrict src,
	int                    ch,
	size_t                 count
);
void *memmove(void       *dest, const void *src, size_t count);
int  memcmp  (const void *lhs,  const void *rhs, size_t count);
void *memchr (const void *ptr,  int        ch,   size_t count);

char *strcpy (char *__restrict dest, const char *__restrict src);
char *strncpy(char *__restrict dest, const char *__restrict src,  size_t count);
int  strcmp  (const char       *lhs, const char             *rhs);
int  strncmp (const char       *lhs, const char             *rhs, size_t count);
char *strchr (const char       *str, int                    ch);
char *strrchr(const char       *str, int                    ch);
char *strpbrk(const char       *str, const char             *breakSet);
char *strstr (const char       *str, const char             *substr);

size_t strlen  (const char       *str);
char   *strcat (char *__restrict dest, const char *__restrict src);
char   *strncat(char *__restrict dest, const char *__restrict src, size_t count);
char   *strdup (const char       *str);
char   *strndup(const char       *str, size_t count);

char *strtok(char *__restrict str, const char *__restrict delim);

#ifdef __cplusplus
}
#endif
