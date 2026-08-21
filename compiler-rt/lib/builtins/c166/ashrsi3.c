//===-- ashrsi3.c - Implement __ashrsi3 -----------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements __ashrsi3 for the compiler_rt library.  See ashlsi3.c
// for why a 16 bit machine needs it at all.
//
//===----------------------------------------------------------------------===//

#include "int_words.h"

// Returns: arithmetic a >> b

// Precondition:  0 <= b < bits_in_word

COMPILER_RT_ABI si_int __ashrsi3(si_int a, int b) {
  words input;
  words result;
  input.all = a;
  if (b & BITS_IN_HWORD) /* BITS_IN_HWORD <= b < bits_in_word */ {
    // result.s.high = input.s.high < 0 ? -1 : 0
    result.s.high = input.s.high >> (BITS_IN_HWORD - 1);
    result.s.low = input.s.high >> (b - BITS_IN_HWORD);
  } else /* 0 <= b < BITS_IN_HWORD */ {
    if (b == 0)
      return a;
    result.s.high = input.s.high >> b;
    result.s.low =
        ((hu_int)input.s.high << (BITS_IN_HWORD - b)) | (input.s.low >> b);
  }
  return result.all;
}
