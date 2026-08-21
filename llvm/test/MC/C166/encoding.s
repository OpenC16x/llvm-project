; RUN: llvm-mc -triple=c166 -show-encoding < %s | FileCheck %s

; Encodings are from the C166 Family Instruction Set Manual, V2.0 2001-03.
; A GPR in an 8 bit "reg" field is addressed as F0H + n.

; CHECK: add r2, r3       ; encoding: [0x00,0x23]
        add     r2, r3
; CHECK: addb rl2, rh3    ; encoding: [0x01,0x47]
        addb    rl2, rh3
; CHECK: add r2, #1234    ; encoding: [0x06,0xf2,0xd2,0x04]
        add     r2, #1234
; CHECK: sub r4, r5       ; encoding: [0x20,0x45]
        sub     r4, r5
; CHECK: and r6, r7       ; encoding: [0x60,0x67]
        and     r6, r7
; CHECK: or r8, r9        ; encoding: [0x70,0x89]
        or      r8, r9
; CHECK: xor r10, r11     ; encoding: [0x50,0xab]
        xor     r10, r11
; CHECK: addc r2, r3      ; encoding: [0x10,0x23]
        addc    r2, r3
; CHECK: subc r2, r3      ; encoding: [0x30,0x23]
        subc    r2, r3
; CHECK: cmp r2, r3       ; encoding: [0x40,0x23]
        cmp     r2, r3
; CHECK: cmpb rl2, rl3    ; encoding: [0x41,0x46]
        cmpb    rl2, rl3
; CHECK: cmp r2, #7       ; encoding: [0x46,0xf2,0x07,0x00]
        cmp     r2, #7

; Unary operations are encoded as "n0".
; CHECK: cpl r3           ; encoding: [0x91,0x30]
        cpl     r3
; CHECK: neg r3           ; encoding: [0x81,0x30]
        neg     r3
; CHECK: cplb rl3         ; encoding: [0xb1,0x60]
        cplb    rl3
; CHECK: negb rl3         ; encoding: [0xa1,0x60]
        negb    rl3

; The shift count of an immediate shift goes in the high nibble ("#n").
; CHECK: shl r2, #3       ; encoding: [0x5c,0x32]
        shl     r2, #3
; CHECK: shl r2, r3       ; encoding: [0x4c,0x23]
        shl     r2, r3
; CHECK: shr r2, #5       ; encoding: [0x7c,0x52]
        shr     r2, #5
; CHECK: ashr r2, #2      ; encoding: [0xbc,0x22]
        ashr    r2, #2
; CHECK: rol r2, r3       ; encoding: [0x0c,0x23]
        rol     r2, r3
; CHECK: ror r2, #1       ; encoding: [0x3c,0x12]
        ror     r2, #1

; MOVBZ and MOVBS reverse the usual nibble order ("mn").
; CHECK: movbz r4, rl5    ; encoding: [0xc0,0xa4]
        movbz   r4, rl5
; CHECK: movbs r6, rh7    ; encoding: [0xd0,0xf6]
        movbs   r6, rh7

; DIV repeats the register number in both nibbles ("nn").
; CHECK: mul r2, r3       ; encoding: [0x0b,0x23]
        mul     r2, r3
; CHECK: mulu r2, r3      ; encoding: [0x1b,0x23]
        mulu    r2, r3
; CHECK: div r3           ; encoding: [0x4b,0x33]
        div     r3
; CHECK: divu r5          ; encoding: [0x5b,0x55]
        divu    r5

; Moves.
; CHECK: mov r2, r3       ; encoding: [0xf0,0x23]
        mov     r2, r3
; CHECK: movb rl2, rh3    ; encoding: [0xf1,0x47]
        movb    rl2, rh3
; CHECK: mov r2, #4660    ; encoding: [0xe6,0xf2,0x34,0x12]
        mov     r2, #4660
