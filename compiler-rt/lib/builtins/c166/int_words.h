//===-- int_words.h - 32 bit values as pairs of 16 bit halves -------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// int_types.h splits a 64 bit value into two 32 bit halves, which is what a
// machine with a 32 bit word needs.  A machine with a 16 bit word needs the
// same thing one size down, to write the SI mode helpers that a wider machine
// gets from its own instruction set.
//
// Nothing here is C166 specific; it lives under c166/ because that is the only
// target that builds it so far.
//
//===----------------------------------------------------------------------===//

#ifndef C166_INT_WORDS_H
#define C166_INT_WORDS_H

#include "../int_lib.h"

typedef int16_t hi_int;
typedef uint16_t hu_int;

#define BITS_IN_HWORD 16

typedef union {
  si_int all;
  struct {
#if _YUGA_LITTLE_ENDIAN
    hu_int low;
    hi_int high;
#else
    hi_int high;
    hu_int low;
#endif // _YUGA_LITTLE_ENDIAN
  } s;
} words;

typedef union {
  su_int all;
  struct {
#if _YUGA_LITTLE_ENDIAN
    hu_int low;
    hu_int high;
#else
    hu_int high;
    hu_int low;
#endif // _YUGA_LITTLE_ENDIAN
  } s;
} uwords;

#endif // C166_INT_WORDS_H
