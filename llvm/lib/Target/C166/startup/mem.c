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
// They move a word at a time.  On this part a word access is an even address
// access - the hardware ignores bit 0 rather than trapping - so a word at a
// time copy is possible exactly when the two pointers have the same parity,
// and then only after a leading byte has brought them both to even.  Two
// pointers of different parity never become word aligned together, so those
// stay byte at a time; that is the fall through path below rather than a
// branch of its own.
//
// The three __far entry points at the bottom are the same functions again for
// the far address space.  The compiler calls those instead whenever either
// pointer is far and the move is too big or too dynamic to expand inline, so
// a program that copies a far array cannot be linked without them either.
//
//===----------------------------------------------------------------------===//

typedef __SIZE_TYPE__ size_t;

// Every loop here is one the loop idiom recogniser would like to turn back
// into a call to the function containing it.  It declines to do that inside a
// function actually named memcpy or memset, which is how the two near ones
// used to escape, but that leaves memmove and the far entry points below
// relying on nothing at all, and a word at a time loop is a shape it
// recognises where a byte at a time one is not.  So the whole file says so
// once, and no function in it depends on what it is called.  The attribute
// rather than a flag in the script that builds this file is what keeps that
// true wherever the file is compiled.
#define NO_BLOCK_IDIOM __attribute__((no_builtin("memcpy", "memmove", "memset")))

// A block move reads and writes whatever the caller put there, so the word
// accesses below alias the caller's objects of every type.  Saying so is what
// makes them defined rather than what happens to work.
typedef unsigned short __attribute__((may_alias)) word;

NO_BLOCK_IDIOM void *memcpy(void *dst, const void *src, size_t n) {
  unsigned char *d = dst;
  const unsigned char *s = src;
  if (!(((size_t)d ^ (size_t)s) & 1)) {
    if (n && ((size_t)d & 1)) {
      *d++ = *s++;
      n--;
    }
    for (size_t w = n >> 1; w; w--) {
      *(word *)d = *(const word *)s;
      d += 2;
      s += 2;
    }
    n &= 1;
  }
  while (n--)
    *d++ = *s++;
  return dst;
}

NO_BLOCK_IDIOM void *memmove(void *dst, const void *src, size_t n) {
  unsigned char *d = dst;
  const unsigned char *s = src;
  // Copy backwards when the regions overlap with the destination above the
  // source, which is the one direction a forward copy would corrupt.  Both
  // ends move by the same n, so two pointers that started with the same
  // parity still have it, and the byte that brings them to even is the last
  // one rather than the first.
  if (d > s && d < s + n) {
    d += n;
    s += n;
    if (!(((size_t)d ^ (size_t)s) & 1)) {
      if ((size_t)d & 1) {
        *--d = *--s;
        n--;
      }
      for (size_t w = n >> 1; w; w--) {
        d -= 2;
        s -= 2;
        *(word *)d = *(const word *)s;
      }
      n &= 1;
    }
    while (n--)
      *--d = *--s;
    return dst;
  }
  // Everything else is a forward copy, which is the function next door.  A
  // zero length and two equal pointers both land here, and both are copies
  // that do nothing.
  return memcpy(dst, src, n);
}

NO_BLOCK_IDIOM void *memset(void *dst, int c, size_t n) {
  unsigned char *d = dst;
  unsigned char b = (unsigned char)c;
  if (n && ((size_t)d & 1)) {
    *d++ = b;
    n--;
  }
  // One byte in both halves, which is the word the fill writes.
  unsigned short both = (unsigned short)(b * 0x0101u);
  for (size_t w = n >> 1; w; w--) {
    *(word *)d = both;
    d += 2;
  }
  if (n & 1)
    *d = b;
  return dst;
}

NO_BLOCK_IDIOM int memcmp(const void *a, const void *b, size_t n) {
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
// writing them any other way would be re-implementing it by hand.  The parity
// is the parity of the whole linear address, which is the parity of the
// offset, because a segment boundary is even.
//
// The size stays 16 bit, as it is for a near move, so one call moves at most a
// segment's worth of bytes.  Both pointers are far even when the caller's was
// near: the compiler widens a near one on the way in, which assumes the reset
// configuration of the data page pointers in the same way an addrspacecast
// does.

typedef unsigned short __attribute__((may_alias)) __far farword;
// The near uintptr_t is 16 bit, which a far pointer does not fit in, so the
// parity below is read out of the 32 bit integer a far pointer converts to.
typedef unsigned long farint;

NO_BLOCK_IDIOM void __far *__memcpy_far(void __far *dst, const void __far *src, unsigned n) {
  unsigned char __far *d = dst;
  const unsigned char __far *s = src;
  if (!(((farint)d ^ (farint)s) & 1)) {
    if (n && ((farint)d & 1)) {
      *d++ = *s++;
      n--;
    }
    for (unsigned w = n >> 1; w; w--) {
      *(farword *)d = *(const farword *)s;
      d += 2;
      s += 2;
    }
    n &= 1;
  }
  while (n--)
    *d++ = *s++;
  return dst;
}

NO_BLOCK_IDIOM void __far *__memmove_far(void __far *dst, const void __far *src, unsigned n) {
  unsigned char __far *d = dst;
  const unsigned char __far *s = src;
  // The comparison is on the whole 24 bit address, so two pointers in
  // different segments order the way their addresses do rather than by
  // offset.
  if (d > s && d < s + n) {
    d += n;
    s += n;
    if (!(((farint)d ^ (farint)s) & 1)) {
      if ((farint)d & 1) {
        *--d = *--s;
        n--;
      }
      for (unsigned w = n >> 1; w; w--) {
        d -= 2;
        s -= 2;
        *(farword *)d = *(const farword *)s;
      }
      n &= 1;
    }
    while (n--)
      *--d = *--s;
    return dst;
  }
  return __memcpy_far(dst, src, n);
}

NO_BLOCK_IDIOM void __far *__memset_far(void __far *dst, int c, unsigned n) {
  unsigned char __far *d = dst;
  unsigned char b = (unsigned char)c;
  if (n && ((farint)d & 1)) {
    *d++ = b;
    n--;
  }
  unsigned short both = (unsigned short)(b * 0x0101u);
  for (unsigned w = n >> 1; w; w--) {
    *(farword *)d = both;
    d += 2;
  }
  if (n & 1)
    *d = b;
  return dst;
}

/* strlen, which is here for the same reason the four above are: the compiler
 * and the C++ runtime call it without being asked to.  Nothing else of
 * <string.h> is, because nothing else has turned up being needed - a program
 * that wants the rest brings its own C library.
 *
 * The loop idiom recognizer can turn this into a call to itself, exactly as it
 * can with the block moves above, so it gets the same treatment. */
__attribute__((no_builtin("strlen"))) unsigned strlen(const char *s) {
  const char *p = s;
  while (*p)
    p++;
  return (unsigned)(p - s);
}
