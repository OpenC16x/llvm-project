// RUN: %clang_cc1 -triple c166 -emit-llvm -o - %s | FileCheck %s

// Address space 1 is the far space: a 24 bit address zero extended into a 32
// bit pointer.  __far is predefined as a shorthand for it.

// CHECK: target datalayout = "e-m:e-p:16:16-p1:32:16-{{.*}}"

int near_object;
__far int far_object;

// CHECK: @near_object = global i16 0, align 2
// CHECK: @far_object = addrspace(1) global i16 0, align 2

int read_far(__far int *p) { return *p; }
// CHECK-LABEL: define {{.*}}i16 @read_far(ptr addrspace(1) noundef %p)
// CHECK: load i16, ptr addrspace(1)

void write_far(__far int *p, int v) { *p = v; }
// CHECK-LABEL: define {{.*}}void @write_far(ptr addrspace(1) noundef %p, i16 noundef %v)
// CHECK: store i16 %{{.*}}, ptr addrspace(1)

// A near pointer converts to a far one without a cast: every near object has a
// far address.
__far int *widen(int *p) { return p; }
// CHECK-LABEL: define {{.*}}ptr addrspace(1) @widen(ptr noundef %p)
// CHECK: addrspacecast ptr %{{.*}} to ptr addrspace(1)

__far int *take_address(void) { return &near_object; }
// CHECK-LABEL: define {{.*}}ptr addrspace(1) @take_address()
// CHECK: ret ptr addrspace(1) addrspacecast (ptr @near_object to ptr addrspace(1))