; CHECK: mov r2, [r3+#8]  ; encoding: [0xd4,0x23,0x08,0x00]
        mov     r2, [r3+#8]
; CHECK: mov [r0+#4], r2  ; encoding: [0xc4,0x20,0x04,0x00]
        mov     [r0+#4], r2
; CHECK: mov r2, [r3]     ; encoding: [0xd4,0x23,0x00,0x00]
        mov     r2, [r3]
; CHECK: movb rl2, [r3+#1] ; encoding: [0xf4,0x43,0x01,0x00]
        movb    rl2, [r3+#1]

; An SFR name stands for its address, so this assembles as MOV reg, mem and
; prints back with the address spelled out.  MDL is at FE0EH and MDH at FE0CH.
; CHECK: mov r2, 65038    ; encoding: [0xf2,0xf2,0x0e,0xfe]
        mov     r2, mdl
; CHECK: mov 65036, r3    ; encoding: [0xf6,0xf3,0x0c,0xfe]
        mov     mdh, r3

; Control flow.
; CHECK: ret              ; encoding: [0xcb,0x00]
        ret
; CHECK: reti             ; encoding: [0xfb,0x88]
        reti
; CHECK: nop              ; encoding: [0xcc,0x00]
        nop
; CHECK: calli cc_UC, [r5] ; encoding: [0xab,0x05]
        calli   cc_UC, [r5]
; CHECK: jmpi cc_UC, [r6] ; encoding: [0x9c,0x06]
        jmpi    cc_UC, [r6]
; CHECK: push r4          ; encoding: [0xec,0xf4]
        push    r4
; CHECK: pop r4           ; encoding: [0xfc,0xf4]
        pop     r4

; The bit-addressable space is named by an 8 bit word offset: F0H to FFH are
; R0 to R15, and a special function register in FF00H to FFDEH keeps the short
; address it already has.  BSET and BCLR put the bit position in the high
; nibble of the opcode byte.
; CHECK: bset r5.3        ; encoding: [0x3f,0xf5]
        bset    r5.3
; CHECK: bclr r5.3        ; encoding: [0x3e,0xf5]
        bclr    r5.3
; CHECK: bset 136.10      ; encoding: [0xaf,0x88]
        bset    psw.10
; CHECK: bclr 135.0       ; encoding: [0x0e,0x87]
        bclr    mdc.0

; A word written as a number closes up with its bit position in decimal, but a
; hexadecimal one has to be spaced out: the lexer reads "0x88.15" as a broken
; floating point literal before the target ever sees it.
; CHECK: bset 136.10      ; encoding: [0xaf,0x88]
        bset    136.10
; CHECK: bset 136.15      ; encoding: [0xff,0x88]
        bset    0x88 . 15

; The two operand bit instructions name the destination first but encode the
; source first, and pack the source bit position into the high nibble of the
; last byte.
; CHECK: band r2.1, r3.2  ; encoding: [0x6a,0xf3,0xf2,0x21]
        band    r2.1, r3.2
; CHECK: bor r2.1, r3.2   ; encoding: [0x5a,0xf3,0xf2,0x21]
        bor     r2.1, r3.2
; CHECK: bxor r2.1, r3.2  ; encoding: [0x7a,0xf3,0xf2,0x21]
        bxor    r2.1, r3.2
; CHECK: bmov r2.1, r3.2  ; encoding: [0x4a,0xf3,0xf2,0x21]
        bmov    r2.1, r3.2
; CHECK: bmovn r2.1, r3.2 ; encoding: [0x3a,0xf3,0xf2,0x21]
        bmovn   r2.1, r3.2
; CHECK: bcmp r2.1, r3.2  ; encoding: [0x2a,0xf3,0xf2,0x21]
        bcmp    r2.1, r3.2

; BFLDL and BFLDH are byte-swapped with respect to each other: the mask and the
; value change places.
; CHECK: bfldl r4, #15, #5 ; encoding: [0x0a,0xf4,0x0f,0x05]
        bfldl   r4, #0x0f, #0x05
; CHECK: bfldh r4, #240, #80 ; encoding: [0x1a,0xf4,0x50,0xf0]
        bfldh   r4, #0xf0, #0x50

; Add and subtract with carry: the plain opcodes plus 10H.
; CHECK: addc r3, r5      ; encoding: [0x10,0x35]
        addc    r3, r5
; CHECK: addc r3, #1234   ; encoding: [0x16,0xf3,0xd2,0x04]
        addc    r3, #1234
; CHECK: addc r3, total   ; encoding: [0x12,0xf3,A,A]
        addc    r3, total
; CHECK: subc r3, r5      ; encoding: [0x30,0x35]
        subc    r3, r5
; CHECK: subc r3, #1234   ; encoding: [0x36,0xf3,0xd2,0x04]
        subc    r3, #1234
; CHECK: subc r3, total   ; encoding: [0x32,0xf3,A,A]
        subc    r3, total

; The 8 bit "reg" field addresses a GPR as F0H + n and a special function
; register by its short address, so PUSH and POP reach both.
; CHECK: push mdc         ; encoding: [0xec,0x87]
        push    mdc
; CHECK: push mdl         ; encoding: [0xec,0x07]
        push    mdl
; CHECK: push mdh         ; encoding: [0xec,0x06]
        push    mdh
; CHECK: pop mdh          ; encoding: [0xfc,0x06]
        pop     mdh
; CHECK: pop mdl          ; encoding: [0xfc,0x07]
        pop     mdl
; CHECK: pop mdc          ; encoding: [0xfc,0x87]
        pop     mdc
; CHECK: push psw         ; encoding: [0xec,0x88]
        push    psw
; CHECK: push cp          ; encoding: [0xec,0x08]
        push    cp
; CHECK: push sp          ; encoding: [0xec,0x09]
        push    sp

; The same name still stands for the register's address everywhere else.
; CHECK: mov r2, 65038    ; encoding: [0xf2,0xf2,0x0e,0xfe]
        mov     r2, mdl

; The protected instructions repeat their opcode in the second word.
; CHECK: srst             ; encoding: [0xb7,0x48,0xb7,0xb7]
        srst
; CHECK: idle             ; encoding: [0x87,0x78,0x87,0x87]
        idle
; CHECK: pwrdn            ; encoding: [0x97,0x68,0x97,0x97]
        pwrdn
; CHECK: diswdt           ; encoding: [0xa5,0x5a,0xa5,0xa5]
        diswdt
; CHECK: srvwdt           ; encoding: [0xa7,0x58,0xa7,0xa7]
        srvwdt
; CHECK: einit            ; encoding: [0xb5,0x4a,0xb5,0xb5]
        einit

; The extend instructions take an instruction range written 1 to 4 but
; encoded 0 to 3.
; CHECK: exts r5, #2      ; encoding: [0xdc,0x15]
        exts    r5, #2
; CHECK: extp r5, #2      ; encoding: [0xdc,0x55]
        extp    r5, #2
; CHECK: extsr r5, #2     ; encoding: [0xdc,0x95]
        extsr   r5, #2
; CHECK: extpr r5, #2     ; encoding: [0xdc,0xd5]
        extpr   r5, #2
; CHECK: exts #1, #1      ; encoding: [0xd7,0x00,0x01,0x00]
        exts    #1, #1
; CHECK: extsr #18, #4    ; encoding: [0xd7,0xb0,0x12,0x00]
        extsr   #0x12, #4
; CHECK: extp #3, #4      ; encoding: [0xd7,0x70,0x03,0x00]
        extp    #3, #4

; The high two bits of a ten bit page number land in the last byte.
; CHECK: extpr #1023, #1  ; encoding: [0xd7,0xc0,0xff,0x03]
        extpr   #0x3ff, #1
; CHECK: extr #1          ; encoding: [0xd1,0x80]
        extr    #1
; CHECK: atomic #2        ; encoding: [0xd1,0x10]
        atomic  #2

; Inter-segment control flow names the target segment as an operand.
; CHECK: jmps #1, far_target  ; encoding: [0xfa,0x01,A,A]
        jmps    #1, far_target
; CHECK: calls #2, far_target ; encoding: [0xda,0x02,A,A]
        calls   #2, far_target
; CHECK: rets             ; encoding: [0xdb,0x00]
        rets
