// RUN: %clang_cc1 -triple c166 -target-cpu xc16x -fsyntax-only -verify %s

// The register names an asm statement may use.  The list is flat, as a GCC
// register list is, so it does not know which part is selected: naming a
// coprocessor register on a part without the unit is accepted here and refused
// where the map is known - by the assembler for hand written assembly, and by
// the code generator for a value pinned to one.  See
// llvm/test/CodeGen/C166/inline-asm-sfr-unsupported.ll.
void ok(void) {
  __asm__ volatile("nop" ::: "r0", "r15", "rl0", "rh7");
  __asm__ volatile("nop" ::: "psw", "mdl", "mdh", "mdc", "sp", "cp");
  __asm__ volatile("nop" ::: "idx0", "idx1", "qx0", "qx1", "qr0", "qr1");
  __asm__ volatile("nop" ::: "mal", "mah", "mas", "mcw", "msw", "mrw");
}

void bad(void) {
  __asm__ volatile("nop" ::: "r16");      // expected-error {{unknown register name 'r16' in asm}}
  __asm__ volatile("nop" ::: "idx2");     // expected-error {{unknown register name 'idx2' in asm}}
  __asm__ volatile("nop" ::: "acc");      // expected-error {{unknown register name 'acc' in asm}}
}

// The two constraints that name a register class.
void constraints(const short *p, short a) {
  short r;
  __asm__("add %0, [%1]" : "=r"(r) : "q"(p), "0"(a));
  __asm__("nop" : "=r"(r) : "r"(a));
}

void unknown_constraint(short a) {
  __asm__("nop" : : "z"(a));              // expected-error {{invalid input constraint 'z' in asm}}
}
