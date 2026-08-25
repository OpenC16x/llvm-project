// size_t here is narrower than the widest pointer: near pointers are sixteen
// bits and far pointers thirty two, and CodeGenTypeCache used to union SizeTy
// with IntPtrTy, which is built from the wider of the two.  That made
// "new char[n]" compute an i32 allocation size and pass it to an operator new
// declared to take an i16, which asserted in ZExtInst before this was split.
//
// RUN: %clang_cc1 -triple c166 -emit-llvm -o - %s | FileCheck %s

typedef __SIZE_TYPE__ size_t;
void *operator new(size_t);
void *operator new[](size_t);
void operator delete[](void *) noexcept;

// The mangling is the check that the parameter is the language's size_t:
// j is unsigned int, which is sixteen bits here.  An i32 would be m.
//
// CHECK-LABEL: define {{.*}} @_Z5arrayj(
// CHECK: call {{.*}} @_Znaj(i16
char *array(unsigned n) { return new char[n]; }

// CHECK-LABEL: define {{.*}} @_Z6scalarv(
// CHECK: call {{.*}} @_Znwj(i16 noundef 4)
struct Pair { int a, b; };
Pair *scalar() { return new Pair; }

// The array cookie holds a size_t, so it is two bytes here rather than the
// four the widest pointer would have made it: the count is stored as an i16,
// aligned to two, and the returned pointer is two bytes past the allocation.
// The GEP index is not a size_t and stays at pointer width.
struct WithDtor { int a; ~WithDtor(); };
// CHECK-LABEL: define {{.*}} @_Z6cookiej(
// CHECK: store i16 %{{.*}}, ptr %{{.*}}, align 2
// CHECK: getelementptr inbounds{{.*}} i8, ptr %{{.*}}, i32 2
WithDtor *cookie(unsigned n) { return new WithDtor[n]; }
