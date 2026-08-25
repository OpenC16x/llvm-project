/* Unicode characters, and the state a multibyte conversion would keep.
 *
 * char16_t and char32_t are keywords in C++ and typedefs in C; the widths come
 * from the compiler rather than from here, so this is right whatever the
 * target's char16_t is.
 *
 * The conversion functions are declared and not defined.  The execution
 * character set here is plain bytes and nothing in this library converts, so a
 * program that calls one gets a link error naming it.
 *
 * Part of the LLVM Project, under the Apache License v2.0 with LLVM
 * Exceptions.  See https://llvm.org/LICENSE.txt for license information.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */

#ifndef _C166_UCHAR_H
#define _C166_UCHAR_H

#include <stddef.h>
#include <wchar.h>

#ifndef __cplusplus
typedef __CHAR16_TYPE__ char16_t;
typedef __CHAR32_TYPE__ char32_t;
#endif

#ifdef __cplusplus
extern "C" {
#endif

size_t mbrtoc16(char16_t *pc16, const char *s, size_t n, mbstate_t *ps);
size_t c16rtomb(char *s, char16_t c16, mbstate_t *ps);
size_t mbrtoc32(char32_t *pc32, const char *s, size_t n, mbstate_t *ps);
size_t c32rtomb(char *s, char32_t c32, mbstate_t *ps);

#ifdef __cplusplus
}
#endif

#endif /* _C166_UCHAR_H */
