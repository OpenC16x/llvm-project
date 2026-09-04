// A pointer on this part is 16 bits or 32, and which one is a property of the
// address space it points into.  Nothing in DW_TAG_pointer_type says so by
// itself, so a debugger handed "__far const char *" and "const char *" sees
// two entries that are identical - and reads two bytes of a four byte pointer,
// which is not a cosmetic error.
//
// DW_AT_address_class is what DWARF has for it and its values are the target's
// to choose; these are the LLVM address space numbers, so the debug
// information says the same number the IR does.  The near space is zero and is
// left out, because it is the default and a reader that knows nothing about
// this target should still be able to read a near pointer.
//
// RUN: %clang_cc1 -triple c166 -debug-info-kind=limited -emit-llvm %s -o - \
// RUN:   | FileCheck %s

__far const char *farp;
__seg const char *segp;
const char *nearp;

// CHECK-DAG: !DIDerivedType(tag: DW_TAG_pointer_type, baseType: ![[#]], size: 32, dwarfAddressSpace: 1)
// CHECK-DAG: !DIDerivedType(tag: DW_TAG_pointer_type, baseType: ![[#]], size: 32, dwarfAddressSpace: 2)

// The near one carries no address class at all.
// CHECK-DAG: !DIDerivedType(tag: DW_TAG_pointer_type, baseType: ![[#]], size: 16)

void use(void) {
  (void)farp;
  (void)segp;
  (void)nearp;
}
