// This part runs one thread, so thread_local is accepted rather than rejected:
// per-thread storage and static storage are the same storage here, and the
// backend lowers one to the other.  Rejecting it only pushes portable code that
// uses thread_local as a "one per thread, and there is one thread" idiom off
// this target - LLVM libc's l64a is the case that turned this up.
//
// RUN: %clang_cc1 -triple c166 -emit-llvm -o - %s | FileCheck %s
// RUN: %clang_cc1 -triple c166 -fsyntax-only -verify %s
// expected-no-diagnostics

// The front end still emits these as thread-local; making them ordinary data
// is the backend's job, so that IR from any front end gets the same treatment.
//
// CHECK-DAG: @counter = thread_local global i16 0
// CHECK-DAG: @seeded = thread_local global i16 7
//
// A file scope static thread_local is the same thing with internal linkage,
// and is what libc/src/stdlib/l64a.cpp uses.
//
// CHECK-DAG: @scratch = internal thread_local global
_Thread_local int counter;
_Thread_local int seeded = 7;
static _Thread_local char scratch[8];

// CHECK-LABEL: define {{.*}} @bump(
// CHECK: call {{.*}}ptr @llvm.threadlocal.address.p0(ptr {{.*}}@counter)
int bump(void) { return ++counter; }

char *buffer(void) { return scratch; }
int get(void) { return seeded; }
