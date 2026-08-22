; RUN: llvm-mc -triple=c166 -show-encoding < %s | FileCheck %s

;; The MAC unit.  Every instruction is four bytes: the first selects the
;; operand form, the second holds the register numbers or the pointer control,
;; the third is the operation, and the fourth the repeat control and what
;; happens to the general purpose register pointer.

;; Two registers is A3, a register and a pointer is 83, two pointers is 93,
;; and the operation byte is the same across all three.
; CHECK: coabs r2, r3     ; encoding: [0xa3,0x23,0xca,0x00]
        coabs   r2, r3
; CHECK: coabs r2, [r3]   ; encoding: [0x83,0x23,0xca,0x01]
        coabs   r2, [r3]
; CHECK: coabs [idx0], [r3] ; encoding: [0x93,0x13,0xca,0x01]
        coabs   [idx0], [r3]

;; What happens to a pointer after it is read is the low three bits of the last
;; byte for a general purpose register, and the top nibble of the second byte
;; for one of the MAC unit's own - which also says which of the two it is.
; CHECK: comac r2, [r3+]  ; encoding: [0x83,0x23,0xd0,0x02]
        comac   r2, [r3+]
; CHECK: comac r2, [r3-]  ; encoding: [0x83,0x23,0xd0,0x03]
        comac   r2, [r3-]
; CHECK: comac r2, [r3+qr0] ; encoding: [0x83,0x23,0xd0,0x04]
        comac   r2, [r3+qr0]
; CHECK: comac r2, [r3-qr1] ; encoding: [0x83,0x23,0xd0,0x07]
        comac   r2, [r3-qr1]
; CHECK: comacm [idx1-], [r7-qr1] ; encoding: [0x93,0xb7,0xd8,0x07]
        comacm  [idx1-], [r7-qr1]
; CHECK: comov [idx0+qx1], [r1+] ; encoding: [0xd3,0x61,0x00,0x02]
        comov   [idx0+qx1], [r1+]

;; A rounding form is a word after the operands rather than part of the
;; mnemonic, and picks a different operation byte.
; CHECK: comul r2, r3     ; encoding: [0xa3,0x23,0xc0,0x00]
        comul   r2, r3
; CHECK: comul r2, r3, rnd ; encoding: [0xa3,0x23,0xc1,0x00]
        comul   r2, r3, rnd

;; A mnemonic can end in a minus, which is part of the name and not an operand.
; CHECK: comul- r2, r3    ; encoding: [0xa3,0x23,0xc8,0x00]
        comul-  r2, r3
; CHECK: coload- r2, [r3+] ; encoding: [0x83,0x23,0x2a,0x02]
        coload- r2, [r3+]

;; The shifts take a count in a register or as five bits in the last byte.
; CHECK: coshl r4         ; encoding: [0xa3,0x44,0x8a,0x00]
        coshl   r4
; CHECK: coshl #17        ; encoding: [0xa3,0x00,0x82,0x11]
        coshl   #17
; CHECK: coashr #7, rnd   ; encoding: [0xa3,0x00,0xb2,0x07]
        coashr  #7, rnd

;; Some have no operands at all.
; CHECK: coabs            ; encoding: [0xa3,0x00,0x1a,0x00]
        coabs
; CHECK: coneg rnd        ; encoding: [0xa3,0x00,0x72,0x00]
        coneg   rnd

;; CoSTORE names its register by a five bit code of its own rather than by an
;; address, which sits in the third byte.
; CHECK: costore [r2+], mah ; encoding: [0xb3,0x22,0x08,0x02]
        costore [r2+], mah
; CHECK: costore r3, mcw  ; encoding: [0xc3,0x33,0x28,0x00]
        costore r3, mcw
; CHECK: costore r3, mas  ; encoding: [0xc3,0x33,0x10,0x00]
        costore r3, mas

;; The MAC unit's registers are ordinary special function registers as well,
;; except MAS, which only CoSTORE can reach.
; CHECK: mov mcw, #1024   ; encoding: [0xe6,0xee,0x00,0x04]
        mov     mcw, #0x400
; CHECK: mov r2, idx0     ; encoding: [0xf2,0xf2,0x08,0xff]
        mov     r2, idx0
