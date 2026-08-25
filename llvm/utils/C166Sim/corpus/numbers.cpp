//===-- numbers.cpp - LLVM libc's integer formatting, on the part ---------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// A driver over libc/src/__support/integer_to_string.h, which is a third shape
// again: a template that formats an integer into a buffer it sizes at compile
// time, in whatever base it is asked for, and which divides its way down.
//
// On a part whose ALU is sixteen bits, formatting a 64 bit value in base ten
// is a long chain of calls into the compiler-rt builtins, so this exercises
// the division and multiplication paths as much as the formatting - and it
// does it from code that was not written with either in mind.
//
//===----------------------------------------------------------------------===//

#include "src/__support/integer_to_string.h"

namespace lc = LIBC_NAMESPACE;

// Fixed widths, because "long" is thirty two bits on the part and sixty four
// on the machine this is checked against.
typedef __UINT32_TYPE__ u32;
typedef __INT32_TYPE__ s32;
typedef __UINT64_TYPE__ u64;
typedef __INT64_TYPE__ s64;
typedef __UINT16_TYPE__ u16;
typedef __INT16_TYPE__ s16;

#ifdef __c166__
static void put(char c) { *(__far volatile unsigned char *)0xFF0000 = c; }
#else
extern "C" int putchar(int);
static void put(char c) { putchar(c); }
#endif

static void puts_(const char *s) {
  while (*s)
    put(*s++);
  put('\n');
}

/// Whatever a formatter produced, printed as it stands.  The buffer is the
/// formatter's own and is not null terminated, so its length comes back with
/// it.
template <class Fmt> static void show(const Fmt &F) {
  auto V = F.view();
  for (unsigned i = 0; i != V.size(); i++)
    put(V[i]);
  put('\n');
}

static u32 Seed = 0xC0FFEEul;
static u32 next32() {
  Seed = Seed * 1103515245ul + 12345ul;
  return Seed;
}

static void bases16() {
  static const s16 Values[] = {0,  1,     -1,    2,    -2,   127,  -128,
                               255, -256, 1000, -1000, 32767, -32768, 12345};
  for (unsigned i = 0; i != sizeof(Values) / sizeof(Values[0]); i++) {
    s16 v = Values[i];
    show(lc::IntegerToString<s16>(v));
    show(lc::IntegerToString<u16>((u16)v));
    show(lc::IntegerToString<u16, lc::radix::Hex>((u16)v));
    show(lc::IntegerToString<u16, lc::radix::Oct>((u16)v));
    show(lc::IntegerToString<u16, lc::radix::Bin>((u16)v));
  }
}

static void bases32() {
  for (unsigned i = 0; i != 24; i++) {
    u32 raw = next32();
    // A spread of magnitudes, so the division loop runs a different number of
    // times each way round.
    u32 v = raw >> (i % 32);
    show(lc::IntegerToString<u32>(v));
    show(lc::IntegerToString<s32>((s32)v));
    show(lc::IntegerToString<u32, lc::radix::Hex>(v));
    show(lc::IntegerToString<u32, lc::radix::Bin>(v));
  }
  static const s32 Edges[] = {0, 1, -1, 2147483647, (-2147483647 - 1), 1000000,
                              -1000000, 65536, -65536};
  for (unsigned i = 0; i != sizeof(Edges) / sizeof(Edges[0]); i++) {
    show(lc::IntegerToString<s32>(Edges[i]));
    show(lc::IntegerToString<u32, lc::radix::Hex>((u32)Edges[i]));
  }
}

static void bases64() {
  for (unsigned i = 0; i != 16; i++) {
    u64 v = ((u64)next32() << 32) | next32();
    v >>= (i * 4) % 64;
    show(lc::IntegerToString<u64>(v));
    show(lc::IntegerToString<s64>((s64)v));
    show(lc::IntegerToString<u64, lc::radix::Hex>(v));
  }
  static const s64 Edges[] = {0, 1, -1, 9223372036854775807ll,
                              (-9223372036854775807ll - 1), 1000000000000ll};
  for (unsigned i = 0; i != sizeof(Edges) / sizeof(Edges[0]); i++) {
    show(lc::IntegerToString<s64>(Edges[i]));
    show(lc::IntegerToString<u64, lc::radix::Hex>((u64)Edges[i]));
  }
}

/// The width and case options, which pick different code paths inside the
/// formatter rather than different arithmetic.
static void styles() {
  for (unsigned i = 0; i != 12; i++) {
    u32 v = next32() >> (i % 24);
    show(lc::IntegerToString<u32, lc::radix::Hex::WithPrefix>(v));
    show(lc::IntegerToString<u32, lc::radix::Hex::Uppercase>(v));
    show(lc::IntegerToString<u32, lc::radix::Hex::WithWidth<8>>(v));
    show(lc::IntegerToString<u32, lc::radix::Bin::WithPrefix>(v));
    show(lc::IntegerToString<u32, lc::radix::Dec::WithWidth<12>>(v));
  }
}

int main() {
  puts_("bases16");
  bases16();
  puts_("bases32");
  bases32();
  puts_("bases64");
  bases64();
  puts_("styles");
  styles();
  put('.');
  put('\n');
  return 0;
}
