//===-- mem.c - The four block functions the compiler calls ---------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// The compiler turns a structure copy or an array initialisation into a call
// to one of these, so a program cannot be linked without them even if it never
// names one.  They belong to a C library rather than to a compiler, and a
// project that has one should use its versions instead of these; these are
// here so that a program can be linked and run with nothing else present.
//
// They are deliberately the simple byte at a time versions.  A word at a time
// version has to deal with the two pointers being differently aligned, which
// is worth doing when there is something to measure it against.
//
//===----------------------------------------------------------------------===//

typedef __SIZE_TYPE__ size_t;

void *memcpy(void *dst, const void *src, size_t n) {
  unsigned char *d = dst;
  const unsigned char *s = src;
  while (n--)
    *d++ = *s++;
  return dst;
}

void *memmove(void *dst, const void *src, size_t n) {
  unsigned char *d = dst;
  const unsigned char *s = src;
  if (d == s || n == 0)
    return dst;
  // Copy backwards when the regions overlap with the destination above the
  // source, which is the one direction a forward copy would corrupt.
  if (d > s && d < s + n) {
    d += n;
    s += n;
    while (n--)
      *--d = *--s;
    return dst;
  }
  while (n--)
    *d++ = *s++;
  return dst;
}

void *memset(void *dst, int c, size_t n) {
  unsigned char *d = dst;
  while (n--)
    *d++ = (unsigned char)c;
  return dst;
}

int memcmp(const void *a, const void *b, size_t n) {
  const unsigned char *p = a;
  const unsigned char *q = b;
  while (n--) {
    if (*p != *q)
      return *p - *q;
    p++;
    q++;
  }
  return 0;
}
