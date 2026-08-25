// _BitInt of any width, which the target did not say it had.  LLVM's own libc
// builds its word-at-a-time string loops out of it - libc/src/__support/CPP/
// simd.h - so twenty two of its sources would not compile for this target at
// all, which is how the gap turned up.
//
// The backend legalises an odd width the way it legalises anything else that
// is not sixteen bits wide.
//
// RUN: %clang_cc1 -triple c166 -emit-llvm -o - %s | FileCheck %s
//
// And it has to reach the end, not only be accepted by the front end: an
// odd width that no register holds is expanded by the legalizer, and this is
// what says the target's own lowering copes with the result.
// RUN: %clang_cc1 -triple c166 -S -o /dev/null %s

// CHECK-LABEL: define {{.*}} @narrow(
// CHECK: mul {{.*}}i5 
unsigned _BitInt(5) narrow(unsigned _BitInt(5) a, unsigned _BitInt(5) b) {
  return a * b;
}

// A width the ALU has no register for at all, so it is expanded rather than
// promoted.
// CHECK-LABEL: define {{.*}} @wide(
// CHECK: mul {{.*}}i37 
_BitInt(37) wide(_BitInt(37) a, _BitInt(37) b) { return a * b; }

// CHECK-LABEL: define {{.*}} @divide(
// CHECK: sdiv i37 
_BitInt(37) divide(_BitInt(37) a, _BitInt(37) b) { return a / b; }

// The one that is exactly a register.
// CHECK-LABEL: define {{.*}} @exact(
// CHECK: add {{.*}}i16 
_BitInt(16) exact(_BitInt(16) a, _BitInt(16) b) { return a + b; }
