; RUN: not llvm-mc -triple=c166 -mcpu=xc16x %s 2>&1 | FileCheck %s

;; The MAC shifter's count field is five bits and the shifter does not use all
;; of it.  The ST10 Family Programming Manual (PM0036) says it twice - section
;; 2.4.8, "the shifter authorizes only 8-bit left/right shifts.  Shift values
;; must be between 0-8 (inclusive)", and again under CoSHL, which its revision
;; history records adding as a clarification.  A larger count encodes and does
;; nothing the manual defines, so it is refused rather than assembled.
;;
;; The check is after the match rather than in the operand class, and the
;; message says so by naming the count.  Three forms of every shift share a
;; mnemonic - #data5, Rwn and [Rwm] - and the matcher reports the diagnostic
;; of whichever candidate it tried last, so a class that rejected the count
;; got "expected a pointer such as [r2]" out of the pointer form.  Which of
;; the three the count was written for is not in doubt; the message should not
;; be either.
;;
;; The range belongs to the shifter and not to one derivative.  The MAC that
;; manual documents is the one this backend already describes: all 53
;; coprocessor mnemonics carry the same function codes in both, and CoSTORE's
;; six CoReg codes are the same six.

; CHECK: [[@LINE+1]]:{{[0-9]+}}: error: shift count must be in the range [0, 8]
        coshl   #9
; CHECK: [[@LINE+1]]:{{[0-9]+}}: error: shift count must be in the range [0, 8]
        coshr   #17
; CHECK: [[@LINE+1]]:{{[0-9]+}}: error: shift count must be in the range [0, 8]
        coashr  #31, rnd
; CHECK: [[@LINE+1]]:{{[0-9]+}}: error: shift count must be in the range [0, 8]
        coashr  #-1

;; Eight is fine, and so is a count that arrives in a register - what is in the
;; register is not known here, and the manual's restriction is on the value
;; rather than on the form.
; CHECK-NOT: error
        coshl   #8
        coshl   r4
        coashr  r5, rnd
