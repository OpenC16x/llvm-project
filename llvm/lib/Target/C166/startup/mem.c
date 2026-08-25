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
// The three __far entry points at the bottom are the same functions again for
// the far address space.  The compiler calls those instead whenever either
// pointer is far and the move is too big or too dynamic to expand inline, so
// a program that copies a far array cannot be linked without them either.
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

//===----------------------------------------------------------------------===//
// The far address space
//===----------------------------------------------------------------------===//
//
// A far pointer is a linear 24 bit address, so stepping one is an ordinary 32
// bit add and a carry out of the offset lands in the segment.  That is what
// makes these the same loops as the ones above with the type changed: the
// crossing is the compiler's arithmetic, not something to open code here, and
// writing them any other way would be re-implementing it by hand.
//
// The size stays 16 bit, as it is for a near move, so one call moves at most a
// segment's worth of bytes.  Both pointers are far even when the caller's was
// near: the compiler widens a near one on the way in, which assumes the reset
// configuration of the data page pointers in the same way an addrspacecast
// does.
//
// The no_builtin is what stops each of these calling itself.  Loop idiom
// recognition turns a byte at a time copy or fill back into a memcpy or a
// memset, and it declines to do that only inside a function actually named
// memcpy or memset - which is how the near versions above escape it.  These
// are not named that, so the loop below would become a block move through far
// pointers, which is a call to this function.  It is an infinite recursion
// that builds and links, and the attribute rather than a flag in the script
// that builds this file is what keeps it fixed wherever the file is compiled.
#define NO_BLOCK_IDIOM __attribute__((no_builtin("memcpy", "memmove", "memset")))

NO_BLOCK_IDIOM void __far *__memcpy_far(void __far *dst, const void __far *src, unsigned n) {
  unsigned char __far *d = dst;
  const unsigned char __far *s = src;
  while (n--)
    *d++ = *s++;
  return dst;
}

NO_BLOCK_IDIOM void __far *__memmove_far(void __far *dst, const void __far *src, unsigned n) {
  unsigned char __far *d = dst;
  const unsigned char __far *s = src;
  if (d == s || n == 0)
    return dst;
  // Copy backwards when the regions overlap with the destination above the
  // source, which is the one direction a forward copy would corrupt.  The
  // comparison is on the whole 24 bit address, so two pointers in different
  // segments order the way their addresses do rather than by offset.
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

NO_BLOCK_IDIOM void __far *__memset_far(void __far *dst, int c, unsigned n) {
  unsigned char __far *d = dst;
  while (n--)
    *d++ = (unsigned char)c;
  return dst;
}

/* strlen, which is here for the same reason the four above are: the compiler
 * and the C++ runtime call it without being asked to.  Nothing else of
 * <string.h> is, because nothing else has turned up being needed - a program
 * that wants the rest brings its own C library.
 *
 * The loop idiom recognizer can turn this into a call to itself, exactly as it
 * can with the block moves above, so it gets the same attribute. */
__attribute__((no_builtin("strlen"))) unsigned strlen(const char *s) {
  const char *p = s;
  while (*p)
    p++;
  return (unsigned)(p - s);
}
