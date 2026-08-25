/* assert, which on a part with nowhere to print stops the machine.
 *
 * There is no stderr and no abort message, so a failed assertion calls
 * __c166_assert_failed with where it was.  That is weak in the C library and
 * spins; a program that wants to say something first, or to reset, defines its
 * own.
 *
 * Part of the LLVM Project, under the Apache License v2.0 with LLVM
 * Exceptions.  See https://llvm.org/LICENSE.txt for license information.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */

/* Deliberately no include guard on the whole file: assert.h may be included
 * more than once, and NDEBUG is read each time. */
#ifdef __cplusplus
extern "C" {
#endif

__attribute__((noreturn)) void __c166_assert_failed(const char *expr,
                                                   const char *file,
                                                   unsigned line);

#ifdef __cplusplus
}
#endif

#undef assert
#ifdef NDEBUG
#define assert(e) ((void)0)
#else
#define assert(e)                                                              \
  ((e) ? (void)0 : __c166_assert_failed(#e, __FILE__, __LINE__))
#endif

#ifndef static_assert
#define static_assert _Static_assert
#endif
