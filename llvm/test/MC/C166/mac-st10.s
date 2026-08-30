; RUN: llvm-mc -triple=c166 -mcpu=st10 -mattr=+mac -show-encoding < %s \
; RUN:   | FileCheck %s
; RUN: llvm-mc -triple=c166 -mcpu=xc16x -show-encoding < %s | FileCheck %s
; RUN: not llvm-mc -triple=c166 -mcpu=st10 < %s 2>&1 \
; RUN:   | FileCheck %s --check-prefix=NOMAC

;; The ST10's DSP is this MAC, not a second one.  The ST10 Family Programming
;; Manual (PM0036) documents the same unit: all 53 coprocessor mnemonics carry
;; the same function code in both, and CoSTORE's six CoReg codes are the same
;; six.  So there is one "mac" feature rather than one per derivative, and the
;; bytes below are the same whichever part asks for it - which is what the two
;; runs above check by holding one set of expectations against both.
;;
;; Which parts have the unit is the part's business: PM0036 says to refer to
;; the device datasheets for which ST10 devices include the MAC.  So an ST10
;; asks for it rather than getting it from the -mcpu= name, and without the
;; feature there is no coprocessor at all.

; CHECK: comac r2, r3     ; encoding: [0xa3,0x23,0xd0,0x00]
; NOMAC: [[@LINE+1]]:{{[0-9]+}}: error: instruction requires a feature{{.*}}
        comac   r2, r3

;; The five pointer post-modifications PM0036 tabulates, on both pointer kinds.
; CHECK: comac [idx0+], [r9+]
        comac   [idx0+], [r9+]
; CHECK: comac [idx1-], [r9-]
        comac   [idx1-], [r9-]

;; CoSTORE names a MAC register by its own five bit code.
; CHECK: costore r4, mah  ; encoding: [0xc3,0x44,0x08,0x00]
        costore r4, mah
; CHECK: costore r4, mrw
        costore r4, mrw

;; The shifter's count, at the largest value the manual defines.
; CHECK: coshl #8         ; encoding: [0xa3,0x00,0x82,0x08]
        coshl   #8
