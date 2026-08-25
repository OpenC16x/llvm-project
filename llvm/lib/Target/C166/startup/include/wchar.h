/* Wide characters, as far as this part has them.
 *
 * wchar_t is what the compiler says it is, which here is a 16 bit type: the
 * width is the target's rather than this header's, and clang emits it into
 * every translation unit as __WCHAR_TYPE__.
 *
 * The conversion state is a struct rather than an int because the standard
 * requires an object type; the one member is what a multibyte conversion in
 * progress would have to remember, and nothing here starts one.
 *
 * The functions are declared and not defined.  A program that calls one gets a
 * link error naming it, which is a better answer than a header that pretends
 * they are missing.
 *
 * Part of the LLVM Project, under the Apache License v2.0 with LLVM
 * Exceptions.  See https://llvm.org/LICENSE.txt for license information.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */

#ifndef _C166_WCHAR_H
#define _C166_WCHAR_H

#include <stddef.h>

typedef __WINT_TYPE__ wint_t;

#ifndef WEOF
#define WEOF ((wint_t)-1)
#endif

typedef struct {
  unsigned int __count;
  unsigned int __value;
} mbstate_t;

#ifdef __cplusplus
extern "C" {
#endif

size_t wcslen(const wchar_t *s);
wchar_t *wcscpy(wchar_t *dst, const wchar_t *src);
int wcscmp(const wchar_t *a, const wchar_t *b);
size_t mbrtowc(wchar_t *pwc, const char *s, size_t n, mbstate_t *ps);
size_t wcrtomb(char *s, wchar_t wc, mbstate_t *ps);

#ifdef __cplusplus
}
#endif

#endif /* _C166_WCHAR_H */
