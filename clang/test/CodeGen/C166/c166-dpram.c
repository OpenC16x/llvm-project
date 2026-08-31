// RUN: %clang_cc1 -triple c166 -target-cpu xc16x -emit-llvm -o - %s | FileCheck %s
// RUN: %clang_cc1 -triple c166 -target-cpu xc16x -S -o - %s | FileCheck %s --check-prefix=ASM

// The attribute travels to the backend as a string attribute on the global
// rather than as a section name, so that the backend picks the section - which
// is where every other section decision on this target is made, and which is
// what gets a zero initialised object a NOBITS section instead of one carrying
// its zeroes in the image.
// CHECK: @seeded = global [4 x i16] [i16 1, i16 2, i16 3, i16 4], align 2 #[[DP:[0-9]+]]
// CHECK: @delay = global [8 x i16] zeroinitializer, align 2 #[[DP]]
// CHECK: @ordinary = global [8 x i16] zeroinitializer, align 2{{$}}
// CHECK: attributes #[[DP]] = { "c166-dpram" }
__dpram short delay[8];
__dpram short seeded[4] = {1, 2, 3, 4};
short ordinary[8];

// ASM: .section .dpramdata,"aw",@progbits
// ASM: seeded:
// ASM: .section .dprambss,"aw",@nobits
// ASM: delay:
