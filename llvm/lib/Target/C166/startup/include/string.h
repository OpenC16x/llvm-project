/* <string.h>, nearly all of which now exists.
 *
 * It used to be that five functions here were defined and the rest were only
 * declared: memcpy, memmove, memset, memcmp and strlen are in the C library
 * because the compiler calls them whether or not a program does - a structure
 * copy or an array initialisation becomes a call to one - and nothing else
 * was.  libc.a now carries LLVM's own libc beside those five, so what is
 * declared and not defined has shrunk to two functions and a reason:
 *
 *   strdup and strndup return memory they allocated, and there is no allocator
 *   here.  They are in the archive and they fail to link, naming malloc, which
 *   is what actually went wrong.
 *
 * The five above are still mem.c's rather than llvm-libc's - mem.c comes first
 * in the archive, and that is the choice, made because it moves a word at a
 * time where llvm-libc moves a byte.  differential/mksysroot.sh has the
 * measurement.
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

/* The five the compiler reaches itself. */
void *memcpy(void *dst, const void *src, size_t n);
void *memmove(void *dst, const void *src, size_t n);
void *memset(void *dst, int c, size_t n);
int memcmp(const void *a, const void *b, size_t n);
size_t strlen(const char *s);

void __far *__memcpy_far(void __far *dst, const void __far *src, size_t n);
void __far *__memmove_far(void __far *dst, const void __far *src, size_t n);
void __far *__memset_far(void __far *dst, int c, size_t n);

/* The rest of C. */
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
char *strerror(int err);

/* C23. */
void *memset_explicit(void *dst, int c, size_t n);

/* C11 Annex K, of which this is the only member here: bounds checked
 * interfaces are optional and llvm-libc implements this one. */
size_t strnlen_s(const char *s, size_t n);

/* POSIX, and the ones from BSD that llvm-libc carries.  A part this size has
 * no feature test macros to hide them behind and no reason to: they are in the
 * archive either way, and a program that does not name one does not get it. */
size_t strnlen(const char *s, size_t n);
int strerror_r(int err, char *buf, size_t n);
char *strtok_r(char *s, const char *sep, char **save);
char *stpcpy(char *dst, const char *src);
char *stpncpy(char *dst, const char *src, size_t n);
char *strsep(char **s, const char *sep);
char *strchrnul(const char *s, int c);
char *strcasestr(const char *haystack, const char *needle);
void *memccpy(void *dst, const void *src, int c, size_t n);
void *mempcpy(void *dst, const void *src, size_t n);
void *memrchr(const void *s, int c, size_t n);
void *memmem(const void *haystack, size_t hn, const void *needle, size_t nn);
size_t strlcpy(char *dst, const char *src, size_t size);
size_t strlcat(char *dst, const char *src, size_t size);

/* Declared and not defined: these want an allocator, and this sysroot has
 * none.  A program that calls one gets a link error naming malloc. */
char *strdup(const char *s);
char *strndup(const char *s, size_t n);

#ifdef __cplusplus
}
#endif

#endif /* _C166_STRING_H */
