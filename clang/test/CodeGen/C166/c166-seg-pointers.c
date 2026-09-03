// RUN: %clang_cc1 -triple c166 -emit-llvm -o - %s | FileCheck %s

// Address space 2 is a far pointer that has been promised to stay inside one
// segment: the same 24 bit address in the same 32 bits, but with arithmetic
// that touches only the low sixteen.  __seg is predefined as a shorthand for
// it.  The promise is in the data layout, which gives the space an index size
// of sixteen -- the language reference makes that mean exactly this.

// CHECK: target datalayout = "{{.*}}-p2:32:16:16:16-{{.*}}"

_Static_assert(sizeof(__seg int *) == sizeof(__far int *),
               "the same address in the same bits");

__seg int seg_object;
// CHECK: @seg_object = addrspace(2) global i16 0, align 2

int read_seg(__seg int *p) { return *p; }
// CHECK-LABEL: define {{.*}}i16 @read_seg(ptr addrspace(2) noundef %p)
// CHECK: load i16, ptr addrspace(2)

// A step through one is a getelementptr like any other; what makes it sixteen
// bit arithmetic is the index size in the data layout above, which the
// language reference says applies to the getelementptr itself.  What that
// turns into is checked in llvm/test/CodeGen/C166/seg-pointer.ll.
int step(__seg int *p, int i) { return p[i]; }
// CHECK-LABEL: define {{.*}}i16 @step(
// CHECK: getelementptr inbounds i16, ptr addrspace(2)

// Dropping the promise is always safe, so it needs no cast: a pointer that
// stays inside one segment is a far pointer.
__far int *widen(__seg int *p) { return p; }
// CHECK-LABEL: define {{.*}}ptr addrspace(1) @widen(ptr addrspace(2) noundef %p)
// CHECK: addrspacecast ptr addrspace(2) %{{.*}} to ptr addrspace(1)

// A near pointer converts to one without a cast too: under the reset
// configuration of the data page pointers the whole near space is segment 0,
// so a near pointer is already confined to one segment.
__seg int *from_near(int *p) { return p; }
// CHECK-LABEL: define {{.*}}ptr addrspace(2) @from_near(ptr noundef %p)
// CHECK: addrspacecast ptr %{{.*}} to ptr addrspace(2)

// Making the promise is what needs the cast; the representation does not
// change, so the cast costs nothing.
__seg int *narrow(__far int *p) { return (__seg int *)p; }
// CHECK-LABEL: define {{.*}}ptr addrspace(2) @narrow(ptr addrspace(1) noundef %p)
// CHECK: addrspacecast ptr addrspace(1) %{{.*}} to ptr addrspace(2)

// The difference of two pointers is a ptrdiff_t, which is int here and so is
// sixteen bits wide whichever space the pointers are in.
int diff(__seg int *a, __seg int *b) { return a - b; }
// CHECK-LABEL: define {{.*}}i16 @diff(
// CHECK: %[[L:.*]] = ptrtoaddr ptr addrspace(2) %{{.*}} to i16
// CHECK: %[[R:.*]] = ptrtoaddr ptr addrspace(2) %{{.*}} to i16
// CHECK: sub i16 %[[L]], %[[R]]

int far_diff(__far int *a, __far int *b) { return a - b; }
// CHECK-LABEL: define {{.*}}i16 @far_diff(
// CHECK: ptrtoaddr ptr addrspace(1) %{{.*}} to i32
// CHECK: trunc i32 %{{.*}} to i16
// CHECK: sub i16
