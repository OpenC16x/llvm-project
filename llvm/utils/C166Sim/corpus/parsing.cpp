//===-- parsing.cpp - LLVM libc's number parsers, on the part -------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// A driver over the strtol family, which until the sysroot had an <errno.h>
// would not compile for this target at all: every one of these sources sets
// errno on overflow, and libc in overlay mode resolves its errno to the
// system header's.
//
// Three things have to agree between the two machines, not one: the value, the
// pointer the parse stopped at, and errno.  The last is what makes this worth
// having - the saturating paths are the ones with the arithmetic in them, and
// a value that is checked but an errno that is not would let half of each
// function go unwatched.
//
// The widths decide what can be compared with what.  long long is sixty four
// bits on both machines, so strtoll, strtoull, the two intmax parsers and
// atoll can be given anything, saturation included, and the two sides have to
// agree digit for digit.  long is thirty two bits on the part and sixty four
// on the machine this is checked against, and int is sixteen against thirty
// two, so strtol, strtoul, atol and atoi saturate at different places by
// construction; those are given only inputs that fit both widths, where the
// answer is the same number on both.  What that leaves unwatched is the part's
// own thirty two bit saturation in strtol, which nothing on the host computes
// the same way; the sixty four bit saturation next to it exercises the same
// code in the same template at a different width.
//
//===----------------------------------------------------------------------===//

#include "src/inttypes/strtoimax.h"
#include "src/inttypes/strtoumax.h"
#include "src/stdlib/atoi.h"
#include "src/stdlib/atol.h"
#include "src/stdlib/atoll.h"
#include "src/stdlib/strtol.h"
#include "src/stdlib/strtoll.h"
#include "src/stdlib/strtoul.h"
#include "src/stdlib/strtoull.h"

#include <errno.h>

namespace lc = LIBC_NAMESPACE;

typedef __UINT32_TYPE__ u32;
typedef __UINT64_TYPE__ u64;

#ifdef __c166__
static void put(char c) { *(__far volatile unsigned char *)0xFF0000 = c; }
#else
extern "C" int putchar(int);
static void put(char c) { putchar(c); }
#endif

static void puthex(u64 v, int digits) {
  for (int i = digits - 1; i >= 0; i--)
    put("0123456789abcdef"[(unsigned)(v >> (i * 4)) & 0xF]);
  put('\n');
}

/// How far the parse got, as an offset rather than an address: the addresses
/// differ between the two machines by construction.
static void putend(const char *end, const char *base) {
  puthex((u32)(end - base), 4);
}

/// errno, reduced to the three outcomes these functions have.  Printing the
/// number itself would be printing whatever the host's <errno.h> happens to
/// use, which is not what is under test.
static void puterrno() {
  put(errno == 0 ? '0' : errno == ERANGE ? 'R' : errno == EINVAL ? 'I' : '?');
  put('\n');
}

/// Everything, saturation included.  Only the sixty four bit parsers see this
/// list, because only they mean the same thing on both machines.
static const char *const Inputs[] = {
    // Ordinary.
    "0", "1", "-1", "42", "  \t 42", "+42", "0x1f", "0X1F", "017", "1_000",
    // Nothing to parse, or nothing after the sign or the prefix.
    "", " ", "-", "+", "x", "0x", "0xg", "--1", " - 1",
    // Trailing text, which stops the parse and is not an error.
    "12abc", "12 34", "0x12zz", "7,8",
    // The edges of sixteen and thirty two bits, which are where int and long
    // are on the part.
    "32767", "32768", "-32768", "-32769", "65535", "65536",
    "2147483647", "2147483648", "-2147483648", "-2147483649", "4294967295",
    "4294967296",
    // The edges of sixty four bits, which is where long long is on both.
    "9223372036854775807", "9223372036854775808", "-9223372036854775808",
    "-9223372036854775809", "18446744073709551615", "18446744073709551616",
    // Past all of them, and long enough that the digit loop runs for a while
    // before it decides.
    "99999999999999999999999999", "-99999999999999999999999999",
    "0000000000000000000000000000000000000012",
};
static const unsigned NumInputs = sizeof(Inputs) / sizeof(Inputs[0]);

/// The same shapes, chosen so that neither machine saturates and the narrow
/// parsers can be compared too.  That means two things: read in any of the
/// bases below the value fits thirty two bits, which is what strtol, strtoul
/// and atol return on the part, and read in base ten it fits sixteen, which is
/// what atoi returns.  The cases with nothing to parse and with trailing text
/// are here as well, since those do not depend on a width at all.
static const char *const Narrow[] = {
    "0", "1", "-1", "42", "  \t 42", "+42", "0x1f", "0X1F", "017", "1_000",
    "", " ", "-", "+", "x", "0x", "0xg", "--1", " - 1",
    "12abc", "12 34", "0x12zz", "7,8",
    "32767", "-32768", "0000000000000000000000000000000000000012",
    "-0", "+0", "7fff", "-7fff", "z", "-z",
};
static const unsigned NumNarrow = sizeof(Narrow) / sizeof(Narrow[0]);

static const int Bases[] = {0, 2, 8, 10, 16, 36};
static const unsigned NumBases = sizeof(Bases) / sizeof(Bases[0]);

/// The sixty four bit parsers, at every base, over everything.
static void wide() {
  for (unsigned i = 0; i != NumInputs; i++) {
    const char *S = Inputs[i];
    for (unsigned b = 0; b != NumBases; b++) {
      char *End;

      errno = 0;
      puthex((u64)lc::strtoll(S, &End, Bases[b]), 16);
      putend(End, S);
      puterrno();

      errno = 0;
      puthex(lc::strtoull(S, &End, Bases[b]), 16);
      putend(End, S);
      puterrno();

      errno = 0;
      puthex((u64)lc::strtoimax(S, &End, Bases[b]), 16);
      putend(End, S);
      puterrno();

      errno = 0;
      puthex(lc::strtoumax(S, &End, Bases[b]), 16);
      putend(End, S);
      puterrno();
    }

    errno = 0;
    puthex((u64)lc::atoll(S), 16);
    puterrno();
  }
}

/// The parsers whose return type is narrower on the part than on the machine
/// this is checked against, over the inputs where that does not show.
static void narrow() {
  for (unsigned i = 0; i != NumNarrow; i++) {
    const char *S = Narrow[i];
    for (unsigned b = 0; b != NumBases; b++) {
      char *End;

      errno = 0;
      puthex((u32)lc::strtol(S, &End, Bases[b]), 8);
      putend(End, S);
      puterrno();

      errno = 0;
      puthex((u32)lc::strtoul(S, &End, Bases[b]), 8);
      putend(End, S);
      puterrno();
    }

    errno = 0;
    puthex((u32)lc::atol(S), 8);
    puterrno();

    errno = 0;
    puthex((unsigned)(unsigned short)lc::atoi(S), 4);
    puterrno();
  }
}

int main() {
  wide();
  narrow();
  return 0;
}
