;; A JMPA or CALLA takes its segment from CSP, so it reaches the segment the
;; instruction is in and no other.  Nothing in the instruction says which one
;; that is, so a target somewhere else is not something the assembler can catch
;; and not something the sixteen bits that get written would show: the branch
;; simply goes to that offset in the wrong segment.
;;
;; It is a real way to get there.  A function placed in the PSRAM at E0'0000H -
;; which is what that memory is for - calling an ordinary one in the Flash at
;; C0'xxxxH is exactly this, and c166.ld puts those two in different segments on
;; purpose.  So the relocation on a near branch is its own kind and the linker
;; checks it here.

; REQUIRES: c166
; RUN: rm -rf %t && split-file %s %t
; RUN: llvm-mc -filetype=obj -triple=c166 %t/a.s -o %t/a.o

;; Same segment: fine, and the call is to the offset within it.
; RUN: ld.lld -T %t/same.t %t/a.o -o %t/same.exe
; RUN: llvm-objdump -d %t/same.exe | FileCheck %s --check-prefix=SAME
; SAME: calla cc_UC, 256

;; Different segments: refused, and the message says both addresses and what to
;; do instead.
; RUN: not ld.lld -T %t/split.t %t/a.o -o %t/split.exe 2>&1 | FileCheck %s --check-prefix=ERR
; ERR: branch or call to callee leaves the segment it is in: the target is at c00100 and the instruction at e00002
; ERR-SAME: has to be a far one

;; A far call across the same two segments is what the near one should have
;; been, and is not complained about.
; RUN: llvm-mc -filetype=obj -triple=c166 %t/far.s -o %t/far.o
; RUN: ld.lld -T %t/split.t %t/far.o -o %t/far.exe
; RUN: llvm-objdump -d %t/far.exe | FileCheck %s --check-prefix=FAR
; FAR: calls #192, 256

;--- a.s
        .section .caller,"ax",@progbits
        .globl _start
_start:
        calla   cc_UC, callee

        .text
        .globl callee
callee:
        ret

;--- far.s
        .section .caller,"ax",@progbits
        .globl _start
_start:
        calls   #seg(callee), sof(callee)

        .text
        .globl callee
callee:
        ret

;--- same.t
SECTIONS {
  . = 0xc00100;
  .text : { *(.text) }
  .caller : { *(.caller) }
}

;--- split.t
SECTIONS {
  . = 0xc00100;
  .text : { *(.text) }
  . = 0xe00000;
  .caller : { *(.caller) }
}
