// A __atomic_compare_exchange whose failure order is not a constant is emitted
// as a switch over the ordering.  The cases were built as i32 while the value
// switched on has the type of the language's int, so on a target where int is
// sixteen bits the switch was malformed and the verifier rejected the module.
//
// RUN: %clang_cc1 -triple msp430 -emit-llvm -o - %s | FileCheck %s
// RUN: %clang_cc1 -triple avr -emit-llvm -o - %s | FileCheck %s
// RUN: %clang_cc1 -triple c166 -emit-llvm -o - %s | FileCheck %s
// RUN: %clang_cc1 -triple x86_64-linux-gnu -emit-llvm -o - %s \
// RUN:     | FileCheck %s --check-prefix=WIDE

int f(int *p, int *expected, int desired, int failure) {
  return __atomic_compare_exchange_n(p, expected, desired, 0,
                                     __ATOMIC_SEQ_CST, failure);
}

// The ordering is widened to i32 so that it agrees with the cases.
//
// CHECK: [[ORDER:%.*]] = zext i16 %{{.*}} to i32
// CHECK: switch i32 [[ORDER]], label %monotonic_fail [
// CHECK-NEXT: i32 1, label %acquire_fail
// CHECK-NEXT: i32 2, label %acquire_fail
// CHECK-NEXT: i32 5, label %seqcst_fail

// Where int is already thirty two bits the widening is a no-op and the switch
// is on the value itself.
//
// WIDE: switch i32 %{{.*}}, label %monotonic_fail [
// WIDE-NEXT: i32 1, label %acquire_fail
// WIDE-NEXT: i32 2, label %acquire_fail
// WIDE-NEXT: i32 5, label %seqcst_fail
