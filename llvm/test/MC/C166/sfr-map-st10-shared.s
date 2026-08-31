; RUN: llvm-mc -triple=c166 -mcpu=st10 -show-encoding < %s | FileCheck %s
; RUN: llvm-mc -triple=c166 -mcpu=c167 -show-encoding < %s | FileCheck %s
; RUN: not llvm-mc -triple=c166 -mcpu=xc16x < %s 2>&1 \
; RUN:   | FileCheck %s --check-prefix=NOTXC164
; RUN: not llvm-mc -triple=c166 -mcpu=c166 < %s 2>&1 \
; RUN:   | FileCheck %s --check-prefix=NOTXC164

;; The cross-check, which is the whole reason the ST10F269 datasheet was read
;; rather than assumed.  Its Table 31 gives 163 registers a short address, and
;; every one of those 163 names is a name this tree already had at the same
;; short address: 97 in SFRCommon and the 66 below, which are the C167CS's.
;; Zero disagreements out of 163 is what says the two maps were both read
;; correctly - a single mismatch would have meant one of them was not.
;;
;; So this file holds one set of expectations against two runs.  -mcpu=st10
;; and -mcpu=c167 have to produce identical bytes for all of it; the day they
;; do not, one of the two maps has drifted from the document it came from.
;;
;; The two names that are the C167CS's alone - P1DIDIS at 52H and FOCON at
;; D5H, neither of which appears anywhere in Table 31 - are in
;; sfr-map-st10-absent.s rather than here, because they are the one thing the
;; two runs must disagree about.
;;
;; The last two runs are there so that agreeing is not the same as accepting
;; everything: neither the XC164CM's map nor the first generation has these.
; CHECK: mov r2, addrsel1 ; encoding: [0xf2,0xf2,0x18,0xfe]
; NOTXC164: [[@LINE+1]]:{{[0-9]+}}: error: 'addrsel1' is not a register on the selected processor
        mov     r2, addrsel1
; CHECK: mov r2, addrsel2 ; encoding: [0xf2,0xf2,0x1a,0xfe]
        mov     r2, addrsel2
; CHECK: mov r2, addrsel3 ; encoding: [0xf2,0xf2,0x1c,0xfe]
        mov     r2, addrsel3
; CHECK: mov r2, addrsel4 ; encoding: [0xf2,0xf2,0x1e,0xfe]
        mov     r2, addrsel4
; CHECK: mov r2, pw0      ; encoding: [0xf2,0xf2,0x30,0xfe]
        mov     r2, pw0
; CHECK: mov r2, pw1      ; encoding: [0xf2,0xf2,0x32,0xfe]
        mov     r2, pw1
; CHECK: mov r2, pw2      ; encoding: [0xf2,0xf2,0x34,0xfe]
        mov     r2, pw2
; CHECK: mov r2, pw3      ; encoding: [0xf2,0xf2,0x36,0xfe]
        mov     r2, pw3
; CHECK: mov r2, t0       ; encoding: [0xf2,0xf2,0x50,0xfe]
        mov     r2, t0
; CHECK: mov r2, t1       ; encoding: [0xf2,0xf2,0x52,0xfe]
        mov     r2, t1
; CHECK: mov r2, t0rel    ; encoding: [0xf2,0xf2,0x54,0xfe]
        mov     r2, t0rel
; CHECK: mov r2, t1rel    ; encoding: [0xf2,0xf2,0x56,0xfe]
        mov     r2, t1rel
; CHECK: mov r2, cc0      ; encoding: [0xf2,0xf2,0x80,0xfe]
        mov     r2, cc0
; CHECK: mov r2, cc1      ; encoding: [0xf2,0xf2,0x82,0xfe]
        mov     r2, cc1
; CHECK: mov r2, cc2      ; encoding: [0xf2,0xf2,0x84,0xfe]
        mov     r2, cc2
; CHECK: mov r2, cc3      ; encoding: [0xf2,0xf2,0x86,0xfe]
        mov     r2, cc3
; CHECK: mov r2, cc4      ; encoding: [0xf2,0xf2,0x88,0xfe]
        mov     r2, cc4
; CHECK: mov r2, cc5      ; encoding: [0xf2,0xf2,0x8a,0xfe]
        mov     r2, cc5
; CHECK: mov r2, cc6      ; encoding: [0xf2,0xf2,0x8c,0xfe]
        mov     r2, cc6
; CHECK: mov r2, cc7      ; encoding: [0xf2,0xf2,0x8e,0xfe]
        mov     r2, cc7
; CHECK: mov r2, cc8      ; encoding: [0xf2,0xf2,0x90,0xfe]
        mov     r2, cc8
; CHECK: mov r2, cc9      ; encoding: [0xf2,0xf2,0x92,0xfe]
        mov     r2, cc9
; CHECK: mov r2, cc10     ; encoding: [0xf2,0xf2,0x94,0xfe]
        mov     r2, cc10
; CHECK: mov r2, cc11     ; encoding: [0xf2,0xf2,0x96,0xfe]
        mov     r2, cc11
; CHECK: mov r2, cc12     ; encoding: [0xf2,0xf2,0x98,0xfe]
        mov     r2, cc12
; CHECK: mov r2, cc13     ; encoding: [0xf2,0xf2,0x9a,0xfe]
        mov     r2, cc13
