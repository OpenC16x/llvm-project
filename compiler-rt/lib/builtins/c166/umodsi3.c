//===-- umodsi3.c - Implement __umodsi3 -----------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// __umodsi3 for the C166.  See int_divmod.h.
//
//===----------------------------------------------------------------------===//

#include "int_divmod.h"

typedef su_int fixuint_t;
typedef si_int fixint_t;
#include "../int_div_impl.inc"

// Returns: a % b

COMPILER_RT_ABI su_int __umodsi3(su_int a, su_int b) {
  uwords d;
  d.all = b;
  if (d.s.high == 0)
    return c166_umod_word(a, d.s.low);
  return __umodXi3(a, b);
}
