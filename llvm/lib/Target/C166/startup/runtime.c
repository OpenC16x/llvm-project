//===-- runtime.c - The objects the sysroot headers promise ---------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// A header that declares something and nothing that defines it is a link error
// with a name on it, which is the right answer for most of what a hosted C
// library has and this part does not.  These are the exceptions: errno, the
// three ways a program stops and the assertion handler are all promised by the
// headers in include/, so here they are.
//
// Everything here stops the machine rather than returning, because on a part
// with no operating system there is nothing to return to.  __c166_exit is the
// same place crt0 goes when main returns.
//
//===----------------------------------------------------------------------===//

/// The one errno.  <errno.h> defines the errno macro to this; there is one
/// thread, so there is one of these.
int __c166_errno;

void __c166_exit(void);

/// The three ways a program stops, and the assertion handler, all of which
/// are weak: a part with a watchdog to kick, a pin to pull or something to
/// print before it stops replaces the one it cares about and gets the rest of
/// these unchanged.
///
/// exit runs no handler, because nothing here registers one: <stdlib.h>
/// deliberately does not declare atexit, so there is no table to walk and no
/// way for a program to have put anything in it.
__attribute__((weak, noreturn)) void exit(int status) {
  (void)status;
  __c166_exit();
  __builtin_unreachable();
}

__attribute__((weak, noreturn)) void _Exit(int status) {
  (void)status;
  __c166_exit();
  __builtin_unreachable();
}

__attribute__((weak, noreturn)) void abort(void) {
  __c166_exit();
  __builtin_unreachable();
}

/// Where a failed assertion goes.  There is no stderr, so the three arguments
/// go nowhere by default; they are there so that a program which replaces
/// this can print them.
__attribute__((weak, noreturn)) void
__c166_assert_failed(const char *expr, const char *file, unsigned line) {
  (void)expr;
  (void)file;
  (void)line;
  __c166_exit();
  __builtin_unreachable();
}

/// Where a prologue that found R0 below __user_stack_limit goes, under
/// -mstack-check.  The compiler jumps here rather than calling, because the
/// frame that would hold the return address is the one that did not fit, and
/// because there is nothing to go back to: by the time this runs the stack
/// pointer is already below the memory it is allowed to use, so anything this
/// does that needs a frame of its own makes it worse.
///
/// It stops the machine.  A program that wants to say something first, or to
/// reset R0 to the top and carry on, defines its own - the symbol is weak and
/// takes no arguments and returns nothing, so a replacement is four lines.
///
/// A replacement must need no frame of its own, which is why this one is a
/// call and nothing else.  That is not only a rule about this function: it is
/// also what keeps the check from finding itself, since -mstack-check only
/// puts the comparison in a prologue that allocates something.
__attribute__((weak, noreturn)) void __c166_stack_overflow(void) {
  __c166_exit();
  __builtin_unreachable();
}