; CHECK: mov r2, cc14     ; encoding: [0xf2,0xf2,0x9c,0xfe]
        mov     r2, cc14
; CHECK: mov r2, cc15     ; encoding: [0xf2,0xf2,0x9e,0xfe]
        mov     r2, cc15
; CHECK: mov r2, wdt      ; encoding: [0xf2,0xf2,0xae,0xfe]
        mov     r2, wdt
; CHECK: mov r2, p0l      ; encoding: [0xf2,0xf2,0x00,0xff]
        mov     r2, p0l
; CHECK: mov r2, p0h      ; encoding: [0xf2,0xf2,0x02,0xff]
        mov     r2, p0h
; CHECK: mov r2, buscon0  ; encoding: [0xf2,0xf2,0x0c,0xff]
        mov     r2, buscon0
; CHECK: mov r2, syscon   ; encoding: [0xf2,0xf2,0x12,0xff]
        mov     r2, syscon
; CHECK: mov r2, buscon1  ; encoding: [0xf2,0xf2,0x14,0xff]
        mov     r2, buscon1
; CHECK: mov r2, buscon2  ; encoding: [0xf2,0xf2,0x16,0xff]
        mov     r2, buscon2
; CHECK: mov r2, buscon3  ; encoding: [0xf2,0xf2,0x18,0xff]
        mov     r2, buscon3
; CHECK: mov r2, buscon4  ; encoding: [0xf2,0xf2,0x1a,0xff]
        mov     r2, buscon4
; CHECK: mov r2, pwmcon0  ; encoding: [0xf2,0xf2,0x30,0xff]
        mov     r2, pwmcon0
; CHECK: mov r2, pwmcon1  ; encoding: [0xf2,0xf2,0x32,0xff]
        mov     r2, pwmcon1
; CHECK: mov r2, t01con   ; encoding: [0xf2,0xf2,0x50,0xff]
        mov     r2, t01con
; CHECK: mov r2, ccm0     ; encoding: [0xf2,0xf2,0x52,0xff]
        mov     r2, ccm0
; CHECK: mov r2, ccm1     ; encoding: [0xf2,0xf2,0x54,0xff]
        mov     r2, ccm1
; CHECK: mov r2, ccm2     ; encoding: [0xf2,0xf2,0x56,0xff]
        mov     r2, ccm2
; CHECK: mov r2, ccm3     ; encoding: [0xf2,0xf2,0x58,0xff]
        mov     r2, ccm3
; CHECK: mov r2, cc0ic    ; encoding: [0xf2,0xf2,0x78,0xff]
        mov     r2, cc0ic
; CHECK: mov r2, cc1ic    ; encoding: [0xf2,0xf2,0x7a,0xff]
        mov     r2, cc1ic
; CHECK: mov r2, cc2ic    ; encoding: [0xf2,0xf2,0x7c,0xff]
        mov     r2, cc2ic
; CHECK: mov r2, cc3ic    ; encoding: [0xf2,0xf2,0x7e,0xff]
        mov     r2, cc3ic
; CHECK: mov r2, cc4ic    ; encoding: [0xf2,0xf2,0x80,0xff]
        mov     r2, cc4ic
; CHECK: mov r2, cc5ic    ; encoding: [0xf2,0xf2,0x82,0xff]
        mov     r2, cc5ic
; CHECK: mov r2, cc6ic    ; encoding: [0xf2,0xf2,0x84,0xff]
        mov     r2, cc6ic
; CHECK: mov r2, cc7ic    ; encoding: [0xf2,0xf2,0x86,0xff]
        mov     r2, cc7ic
; CHECK: mov r2, cc14ic   ; encoding: [0xf2,0xf2,0x94,0xff]
        mov     r2, cc14ic
; CHECK: mov r2, cc15ic   ; encoding: [0xf2,0xf2,0x96,0xff]
        mov     r2, cc15ic
; CHECK: mov r2, t0ic     ; encoding: [0xf2,0xf2,0x9c,0xff]
        mov     r2, t0ic
; CHECK: mov r2, t1ic     ; encoding: [0xf2,0xf2,0x9e,0xff]
        mov     r2, t1ic
; CHECK: mov r2, p2       ; encoding: [0xf2,0xf2,0xc0,0xff]
        mov     r2, p2
; CHECK: mov r2, dp2      ; encoding: [0xf2,0xf2,0xc2,0xff]
        mov     r2, dp2
; CHECK: mov r2, p4       ; encoding: [0xf2,0xf2,0xc8,0xff]
        mov     r2, p4
; CHECK: mov r2, dp4      ; encoding: [0xf2,0xf2,0xca,0xff]
        mov     r2, dp4
; CHECK: mov r2, p6       ; encoding: [0xf2,0xf2,0xcc,0xff]
        mov     r2, p6
; CHECK: mov r2, dp6      ; encoding: [0xf2,0xf2,0xce,0xff]
        mov     r2, dp6
; CHECK: mov r2, p7       ; encoding: [0xf2,0xf2,0xd0,0xff]
        mov     r2, p7
; CHECK: mov r2, dp7      ; encoding: [0xf2,0xf2,0xd2,0xff]
        mov     r2, dp7
; CHECK: mov r2, p8       ; encoding: [0xf2,0xf2,0xd4,0xff]
        mov     r2, p8
; CHECK: mov r2, dp8      ; encoding: [0xf2,0xf2,0xd6,0xff]
        mov     r2, dp8
