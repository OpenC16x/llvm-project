; RUN: llvm-mc -triple=c166 -mcpu=c167 -show-encoding < %s | FileCheck %s
; RUN: not llvm-mc -triple=c166 -mcpu=st10 < %s 2>&1 \
; RUN:   | FileCheck %s --check-prefix=NOTST10

;; The two registers the C167CS has and the ST10F269 has not.  Table 31 of the
;; ST10F269 datasheet lists 237 registers and neither of these is among them,
;; and neither short address is used for anything else on that part - so this
;; is a hole in the map rather than a collision, which is the harder case to
;; notice and the easier one to get wrong by sharing a class.
;;
;; FOCON is the more interesting of the two.  It used to sit in SFRCommon,
;; which is documented as what every map in the register file agrees about;
;; that was true while there were two maps and the third made it false.  It is
;; now in the two maps that have the register, which is what keeps SFRCommon's
;; claim about itself true.

; CHECK: mov r2, p1didis  ; encoding: [0xf2,0xf2,0xa4,0xfe]
; NOTST10: [[@LINE+1]]:{{[0-9]+}}: error: 'p1didis' is not a register on the selected processor
        mov     r2, p1didis
; CHECK: mov r2, focon    ; encoding: [0xf2,0xf2,0xaa,0xff]
; NOTST10: [[@LINE+1]]:{{[0-9]+}}: error: 'focon' is not a register on the selected processor
        mov     r2, focon
