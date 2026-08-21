; RUN: llvm-mc -triple=c166 -filetype=obj %s -o %t.o
; RUN: llvm-readobj -r %t.o | FileCheck %s --check-prefix=RELOC
; RUN: llvm-objdump -d %t.o | FileCheck %s --check-prefix=DISASM

; A branch inside the section is worked out here; one to a symbol the assembler
; cannot place leaves R_C166_PCREL8W behind, whose field stays zero because the
; whole distance travels in the addend.

        .text
here:
        nop
        jb      r5.3, here
        jnb     r5.3, there
there:
        nop
        jbc     r5.3, elsewhere

; RELOC:      Section ({{.*}}) .rela.text {
; RELOC-NEXT:   0xE R_C166_PCREL8W elsewhere 0x0
; RELOC-NEXT: }

; llvm-objdump asks for the target address rather than the distance to it.
; DISASM:      2: 8a f5 fd 30  jb   r5.3, 0x0
; DISASM-NEXT: 6: 9a f5 00 30  jnb  r5.3, 0xa
; DISASM:      c: aa f5 00 30  jbc  r5.3, 0x10
