/* <string.h>, of which five functions exist and the rest do not.
 *
 * memcpy, memmove, memset, memcmp and strlen are in the C library because the
 * compiler calls them whether or not a program does - a structure copy or an
 * array initialisation becomes a call to one - so those five are defined.
 * Everything else here is declared and defined nowhere: a program that calls
 * strcpy gets a link error naming it, which says "bring a C library" better
 * than a header that hides the function would.
 *
 * The __far entry points are the same functions again for the far address
 * space, which the compiler calls instead when either pointer is far.  They
 * are not standard and are named as this implementation's own.
 *
 * Part of the LLVM Project, under the Apache License v2.0 with LLVM
 * Exceptions.  See https://llvm.org/LICENSE.txt for license information.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */

#ifndef _C166_STRING_H
#define _C166_STRING_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Defined: the compiler reaches these itself. */
void *memcpy(void *dst, const void *src, size_t n);
void *memmove(void *dst, const void *src, size_t n);
void *memset(void *dst, int c, size_t n);
int memcmp(const void *a, const void *b, size_t n);
size_t strlen(const char *s);

void __far *__memcpy_far(void __far *dst, const void __far *src, size_t n);
void __far *__memmove_far(void __far *dst, const void __far *src, size_t n);
void __far *__memset_far(void __far *dst, int c, size_t n);

/* Declared: a program that wants one brings it. */
void *memchr(const void *s, int c, size_t n);
char *strcpy(char *dst, const char *src);
char *strncpy(char *dst, const char *src, size_t n);
char *strcat(char *dst, const char *src);
char *strncat(char *dst, const char *src, size_t n);
int strcmp(const char *a, const char *b);
int strncmp(const char *a, const char *b, size_t n);
int strcoll(const char *a, const char *b);
size_t strxfrm(char *dst, const char *src, size_t n);
char *strchr(const char *s, int c);
char *strrchr(const char *s, int c);
size_t strspn(const char *s, const char *set);
size_t strcspn(const char *s, const char *set);
char *strpbrk(const char *s, const char *set);
char *strstr(const char *haystack, const char *needle);
char *strtok(char *s, const char *sep);
size_t strnlen(const char *s, size_t n);
char *strerror(int err);

#ifdef __cplusplus
}
#endif

#endif /* _C166_STRING_H */
