// RUN: %clang_cc1 -triple c166 -target-cpu xc16x -emit-llvm -o - %s | FileCheck %s

// The pointer field of an indirect ALU form is two bits wide, so only R0 to R3
// fit in it.  "r" would hand back whichever register was spare.
// CHECK-LABEL: define {{.*}}i16 @indirect(
// CHECK: call i16 asm "add $0, [$1]", "=r,q,0"
short indirect(const short *p, short a) {
  short r;
  __asm__("add %0, [%1]" : "=r"(r) : "q"(p), "0"(a));
  return r;
}

// A byte operand needs no constraint of its own: "r" with a byte sized value
// gets a byte register, because R0 to R7 have byte halves.
// CHECK-LABEL: define {{.*}}i8 @byte(
// CHECK: call i8 asm "movb $0, $1", "=r,r"
unsigned char byte(unsigned char v) {
  unsigned char r;
  __asm__("movb %0, %1" : "=r"(r) : "r"(v));
  return r;
}

// The multiply-accumulate unit is memory mapped and nothing selects it, so an
// asm statement is the only way to reach it - and naming its registers is the
// only way to say which of the two pointers an indirect form runs on, since
// the instruction encodes that rather than taking a register operand.
// CHECK-LABEL: define {{.*}}void @macloop(
// CHECK: call void asm sideeffect "corepeat #3 times\0Acomacm [idx0+], [idx1+]", "{idx0},{idx1},~{mal},~{mah},~{msw}"
void macloop(const short *x, const short *y) {
  register const short *p __asm__("idx0") = x;
  register const short *q __asm__("idx1") = y;
  __asm__ volatile("corepeat #3 times\n"
                   "comacm [idx0+], [idx1+]"
                   :
                   : "r"(p), "r"(q)
                   : "mal", "mah", "msw");
}

// Saying an asm statement destroyed the unit, without pinning anything to it.
// CHECK-LABEL: define {{.*}}void @clobber(
// CHECK: call void asm sideeffect "comul r2, r3", "~{mal},~{mah},~{mas},~{mcw},~{msw},~{mrw},~{qx0},~{qx1},~{qr0},~{qr1}"
void clobber(void) {
  __asm__ volatile("comul r2, r3"
                   :
                   :
                   : "mal", "mah", "mas", "mcw", "msw", "mrw", "qx0", "qx1",
                     "qr0", "qr1");
}
