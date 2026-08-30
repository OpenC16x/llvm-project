//===-- udivmodsi4.c - Implement __udivmodsi4 -----------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// __udivmodsi4 for the C166.  See int_divmod.h.
//
//===----------------------------------------------------------------------===//

#include "int_divmod.h"

typedef su_int fixuint_t;
typedef si_int fixint_t;
#include "../int_div_impl.inc"

// Returns: a / b, and *rem = a % b

COMPILER_RT_ABI su_int __udivmodsi4(su_int a, su_int b, su_int *rem) {
  uwords d;
  d.all = b;
  if (d.s.high == 0)
    return c166_udivmod_word(a, d.s.low, rem);
  su_int q = __udivXi3(a, b);
  *rem = a - q * b;
  return q;
}
