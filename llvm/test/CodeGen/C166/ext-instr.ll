; RUN: llc -mtriple=c166 -mcpu=c167 -O2 < %s | FileCheck %s --check-prefix=EXT
; RUN: llc -mtriple=c166 -O2 < %s | FileCheck %s --check-prefix=EXT
; RUN: llc -mtriple=c166 -mcpu=c166 -O2 < %s | FileCheck %s --check-prefix=NOEXT

; ATOMIC and the EXTend instructions arrived with the second generation of the
; family; the SAB 80C166 and 83C166 do not decode them.  -mcpu=c166 is that
; first generation by name, and the default is the generation after it, which
; is what every part -mmcu= knows has.

; A read-modify-write reaches indivisibility through ATOMIC where there is one,
; and by clearing PSW.IEN around a compare and exchange loop where there is
; not.  The second is longer per turn and needs no instruction counter, which
; is why it is also what the minimum and maximum already use.

define i16 @rmw(ptr %p, i16 %v) {
; EXT-LABEL: rmw:
; EXT:         atomic #4
; EXT-NEXT:    mov r4, [r2]
; EXT-NEXT:    mov r5, r4
; EXT-NEXT:    add r5, r3
; EXT-NEXT:    mov [r2], r5
;
; NOEXT-LABEL: rmw:
; NOEXT:         mov r6, psw
; NOEXT-NEXT:    bclr psw.11
; NOEXT:         mov psw, r6
; NOEXT-NOT:     atomic #
  %r = atomicrmw add ptr %p, i16 %v seq_cst
  ret i16 %r
}

; An exchange needs no copy and no arithmetic, so it is the two instruction
; form of the sequence rather than the four instruction one.

define i16 @swap(ptr %p, i16 %v) {
; EXT-LABEL: swap:
; EXT:         atomic #2
;
; NOEXT-LABEL: swap:
; NOEXT:         bclr psw.11
; NOEXT-NOT:     atomic #
  %r = atomicrmw xchg ptr %p, i16 %v seq_cst
  ret i16 %r
}
