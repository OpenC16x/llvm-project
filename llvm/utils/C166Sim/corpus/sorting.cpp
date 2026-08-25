//===-- sorting.cpp - LLVM libc's qsort and bsearch, on the part ----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// A driver over libc/src/stdlib's sorting, which is a different shape from the
// string functions next door: an introsort with a heapsort fallback, calling
// back into the program through a function pointer for every comparison, and
// moving elements it only knows the size of a byte at a time.
//
// Indirect calls, recursion whose depth depends on the data, and byte-wise
// moves through a pointer of unknown alignment are all things the hand written
// programs reach only lightly.
//
//===----------------------------------------------------------------------===//

#include "src/stdlib/bsearch.h"
#include "src/stdlib/qsort.h"
#include "src/stdlib/qsort_r.h"

namespace lc = LIBC_NAMESPACE;

// Fixed widths, because "long" is thirty two bits on the part and sixty four
// on the machine this is checked against, and a program that overflows one and
// not the other is comparing two different computations rather than two
// compilers.
typedef __UINT32_TYPE__ u32;
typedef __INT32_TYPE__ s32;
typedef __UINT16_TYPE__ u16;

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

/// A reproducible sequence, so that both machines sort the same thing without
/// either of them needing a random number generator.
static u32 Seed = 0x12345678ul;
static unsigned next() {
  Seed = Seed * 1103515245ul + 12345ul;
  return (unsigned)((Seed >> 16) & 0xFFFF);
}

static int cmpUShort(const void *a, const void *b) {
  unsigned x = *(const unsigned short *)a, y = *(const unsigned short *)b;
  return x < y ? -1 : x > y ? 1 : 0;
}

static int cmpWideDesc(const void *a, const void *b) {
  s32 x = *(const s32 *)a, y = *(const s32 *)b;
  return x > y ? -1 : x < y ? 1 : 0;
}

/// An element whose size is not a power of two and whose key is not first, so
/// that the swapping cannot quietly become a word move.
struct Record {
  char tag[3];
  unsigned short key;
  char pad;
};

static int cmpRecord(const void *a, const void *b) {
  const Record *x = (const Record *)a, *y = (const Record *)b;
  if (x->key != y->key)
    return x->key < y->key ? -1 : 1;
  return x->tag[0] - y->tag[0];
}

/// qsort_r's comparison, which takes the program's own pointer as well.
static int cmpByStride(const void *a, const void *b, void *ctx) {
  unsigned stride = *(const unsigned *)ctx;
  unsigned x = *(const unsigned short *)a % stride;
  unsigned y = *(const unsigned short *)b % stride;
  return x < y ? -1 : x > y ? 1 : 0;
}

static void sortShorts() {
  for (unsigned n = 0; n <= 64; n += 9) {
    unsigned short v[64];
    for (unsigned i = 0; i != n; i++)
      v[i] = (unsigned short)next();
    lc::qsort(v, n, sizeof(v[0]), cmpUShort);

    u32 sum = 0;
    int ordered = 1;
    for (unsigned i = 0; i != n; i++) {
      sum = sum * 31 + v[i];
      if (i && v[i - 1] > v[i])
        ordered = 0;
    }
    puthex(sum, 8);
    puthex((u32)ordered, 1);

    // Every element must still be findable, and one that was never there
    // must not be.
    for (unsigned i = 0; i < n; i += 7) {
      unsigned short want = v[i];
      const void *hit = lc::bsearch(&want, v, n, sizeof(v[0]), cmpUShort);
      puthex(hit ? 1 : 0, 1);
    }
    unsigned short absent = 0;
    puthex(lc::bsearch(&absent, v, n, sizeof(v[0]), cmpUShort) ? 1 : 0, 1);
  }
}

static void sortWide() {
  for (unsigned n = 0; n <= 40; n += 8) {
    s32 v[40];
    for (unsigned i = 0; i != n; i++)
      v[i] = (s32)((u32)next() * (u32)(i & 1 ? -7919 : 65537));
    lc::qsort(v, n, sizeof(v[0]), cmpWideDesc);

    u32 sum = 0;
    int ordered = 1;
    for (unsigned i = 0; i != n; i++) {
      sum = sum * 31 + (u32)v[i];
      if (i && v[i - 1] < v[i])
        ordered = 0;
    }
    puthex(sum, 8);
    puthex((u32)ordered, 1);
  }
}

static void sortRecords() {
  for (unsigned n = 0; n <= 48; n += 11) {
    Record v[48];
    for (unsigned i = 0; i != n; i++) {
      unsigned k = next();
      v[i].tag[0] = (char)('a' + (k & 15));
      v[i].tag[1] = (char)('A' + ((k >> 4) & 15));
      v[i].tag[2] = 0;
      v[i].key = (unsigned short)(k & 0xFF); // deliberate ties
      v[i].pad = 0x5A;
    }
    lc::qsort(v, n, sizeof(v[0]), cmpRecord);

    u32 sum = 0;
    int ordered = 1;
    for (unsigned i = 0; i != n; i++) {
      sum = sum * 31 + v[i].key;
      sum = sum * 31 + (unsigned char)v[i].tag[0];
      sum = sum * 31 + (unsigned char)v[i].pad;
      if (i && cmpRecord(&v[i - 1], &v[i]) > 0)
        ordered = 0;
    }
    puthex(sum, 8);
    puthex((u32)ordered, 1);
  }
}

static void sortWithContext() {
  for (unsigned stride = 3; stride <= 11; stride += 4) {
    unsigned short v[32];
    for (unsigned i = 0; i != 32; i++)
      v[i] = (unsigned short)next();
    lc::qsort_r(v, 32, sizeof(v[0]), cmpByStride, &stride);

    u32 sum = 0;
    int ordered = 1;
    for (unsigned i = 0; i != 32; i++) {
      sum = sum * 31 + v[i];
      if (i && (v[i - 1] % stride) > (v[i] % stride))
        ordered = 0;
    }
    puthex(sum, 8);
    puthex((u32)ordered, 1);
  }
}

/// Already sorted, reversed, and all equal: the inputs that decide whether an
/// introsort falls back to its heapsort, and how deep it recurses first.
static void sortDegenerate() {
  for (int shape = 0; shape != 3; shape++) {
    unsigned short v[48];
    for (unsigned i = 0; i != 48; i++)
      v[i] = shape == 0 ? (unsigned short)i
             : shape == 1 ? (unsigned short)(48 - i)
                          : (unsigned short)7;
    lc::qsort(v, 48, sizeof(v[0]), cmpUShort);
    u32 sum = 0;
    for (unsigned i = 0; i != 48; i++)
      sum = sum * 31 + v[i];
    puthex(sum, 8);
  }
}

int main() {
  sortShorts();
  sortWide();
  sortRecords();
  sortWithContext();
  sortDegenerate();
  put('.');
  put('\n');
  return 0;
}
