//===-- int_divmod.h - 32 bit division on a 16 bit divide unit ------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// The generic 32 bit division helpers are a shift and subtract loop over 32
// bit values, and on a 16 bit machine every one of those shifts is several
// instructions.  Measured on this target, __udivsi3 is 143 instructions and
// __divsi3 is 31 more on top of it.
//
// The part has a divide unit.  DIVU divides a word by a word and DIVLU divides
// MDH:MDL - a double word - by a word, so two of them in sequence divide a 32
// bit dividend by a 16 bit divisor exactly.  The backend already emits that
// pair; see ReplaceDivBy16Results in C166ISelLowering.cpp for why neither step
// can overflow.
//
// What is left for the loop is a divisor too wide for a word, and there the
// loop is already short: it runs clz(d) - clz(n) + 1 times, so a divisor of
// 65536 or more makes that at most 16 rather than 32.  The case the generic
// code is slowest at is the small divisor, which is exactly the case the
// hardware does in two instructions.
//
//===----------------------------------------------------------------------===//

#ifndef C166_INT_DIVMOD_H
#define C166_INT_DIVMOD_H

#include "int_words.h"

// Why these take the divisor as a 16 bit parameter, and why they are noinline.
//
// The backend turns a 32 bit division into DIVU and DIVLU when it can see that
// the divisor is genuinely 16 bits, and it decides that by asking what is
// known about the value.  Written inline behind a test of the high word:
//
//     if (d.s.high == 0) return n / (su_int)d.s.low;
//
// the optimiser proves the narrowing redundant - the branch it sits under
// already says the high word is zero - and removes it.  What is left is a 32
// by 32 division, which is a call to the routine this is the body of: the
// helper would recurse forever.  Taking the divisor as a parameter of 16 bit
// type keeps the fact in the value rather than in the control flow, and
// noinline is what stops the call being folded back into the branch.
//
// That is a property of the emitted code rather than of the source, so it is
// checked by a test: llvm/test/CodeGen/C166/div-runtime-helpers.ll.

/// n / d, for a divisor that fits in a word.
static __attribute__((noinline)) su_int c166_udiv_word(su_int n, hu_int d) {
  return n / (su_int)d;
}

/// n % d, for a divisor that fits in a word.  The remainder comes back out of
/// MDH, where DIVLU left it.
static __attribute__((noinline)) su_int c166_umod_word(su_int n, hu_int d) {
  return n % (su_int)d;
}

/// Both at once.  Only __udivmodsi4 wants both, and asking for them together
/// costs a multiply and a subtract on top of the pair: the combiner rewrites
/// the second as n - (n / d) * d before the divide ever reaches the backend.
/// That is still far below what the loop costs, and the two routines above
/// stay minimal for the callers that want one answer.
static __attribute__((noinline)) su_int c166_udivmod_word(su_int n, hu_int d,
                                                          su_int *rem) {
  *rem = n % (su_int)d;
  return n / (su_int)d;
}

#endif // C166_INT_DIVMOD_H
