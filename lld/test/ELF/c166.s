; REQUIRES: c166
; RUN: llvm-mc -filetype=obj -triple=c166 -o %t.o %s
; RUN: ld.lld -o %t.exe --image-base=0x1000 -Ttext=0x8000 -Tdata=0x2000 \
; RUN:     --defsym=far=0x123456 --defsym=near=0x4242 --defsym=byte=0x21 \
; RUN:     --defsym=target=0x8010 -z separate-code %t.o
; RUN: llvm-objdump -d %t.exe | FileCheck %s --check-prefix=TEXT
; RUN: llvm-objdump -s -j .data %t.exe | FileCheck %s --check-prefix=DATA

;; Check every C166 relocation type the backend emits.

  .text
  .globl _start
_start:
  nop

;; R_C166_PCREL8W: words from the instruction after the branch.
;; (0x8010 - 0x8006) / 2 == 5
  jb r5.3, target

;; R_C166_PCREL8W2: the same, in a two byte instruction, so it is one byte
;; short of the end rather than two.  (0x8010 - 0x8008) / 2 == 4
  jmpr cc_UC, target

;; R_C166_ABS16
  mov r2, near

;; R_C166_SEG8 and R_C166_SOF16: 0x123456 splits into segment 0x12 and
;; offset 0x3456.
  exts #seg(far), #1
  mov r3, #sof(far)

;; R_C166_PAG10 and R_C166_POF14: the same address splits into page
;; 0x123456 >> 14 == 0x48 and offset 0x123456 & 0x3fff == 0x3456.  Only the
;; low ten bits of the word belong to the page, so the rest of the
;; instruction has to survive.
  extp #pag(far), #1
  mov r4, #pof(far)

; TEXT-LABEL: <_start>:
; TEXT-NEXT: 8000: cc 00        nop
; TEXT-NEXT: 8002: 8a f5 05 30  jb   r5.3, 0x8010
; TEXT-NEXT: 8006: 0d 04        jmpr cc_UC, 0x8010
; TEXT-NEXT: 8008: f2 f2 42 42  mov  r2, 16962
; TEXT-NEXT: 800c: d7 00 12 00  exts #18, #1
; TEXT-NEXT: 8010: e6 f3 56 34  mov  r3, #13398
; TEXT-NEXT: 8014: d7 40 48 00  extp #72, #1
; TEXT-NEXT: 8018: e6 f4 56 34  mov  r4, #13398

  .data
;; R_C166_ABS8
  .byte byte
;; R_C166_ABS16
  .short _start
;; R_C166_ABS32
  .long far

; DATA:      Contents of section .data:
; DATA-NEXT: 2000 21008056 341200
