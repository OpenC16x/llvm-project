// RUN: %clang_cc1 -triple c166 -emit-llvm -o - %s | FileCheck %s

// The backend keys off plain string function attributes, not a calling
// convention, so that is what clang has to attach.

__attribute__((interrupt)) void handler(void) {}
// CHECK: define {{.*}}void @handler() [[ISR:#[0-9]+]]

// A trap number asks for the vector table slot as well, and travels as a
// second string attribute the AsmPrinter reads.
__attribute__((interrupt(26))) void slotted(void) {}
// CHECK: define {{.*}}void @slotted() [[SLOTTED:#[0-9]+]]

__attribute__((far)) int distant(int x) { return x + 1; }
// CHECK: define {{.*}}i16 @distant(i16 noundef %x) [[FAR:#[0-9]+]]

// long_call is the MIPS spelling of the same idea, and is accepted here too.
__attribute__((long_call)) int also_distant(int x) { return x + 1; }
// CHECK: define {{.*}}i16 @also_distant(i16 noundef %x) [[FAR]]

int near_by(int x) { return x + 1; }
// CHECK: define {{.*}}i16 @near_by(i16 noundef %x) [[NEAR:#[0-9]+]]

// A far callee declared in another translation unit still has to be called
// with the wide sequence, so the attribute has to survive on a declaration.
__attribute__((far)) void elsewhere(void);
void caller(void) { elsewhere(); }
// CHECK: declare void @elsewhere() [[FARDECL:#[0-9]+]]

// CHECK-DAG: attributes [[ISR]] = { noinline {{.*}}"interrupt"{{.*}} }
// CHECK-DAG: attributes [[SLOTTED]] = { noinline {{.*}}"c166-interrupt-vector"="26"{{.*}}"interrupt"{{.*}} }
// CHECK-DAG: attributes [[FAR]] = { {{.*}}"far"{{.*}} }
// CHECK-DAG: attributes [[FARDECL]] = { "far"{{.*}} }
// CHECK-DAG: attributes [[NEAR]] = { noinline nounwind optnone "no-trapping-math"{{.*}} }
