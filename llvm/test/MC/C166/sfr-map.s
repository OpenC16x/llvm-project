; RUN: llvm-mc -triple=c166 -mcpu=xc16x -show-encoding < %s \
; RUN:   | FileCheck %s --check-prefix=XC164
; RUN: not llvm-mc -triple=c166 -mcpu=c167 < %s 2>&1 \
; RUN:   | FileCheck %s --check-prefix=NOTC167

;; The special function registers are a property of the part rather than of the
;; family: the same short address names different registers on different
;; derivatives.  So the register file holds every map at once and the subtarget
;; picks, which is what stops a program for one part quietly assembling for
;; another.  Below is the XC164CM's map; sfr-map-c167.s is the C167's, on the
;; same short addresses, and the two disassemble the same bytes differently.

;; The extended special function registers are mapped from F000H and F100H
;; rather than FE00H and FF00H.  Reaching one through a "reg" field would need
;; an EXTR and would encode the same as the register with the same short address
;; in the ordinary space, so they are reachable by address only - which needs no
;; EXTR, because the default DPPs already cover F000H.  They are the XC164CM's
;; entire, so they go with the rest of its map.
; XC164: mov syscon1, r2  ; encoding: [0xf6,0xf2,0xdc,0xf1]
; NOTC167: [[@LINE+1]]:{{[0-9]+}}: error: 'syscon1' is not a register on the selected processor
        mov     syscon1, r2
; XC164: mov r3, pllcon   ; encoding: [0xf2,0xf3,0xd0,0xf1]
; NOTC167: [[@LINE+1]]:{{[0-9]+}}: error: 'pllcon' is not a register on the selected processor
        mov     r3, pllcon
; XC164: mov odp3, r4     ; encoding: [0xf6,0xf4,0xc6,0xf1]
; NOTC167: [[@LINE+1]]:{{[0-9]+}}: error: 'odp3' is not a register on the selected processor
        mov     odp3, r4
; XC164: mov r5, rtc_con  ; encoding: [0xf2,0xf5,0x10,0xf1]
; NOTC167: [[@LINE+1]]:{{[0-9]+}}: error: 'rtc_con' is not a register on the selected processor
        mov     r5, rtc_con

;; The short addresses an external bus would use name other things on this
;; part, so these are the XC164CM's registers rather than the C167's
;; BUSCON/ADDRSEL.  These seven are the whole of the disagreement between the
;; two maps: every one of the 91 names they share is at the same short address
;; in both.
; XC164: mov vecseg, #192 ; encoding: [0xe6,0x89,0xc0,0x00]
; NOTC167: [[@LINE+1]]:{{[0-9]+}}: error: 'vecseg' is not a register on the selected processor
        mov     vecseg, #0xC0
; XC164: mov spseg, #3    ; encoding: [0xe6,0x86,0x03,0x00]
; NOTC167: [[@LINE+1]]:{{[0-9]+}}: error: 'spseg' is not a register on the selected processor
        mov     spseg, #3
; XC164: mov cpucon1, r2  ; encoding: [0xf6,0xf2,0x18,0xfe]
; NOTC167: [[@LINE+1]]:{{[0-9]+}}: error: 'cpucon1' is not a register on the selected processor
        mov     cpucon1, r2
; XC164: mov cpucon2, r2  ; encoding: [0xf6,0xf2,0x1a,0xfe]
; NOTC167: [[@LINE+1]]:{{[0-9]+}}: error: 'cpucon2' is not a register on the selected processor
        mov     cpucon2, r2
; XC164: mov p9, #0       ; encoding: [0xe6,0x8b,0x00,0x00]
; NOTC167: [[@LINE+1]]:{{[0-9]+}}: error: 'p9' is not a register on the selected processor
        mov     p9, #0
; XC164: mov dp9, #0      ; encoding: [0xe6,0x8c,0x00,0x00]
; NOTC167: [[@LINE+1]]:{{[0-9]+}}: error: 'dp9' is not a register on the selected processor
        mov     dp9, #0
; XC164: bset odp9.3      ; encoding: [0x3f,0x8d]
; NOTC167: [[@LINE+1]]:{{[0-9]+}}: error: 'odp9' is not a register on the selected processor
        bset    odp9.3

;; A register both maps have is not gated at all, and keeps the same encoding.
; XC164: mov r2, t2con    ; encoding: [0xf2,0xf2,0x40,0xff]
; NOTC167-NOT: 't2con' is not a register
        mov     r2, t2con
