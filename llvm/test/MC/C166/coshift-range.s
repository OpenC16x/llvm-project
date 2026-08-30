; RUN: not llvm-mc -triple=c166 -mcpu=xc16x %s 2>&1 | FileCheck %s

;; The MAC shifter's count field is five bits and the shifter does not use all
;; of it.  The ST10 Family Programming Manual (PM0036) says it twice - section
;; 2.4.8, "the shifter authorizes only 8-bit left/right shifts.  Shift values
;; must be between 0-8 (inclusive)", and again under CoSHL, which its revision
;; history records adding as a clarification.  A larger count encodes and does
;; nothing the manual defines, so it is refused rather than assembled.
;;
;; The range belongs to the shifter and not to one derivative.  The MAC that
;; manual documents is the one this backend already describes: all 53
;; coprocessor mnemonics carry the same function codes in both, and CoSTORE's
;; six CoReg codes are the same six.

; CHECK: [[@LINE+1]]:{{[0-9]+}}: error: expected a shift count in the range [0, 8]
        coshl   #9
; CHECK: [[@LINE+1]]:{{[0-9]+}}: error: expected a shift count in the range [0, 8]
        coshr   #17
; CHECK: [[@LINE+1]]:{{[0-9]+}}: error: expected a shift count in the range [0, 8]
        coashr  #31, rnd
; CHECK: [[@LINE+1]]:{{[0-9]+}}: error: expected a shift count in the range [0, 8]
        coashr  #-1

;; Eight is fine, and so is a count that arrives in a register - what is in the
;; register is not known here, and the manual's restriction is on the value
;; rather than on the form.
; CHECK-NOT: error
        coshl   #8
        coshl   r4
        coashr  r5, rnd
