//===-- udivsi3.c - Implement __udivsi3 -----------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// __udivsi3 for the C166, using the part's divide unit for the case the
// generic loop is slowest at.  See int_divmod.h.
//
//===----------------------------------------------------------------------===//

#include "int_divmod.h"

typedef su_int fixuint_t;
typedef si_int fixint_t;
#include "../int_div_impl.inc"

// Returns: a / b

COMPILER_RT_ABI su_int __udivsi3(su_int a, su_int b) {
  uwords d;
  d.all = b;
  if (d.s.high == 0)
    return c166_udiv_word(a, d.s.low);
  return __udivXi3(a, b);
}
