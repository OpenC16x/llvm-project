; RUN: llvm-mc -triple=c166 -mcpu=c167 -show-encoding < %s \
; RUN:   | FileCheck %s --check-prefix=C167
; RUN: not llvm-mc -triple=c166 -mcpu=xc16x < %s 2>&1 \
; RUN:   | FileCheck %s --check-prefix=NOTXC164
; RUN: llvm-mc -triple=c166 -mcpu=c167 -filetype=obj -o %t.o < %s
; RUN: llvm-objdump -d --triple=c166 --mcpu=c167 %t.o \
; RUN:   | FileCheck %s --check-prefix=DIS167
; RUN: llvm-objdump -d --triple=c166 --mcpu=xc16x %t.o \
; RUN:   | FileCheck %s --check-prefix=DISXC164

;; The C167's side of the same seven short addresses.  Addresses are the Keil
;; C167CS device header's, which cites the Siemens C167CS User Manual V1.0 of
;; 1999-05; sfr-map.s is what the XC164CM has there instead.

; C167: mov r2, addrsel1 ; encoding: [0xf2,0xf2,0x18,0xfe]
; NOTXC164: [[@LINE+1]]:{{[0-9]+}}: error: 'addrsel1' is not a register on the selected processor
        mov     r2, addrsel1
; C167: mov r2, addrsel2 ; encoding: [0xf2,0xf2,0x1a,0xfe]
; NOTXC164: [[@LINE+1]]:{{[0-9]+}}: error: 'addrsel2' is not a register on the selected processor
        mov     r2, addrsel2
; C167: mov r2, buscon0  ; encoding: [0xf2,0xf2,0x0c,0xff]
; NOTXC164: [[@LINE+1]]:{{[0-9]+}}: error: 'buscon0' is not a register on the selected processor
        mov     r2, buscon0
; C167: mov r2, syscon   ; encoding: [0xf2,0xf2,0x12,0xff]
; NOTXC164: [[@LINE+1]]:{{[0-9]+}}: error: 'syscon' is not a register on the selected processor
        mov     r2, syscon
; C167: mov r2, buscon2  ; encoding: [0xf2,0xf2,0x16,0xff]
; NOTXC164: [[@LINE+1]]:{{[0-9]+}}: error: 'buscon2' is not a register on the selected processor
        mov     r2, buscon2
; C167: mov r2, buscon3  ; encoding: [0xf2,0xf2,0x18,0xff]
; NOTXC164: [[@LINE+1]]:{{[0-9]+}}: error: 'buscon3' is not a register on the selected processor
        mov     r2, buscon3
; C167: mov r2, buscon4  ; encoding: [0xf2,0xf2,0x1a,0xff]
; NOTXC164: [[@LINE+1]]:{{[0-9]+}}: error: 'buscon4' is not a register on the selected processor
        mov     r2, buscon4

;; The blocks the XC164CM does not have at all: CAPCOM1, the PWM unit, and the
;; ports beyond 1, 3, 5 and 9.  These are absent rather than moved, so naming
;; one for the XC164CM is the same error.
; C167: mov r2, cc0      ; encoding: [0xf2,0xf2,0x80,0xfe]
; NOTXC164: [[@LINE+1]]:{{[0-9]+}}: error: 'cc0' is not a register on the selected processor
        mov     r2, cc0
; C167: mov r2, t01con   ; encoding: [0xf2,0xf2,0x50,0xff]
; NOTXC164: [[@LINE+1]]:{{[0-9]+}}: error: 't01con' is not a register on the selected processor
        mov     r2, t01con
; C167: mov r2, pwmcon0  ; encoding: [0xf2,0xf2,0x30,0xff]
; NOTXC164: [[@LINE+1]]:{{[0-9]+}}: error: 'pwmcon0' is not a register on the selected processor
        mov     r2, pwmcon0
; C167: mov r2, p2       ; encoding: [0xf2,0xf2,0xc0,0xff]
; NOTXC164: [[@LINE+1]]:{{[0-9]+}}: error: 'p2' is not a register on the selected processor
        mov     r2, p2

;; The same bytes read back for each part.  This is the whole point: an object
;; disassembled as the wrong derivative names the wrong registers, so which one
;; it was for has to be said rather than assumed.
; DIS167: mov r2, addrsel1
; DIS167: mov r2, addrsel2
; DIS167: mov r2, buscon0
; DIS167: mov r2, syscon
;
; DISXC164: mov r2, cpucon1
; DISXC164: mov r2, cpucon2
; DISXC164: mov r2, spseg
; DISXC164: mov r2, vecseg
