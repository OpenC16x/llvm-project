; RUN: llc -mtriple=c166 -filetype=obj < %s -o %t.o
; RUN: llvm-readobj --file-headers -r %t.o | FileCheck %s
; RUN: llvm-objdump -d --triple=c166 %t.o | FileCheck %s --check-prefix=DISASM

; Every relocatable field of a C166 instruction lives in the second word, so a
; relocation always lands two bytes into the instruction.

; CHECK: Format: elf32-c166
; CHECK: Machine: EM_C166

@g = external global i16
declare void @callee()

define void @f() {
  %v = load volatile i16, ptr @g
  store volatile i16 %v, ptr @g
  call void @callee()
  %p = ptrtoint ptr @g to i16
  store volatile i16 %p, ptr @g
  ret void
}

; DISASM:      <f>:
; DISASM-NEXT:   0: f2 f2 00 00   mov r2, 0
; DISASM-NEXT:   4: f6 f2 00 00   mov 0, r2
; DISASM-NEXT:   8: ca 00 00 00   calla cc_UC, 0
; DISASM-NEXT:   c: e6 f2 00 00   mov r2, #0
; DISASM-NEXT:  10: f6 f2 00 00   mov 0, r2
; DISASM-NEXT:  14: cb 00         ret

; CHECK:      Section {{.*}} .rela.text {
; CHECK-NEXT:   0x2 R_C166_SOF16 g 0x0
; CHECK-NEXT:   0x6 R_C166_SOF16 g 0x0
; CHECK-NEXT:   0xA R_C166_SOF16 callee 0x0
; CHECK-NEXT:   0xE R_C166_SOF16 g 0x0
; CHECK-NEXT:   0x12 R_C166_SOF16 g 0x0
; CHECK-NEXT: }
