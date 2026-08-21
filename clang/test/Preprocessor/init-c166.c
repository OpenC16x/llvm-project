// RUN: %clang_cc1 -E -dM -ffreestanding -triple=c166 < /dev/null | FileCheck -match-full-lines %s

// CHECK: #define C166 1
// CHECK: #define __C166__ 1
// CHECK: #define __c166__ 1
