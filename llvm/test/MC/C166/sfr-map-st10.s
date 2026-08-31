; RUN: llvm-mc -triple=c166 -mcpu=st10 -show-encoding < %s \
; RUN:   | FileCheck %s --check-prefix=ST10
; RUN: not llvm-mc -triple=c166 -mcpu=xc16x < %s 2>&1 \
; RUN:   | FileCheck %s --check-prefix=NOTXC164
; RUN: llvm-mc -triple=c166 -mcpu=st10 -filetype=obj -o %t.o < %s
; RUN: llvm-objdump -d --triple=c166 --mcpu=st10 %t.o \
; RUN:   | FileCheck %s --check-prefix=DISST10
; RUN: llvm-objdump -d --triple=c166 --mcpu=xc16x %t.o \
; RUN:   | FileCheck %s --check-prefix=DISXC164

;; The ST10F269's extended special function registers, from Table 31 of its
;; datasheet.  What that table settled about the short-address map is in
;; sfr-map-st10-shared.s; this file is the extended space, where the two maps
;; genuinely disagree.

;; Three short addresses name a different register on each part.  In every
;; case the two names describe the same hardware, so what is wrong about
;; naming the other one is the listing rather than the bytes - which is
;; exactly why the assembler has to refuse it and the disassembler has to
;; print the selected part's name for the address it finds.
; ST10: mov r2, addat2   ; encoding: [0xf2,0xf2,0xa0,0xf0]
; NOTXC164: [[@LINE+1]]:{{[0-9]+}}: error: 'addat2' is not a register on the selected processor
        mov     r2, addat2
; ST10: mov r2, exisel   ; encoding: [0xf2,0xf2,0xda,0xf1]
; NOTXC164: [[@LINE+1]]:{{[0-9]+}}: error: 'exisel' is not a register on the selected processor
        mov     r2, exisel
; ST10: mov r2, xp3ic    ; encoding: [0xf2,0xf2,0x9e,0xf1]
; NOTXC164: [[@LINE+1]]:{{[0-9]+}}: error: 'xp3ic' is not a register on the selected processor
        mov     r2, xp3ic

;; The blocks this part has and the XC164CM does not: the PWM module, CAPCOM
;; timers 7 and 8, the port output control registers, and the interrupt
;; control registers for CAPCOM 16 to 31.
; ST10: mov r2, pt0      ; encoding: [0xf2,0xf2,0x30,0xf0]
; NOTXC164: [[@LINE+1]]:{{[0-9]+}}: error: 'pt0' is not a register on the selected processor
        mov     r2, pt0
; ST10: mov r2, pp3      ; encoding: [0xf2,0xf2,0x3e,0xf0]
; NOTXC164: [[@LINE+1]]:{{[0-9]+}}: error: 'pp3' is not a register on the selected processor
        mov     r2, pp3
; ST10: mov r2, pocon0l  ; encoding: [0xf2,0xf2,0x80,0xf0]
; NOTXC164: [[@LINE+1]]:{{[0-9]+}}: error: 'pocon0l' is not a register on the selected processor
        mov     r2, pocon0l
; ST10: mov r2, cc16ic   ; encoding: [0xf2,0xf2,0x60,0xf1]
; NOTXC164: [[@LINE+1]]:{{[0-9]+}}: error: 'cc16ic' is not a register on the selected processor
        mov     r2, cc16ic
; ST10: mov r2, cc31ic   ; encoding: [0xf2,0xf2,0x94,0xf1]
; NOTXC164: [[@LINE+1]]:{{[0-9]+}}: error: 'cc31ic' is not a register on the selected processor
        mov     r2, cc31ic
; ST10: mov r2, dp0l     ; encoding: [0xf2,0xf2,0x00,0xf1]
; NOTXC164: [[@LINE+1]]:{{[0-9]+}}: error: 'dp0l' is not a register on the selected processor
        mov     r2, dp0l
; ST10: mov r2, sscbr    ; encoding: [0xf2,0xf2,0xb4,0xf0]
; NOTXC164: [[@LINE+1]]:{{[0-9]+}}: error: 'sscbr' is not a register on the selected processor
        mov     r2, sscbr

;; T7IC and T8IC are the reason this file exists at all.  Both manuals print
;; them at F17AH/BEH and F17CH/BFH, and those two halves disagree: BEH is
;; F17CH and BFH is F17EH.  The XC164CM's manual gives nothing to break the
;; tie, so the tree left its CC2_T7IC and CC2_T8IC out.  This datasheet does
;; break it - PWMIC's own row is F17EH/BFH and is self-consistent, so BFH is
;; taken and T8IC cannot have it; BDH is claimed by nothing; and F176H to
;; F17EH is an unbroken run for BBH to BFH.  The physical column is right.
;;
;; So the encodings below are the check: 7a f1 and 7c f1 rather than the 7c f1
;; and 7e f1 the printed short addresses would have produced.  Getting this
;; backwards would put T8IC on top of PWMIC.
; ST10: mov r2, cc28ic   ; encoding: [0xf2,0xf2,0x78,0xf1]
        mov     r2, cc28ic
; ST10: mov r2, t7ic     ; encoding: [0xf2,0xf2,0x7a,0xf1]
; NOTXC164: [[@LINE+1]]:{{[0-9]+}}: error: 't7ic' is not a register on the selected processor
        mov     r2, t7ic
; ST10: mov r2, t8ic     ; encoding: [0xf2,0xf2,0x7c,0xf1]
; NOTXC164: [[@LINE+1]]:{{[0-9]+}}: error: 't8ic' is not a register on the selected processor
        mov     r2, t8ic
; ST10: mov r2, pwmic    ; encoding: [0xf2,0xf2,0x7e,0xf1]
; NOTXC164: [[@LINE+1]]:{{[0-9]+}}: error: 'pwmic' is not a register on the selected processor
        mov     r2, pwmic

;; Twelve extended names are on both parts at the same short address, so both
;; maps list them and neither owns them.  These two are from that set and are
;; accepted here and under -mcpu=xc16x alike, which is what keeps the run
;; above from passing for the trivial reason that an ST10 rejects everything
;; extended.
; ST10: mov r2, exicon   ; encoding: [0xf2,0xf2,0xc0,0xf1]
        mov     r2, exicon
; ST10: mov r2, idchip   ; encoding: [0xf2,0xf2,0x7c,0xf0]
        mov     r2, idchip

;; The same bytes, read back for each part.  This is the half that a wrong
;; answer would leave silent: the object is correct either way, and only the
;; listing says which part it was built for.
; DISST10:  mov r2, addat2
; DISXC164: mov r2, adc_dat2
; DISST10:  mov r2, exisel
; DISXC164: mov r2, exisel0
; DISST10:  mov r2, xp3ic
; DISXC164: mov r2, pll_ic
