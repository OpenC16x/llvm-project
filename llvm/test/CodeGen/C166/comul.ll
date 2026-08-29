; RUN: llc -mtriple=c166 -mattr=+mac -O2 < %s | FileCheck %s --check-prefix=MAC
; RUN: llc -mtriple=c166 -O2 < %s | FileCheck %s --check-prefix=NOMAC

; MUL is ten states and leaves the answer in MDL and MDH, so a multiply is
; that plus a move out of one of them and a widening multiply is that plus two.
; CoMUL is two states and the answer comes out with CoSTOREs.  That is eight
; states for two bytes on each multiply, which is taken at -O2 and declined
; where the function asks to be small; optsize below is the same function with
; that attribute and nothing else changed.

define i32 @widening(i16 %a, i16 %b) {
; MAC-LABEL: widening:
; MAC:         comul r2, r3
; MAC-NEXT:    costore r2, mal
; MAC-NEXT:    costore r3, mah
; MAC-NOT:     {{^[[:space:]]+mul[[:space:]]}}
;
; NOMAC-LABEL: widening:
; NOMAC:         mul r2, r3
; NOMAC-NEXT:    mov r2, mdl
; NOMAC-NEXT:    mov r3, mdh
; NOMAC-NOT:     comul
  %sa = sext i16 %a to i32
  %sb = sext i16 %b to i32
  %m = mul i32 %sa, %sb
  ret i32 %m
}

define i32 @widening_unsigned(i16 %a, i16 %b) {
; MAC-LABEL: widening_unsigned:
; MAC:         comulu r2, r3
; MAC-NEXT:    costore r2, mal
; MAC-NEXT:    costore r3, mah
;
; NOMAC-LABEL: widening_unsigned:
; NOMAC:         mulu r2, r3
  %za = zext i16 %a to i32
  %zb = zext i16 %b to i32
  %m = mul i32 %za, %zb
  ret i32 %m
}

; The low word of a product is the same whether the operands were signed or
; not, so there is one instruction for it and it reads MAL.

define i16 @low_word(i16 %a, i16 %b) {
; MAC-LABEL: low_word:
; MAC:         comul r2, r3
; MAC-NEXT:    costore r2, mal
; MAC-NOT:     costore r{{[0-9]+}}, mah
;
; NOMAC-LABEL: low_word:
; NOMAC:         mul r2, r3
; NOMAC-NEXT:    mov r2, mdl
  %m = mul i16 %a, %b
  ret i16 %m
}

; Only the high word wanted reads MAH and leaves MAL alone.

define i16 @high_word_signed(i16 %a, i16 %b) {
; MAC-LABEL: high_word_signed:
; MAC:         comul r2, r3
; MAC-NEXT:    costore r2, mah
;
; NOMAC-LABEL: high_word_signed:
; NOMAC:         mul r2, r3
; NOMAC-NEXT:    mov r2, mdh
  %sa = sext i16 %a to i32
  %sb = sext i16 %b to i32
  %m = mul i32 %sa, %sb
  %s = ashr i32 %m, 16
  %r = trunc i32 %s to i16
  ret i16 %r
}

define i16 @high_word_unsigned(i16 %a, i16 %b) {
; MAC-LABEL: high_word_unsigned:
; MAC:         comulu r2, r3
; MAC-NEXT:    costore r2, mah
;
; NOMAC-LABEL: high_word_unsigned:
; NOMAC:         mulu r2, r3
; NOMAC-NEXT:    mov r2, mdh
  %za = zext i16 %a to i32
  %zb = zext i16 %b to i32
  %m = mul i32 %za, %zb
  %s = lshr i32 %m, 16
  %r = trunc i32 %s to i16
  ret i16 %r
}

; A 32 bit multiply is three of these, which is where it shows: forty two
; states become eighteen.  Nothing here calls __mulsi3; the multiply is
; expanded inline because both widening multiply nodes are legal.

define i32 @thirty_two_by_thirty_two(i32 %a, i32 %b) {
; MAC-LABEL: thirty_two_by_thirty_two:
; MAC-COUNT-3: {{^[[:space:]]+comul}}
; MAC-NOT:     {{^[[:space:]]+mul[[:space:]]}}
; MAC-NOT:     calla
;
; NOMAC-LABEL: thirty_two_by_thirty_two:
; NOMAC-NOT:     comul
  %m = mul i32 %a, %b
  ret i32 %m
}

; Two bytes per multiply is not free on a part with 48 KByte of near flash, so
; a function built for size keeps the MUL form.

define i32 @thirty_two_by_thirty_two_optsize(i32 %a, i32 %b) optsize {
; MAC-LABEL: thirty_two_by_thirty_two_optsize:
; MAC:         mul r2, r5
; MAC-NOT:     comul
  %m = mul i32 %a, %b
  ret i32 %m
}

; A handler that multiplies has to put back whatever unit it used, and which
; unit that is has changed.  Three words either way, but the right three: the
; coprocessor's accumulator rather than the multiply/divide registers.

define void @interrupt_handler(i16 %a, i16 %b, ptr %out) "interrupt" {
; MAC-LABEL: interrupt_handler:
; MAC:         push msw
; MAC-NEXT:    push mal
; MAC-NEXT:    push mah
; MAC-NOT:     push mdl
;
; NOMAC-LABEL: interrupt_handler:
; NOMAC:         push mdc
; NOMAC-NEXT:    push mdl
; NOMAC-NEXT:    push mdh
; NOMAC-NOT:     push mal
  %sa = sext i16 %a to i32
  %sb = sext i16 %b to i32
  %m = mul i32 %sa, %sb
  store i32 %m, ptr %out
  ret void
}
