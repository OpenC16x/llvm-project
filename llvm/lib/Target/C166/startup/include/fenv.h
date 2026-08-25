/* The floating point environment of a part that has no floating point unit.
 *
 * Everything here is real, and most of it is empty, because the part is: there
 * is no hardware to raise an exception flag or to hold a rounding mode.
 * Floating point is compiler-rt's software implementation, which rounds to
 * nearest and reports nothing, so FE_ALL_EXCEPT is zero, no FE_ exception
 * macro is defined at all - which is what the standard asks for when a
 * particular exception cannot be raised - and FE_TONEAREST is the only
 * rounding direction there is.
 *
 * The functions are declared and defined nowhere.  Nothing in this library
 * calls one, and a program that does is asking the part for something it does
 * not have; a link error naming the function says that better than a stub
 * returning zero would.
 *
 * Part of the LLVM Project, under the Apache License v2.0 with LLVM
 * Exceptions.  See https://llvm.org/LICENSE.txt for license information.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */

#ifndef _C166_FENV_H
#define _C166_FENV_H

/* Nothing to hold, but the standard requires object types. */
typedef struct {
  unsigned short __unused;
} fenv_t;

typedef unsigned short fexcept_t;

#define FE_ALL_EXCEPT 0
#define FE_TONEAREST 0

#define FE_DFL_ENV ((const fenv_t *)0)

#ifdef __cplusplus
extern "C" {
#endif

int feclearexcept(int excepts);
int fegetexceptflag(fexcept_t *flagp, int excepts);
int feraiseexcept(int excepts);
int fesetexceptflag(const fexcept_t *flagp, int excepts);
int fetestexcept(int excepts);

int fegetround(void);
int fesetround(int round);

int fegetenv(fenv_t *envp);
int feholdexcept(fenv_t *envp);
int fesetenv(const fenv_t *envp);
int feupdateenv(const fenv_t *envp);

#ifdef __cplusplus
}
#endif

#endif /* _C166_FENV_H */
