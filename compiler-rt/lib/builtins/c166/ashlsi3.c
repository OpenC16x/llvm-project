//===-- ashlsi3.c - Implement __ashlsi3 -----------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements __ashlsi3 for the compiler_rt library.  A machine with
// a 32 bit word shifts an si_int with one instruction; a 16 bit one has to be
// told how, the same way ashldi3.c tells a 32 bit machine how to shift a
// di_int.
//
//===----------------------------------------------------------------------===//

#include "int_words.h"

// Returns: a << b

// Precondition:  0 <= b < bits_in_word

COMPILER_RT_ABI si_int __ashlsi3(si_int a, int b) {
  uwords input;
  uwords result;
  input.all = a;
  if (b & BITS_IN_HWORD) /* BITS_IN_HWORD <= b < bits_in_word */ {
    result.s.low = 0;
    result.s.high = input.s.low << (b - BITS_IN_HWORD);
  } else /* 0 <= b < BITS_IN_HWORD */ {
    if (b == 0)
      return a;
    result.s.low = input.s.low << b;
    result.s.high = (input.s.high << b) | (input.s.low >> (BITS_IN_HWORD - b));
  }
  return result.all;
}
