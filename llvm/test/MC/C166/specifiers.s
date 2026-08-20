; RUN: llvm-mc -triple=c166 -show-encoding < %s | FileCheck %s
; RUN: llvm-mc -triple=c166 -filetype=obj < %s -o %t.o
; RUN: llvm-readobj -r %t.o | FileCheck %s --check-prefix=RELOC

; A 24 bit address is split by one of four operators.  Against a symbol each
; becomes a relocation and the linker does the work; over something the
; assembler can already fold it is worked out on the spot.

; CHECK: exts #seg(table), #1 ; encoding: [0xd7,0x00,A,0x00]
; CHECK-NEXT: fixup A - offset: 2, value: seg(table), kind: FK_Data_1
        exts    #seg(table), #1
; CHECK: mov r2, sof(table) ; encoding: [0xf2,0xf2,A,A]
; CHECK-NEXT: fixup A - offset: 2, value: sof(table), kind: FK_Data_2
        mov     r2, sof(table)
; CHECK: extp #pag(table), #1 ; encoding: [0xd7,0x40,A,A]
; CHECK-NEXT: fixup A - offset: 2, value: pag(table), kind: FK_Data_2
        extp    #pag(table), #1
; CHECK: mov r3, pof(table) ; encoding: [0xf2,0xf3,A,A]
; CHECK-NEXT: fixup A - offset: 2, value: pof(table), kind: FK_Data_2
        mov     r3, pof(table)

; JMPS and CALLS keep their segment in the first word, so its fixup is one
; byte in rather than two.
; CHECK: jmps #seg(faraway), sof(faraway) ; encoding: [0xfa,A,B,B]
; CHECK-NEXT: fixup A - offset: 1, value: seg(faraway), kind: FK_Data_1
; CHECK-NEXT: fixup B - offset: 2, value: sof(faraway), kind: FK_Data_2
        jmps    #seg(faraway), sof(faraway)
; CHECK: calls #seg(faraway), sof(faraway) ; encoding: [0xda,A,B,B]
; CHECK-NEXT: fixup A - offset: 1, value: seg(faraway), kind: FK_Data_1
; CHECK-NEXT: fixup B - offset: 2, value: sof(faraway), kind: FK_Data_2
        calls   #seg(faraway), sof(faraway)

; An immediate and a memory displacement take them too.
; CHECK: mov r4, #sof(table) ; encoding: [0xe6,0xf4,A,A]
        mov     r4, #sof(table)
; CHECK: mov r5, [r6+#sof(table)] ; encoding: [0xd4,0x56,A,A]
        mov     r5, [r6+#sof(table)]

; RELOC:      Section {{.*}} .rela.text {
; RELOC-NEXT:   0x2 R_C166_SEG8 table 0x0
; RELOC-NEXT:   0x6 R_C166_SOF16 table 0x0
; RELOC-NEXT:   0xA R_C166_PAG10 table 0x0
; RELOC-NEXT:   0xE R_C166_POF14 table 0x0
; RELOC-NEXT:   0x11 R_C166_SEG8 faraway 0x0
; RELOC-NEXT:   0x12 R_C166_SOF16 faraway 0x0
; RELOC-NEXT:   0x15 R_C166_SEG8 faraway 0x0
; RELOC-NEXT:   0x16 R_C166_SOF16 faraway 0x0
; RELOC-NEXT:   0x1A R_C166_SOF16 table 0x0
; RELOC-NEXT:   0x1E R_C166_SOF16 table 0x0
; RELOC-NEXT: }
