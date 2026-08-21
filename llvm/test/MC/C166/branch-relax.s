; RUN: llvm-mc -triple=c166 -filetype=obj %s -o %t.o
; RUN: llvm-objdump -d %t.o | FileCheck %s
; RUN: llvm-readobj -r %t.o | FileCheck %s --check-prefix=RELOC

; JMPR reaches 127 words either way, and whether a target is that close is not
; known until the layout is, so the assembler grows the ones that are not into
; the four byte JMPA.  The long form takes an absolute address, which is not
; known until the link, so what is left behind is a relocation.

        .text
; A target close enough stays two bytes.
; CHECK: 0: 0d 01        jmpr cc_UC, 0x4
        jmpr    cc_UC, near
        nop
near:
        nop

; One that is not becomes four, and needs the address relocating.
; CHECK: 6: ea 30 00 00  jmpa cc_NE, 0
        jmpr    cc_nz, far
        .space  256, 0xcc
far:
        nop

; A displacement written as a number is a distance rather than a label, so
; there is nothing to grow it into and it stays as written.
; CHECK: 10c: 0d 7f        jmpr cc_UC, 0x20c
        jmpr    cc_UC, 127

; RELOC:      Section ({{.*}}) .rela.text {
; RELOC-NEXT:   0x8 R_C166_ABS16 .text 0x10A
; RELOC-NEXT: }
