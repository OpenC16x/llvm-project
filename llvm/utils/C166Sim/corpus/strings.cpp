//===-- strings.cpp - LLVM libc's string functions, on the part ------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// A driver over the string functions in libc/src/string, which are compiled
// for the C166 and for the machine running this and have to agree.
//
// The point is that the code under test was not written for this: it is
// LLVM's own libc, written by people solving a different problem, and it
// reaches shapes the hand written programs next door do not - two-pointer
// scans, tables built on the stack, tail calls between helpers, and the
// SIMD-shaped word-at-a-time loops that libc/src/__support/CPP/simd.h builds
// out of _BitInt.
//
//===----------------------------------------------------------------------===//

#include "src/string/memccpy.h"
#include "src/string/memchr.h"
#include "src/string/memmem.h"
#include "src/string/memrchr.h"
#include "src/string/stpncpy.h"
#include "src/string/strcasestr.h"
#include "src/string/strchrnul.h"
#include "src/string/strcspn.h"
#include "src/string/strlcpy.h"
#include "src/string/strlen.h"
#include "src/string/strncmp.h"
#include "src/string/strnlen.h"
#include "src/string/strpbrk.h"
#include "src/string/strrchr.h"
#include "src/string/strsep.h"
#include "src/string/strspn.h"
#include "src/string/strstr.h"
#include "src/string/strtok_r.h"

namespace lc = LIBC_NAMESPACE;

// Fixed widths, because "long" is thirty two bits on the part and sixty four
// on the machine this is checked against, and "size_t" is sixteen against
// sixty four.  A value that overflows one and not the other would look like
// two compilers disagreeing when it is two different computations.
typedef __UINT32_TYPE__ u32;

#ifdef __c166__
static void put(char c) { *(__far volatile unsigned char *)0xFF0000 = c; }
#else
extern "C" int putchar(int);
static void put(char c) { putchar(c); }
#endif

static void puthex(u32 v, int digits) {
  for (int i = digits - 1; i >= 0; i--)
    put("0123456789abcdef"[(v >> (i * 4)) & 0xF]);
  put('\n');
}

/// An offset into a buffer, or ffff for a null pointer, so that a result can
/// be printed without printing an address that differs between the two
/// machines by construction.
static void putoff(const void *p, const void *base) {
  if (!p) {
    puthex(0xFFFF, 4);
    return;
  }
  puthex((u32)((const char *)p - (const char *)base), 4);
}

static const char Text[] =
    "the quick brown fox jumps over the lazy dog, and the dog "
    "does not mind at all; the fox, however, minds a great deal.";

static const char *const Needles[] = {
    "fox", "dog", "the", "", "zebra", "deal.", "a", "minds", "THE", "Fox",
};
static const unsigned NumNeedles = sizeof(Needles) / sizeof(Needles[0]);

static const char *const Sets[] = {
    "aeiou", "", " ", ".,;", "abcdefghijklmnopqrstuvwxyz", "xyz",
};
static const unsigned NumSets = sizeof(Sets) / sizeof(Sets[0]);

static void searching() {
  const u32 Len = lc::strlen(Text);
  puthex(Len, 4);

  for (unsigned i = 0; i != NumNeedles; i++) {
    const char *N = Needles[i];
    putoff(lc::strstr(Text, N), Text);
    putoff(lc::strcasestr(Text, N), Text);
    putoff(lc::memmem(Text, Len, N, lc::strlen(N)), Text);
  }

  for (int c = 0; c < 128; c += 7) {
    putoff(lc::memchr(Text, c, Len), Text);
    putoff(lc::memrchr(Text, c, Len), Text);
    putoff(lc::strchrnul(Text, c), Text);
    putoff(lc::strrchr(Text, c), Text);
  }
}

static void spans() {
  for (unsigned i = 0; i != NumSets; i++) {
    puthex((u32)lc::strspn(Text, Sets[i]), 4);
    puthex((u32)lc::strcspn(Text, Sets[i]), 4);
    putoff(lc::strpbrk(Text, Sets[i]), Text);
  }

  for (unsigned n = 0; n <= 24; n += 3) {
    puthex((u32)lc::strnlen(Text, n), 4);
    puthex((u32)(unsigned short)lc::strncmp(Text, "the quick", n), 4);
  }
}

static void copying() {
  char Buf[80];

  for (unsigned n = 0; n <= 40; n += 5) {
    for (unsigned i = 0; i != sizeof(Buf); i++)
      Buf[i] = '#';
    char *End = lc::stpncpy(Buf, Text, n);
    puthex((u32)(End - Buf), 4);
    // Whatever stpncpy left behind, including the padding it writes.
    u32 Sum = 0;
    for (unsigned i = 0; i != 48; i++)
      Sum = Sum * 31 + (unsigned char)Buf[i];
    puthex(Sum, 8);
  }

  for (unsigned n = 0; n <= 40; n += 7) {
    for (unsigned i = 0; i != sizeof(Buf); i++)
      Buf[i] = '#';
    puthex((u32)lc::strlcpy(Buf, Text, n), 4);
    u32 Sum = 0;
    for (unsigned i = 0; i != 48; i++)
      Sum = Sum * 31 + (unsigned char)Buf[i];
    puthex(Sum, 8);
  }

  for (int c = 0; c < 128; c += 23) {
    for (unsigned i = 0; i != sizeof(Buf); i++)
      Buf[i] = '#';
    void *End = lc::memccpy(Buf, Text, c, 40);
    putoff(End, Buf);
  }
}

static void splitting() {
  // strtok_r and strsep both write through the string they are given, so each
  // gets its own copy.
  char Work[sizeof(Text)];
  for (unsigned i = 0; i != sizeof(Text); i++)
    Work[i] = Text[i];

  char *Save = nullptr;
  for (char *Tok = lc::strtok_r(Work, " ,.;", &Save); Tok;
       Tok = lc::strtok_r(nullptr, " ,.;", &Save)) {
    u32 Sum = 0;
    for (const char *p = Tok; *p; p++)
      Sum = Sum * 31 + (unsigned char)*p;
    puthex(Sum, 8);
  }

  for (unsigned i = 0; i != sizeof(Text); i++)
    Work[i] = Text[i];
  char *Rest = Work;
  while (char *Tok = lc::strsep(&Rest, " ;")) {
    u32 Sum = 0;
    for (const char *p = Tok; *p; p++)
      Sum = Sum * 31 + (unsigned char)*p;
    puthex(Sum, 8);
  }
}

int main() {
  searching();
  spans();
  copying();
  splitting();
  put('.');
  put('\n');
  return 0;
}
