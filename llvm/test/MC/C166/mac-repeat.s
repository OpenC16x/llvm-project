; RUN: llvm-mc -triple=c166 -mcpu=xc16x -show-encoding < %s | FileCheck %s
; RUN: llvm-mc -triple=c166 -mcpu=st10 -mattr=+mac -show-encoding < %s \
; RUN:   | FileCheck %s
; RUN: llvm-mc -triple=c166 -mcpu=xc16x -filetype=obj -o %t.o < %s
; RUN: llvm-objdump -d --mcpu=xc16x %t.o | FileCheck %s --check-prefix=DIS
;; And what it printed assembles to the same bytes, which is the check the
;; DIS lines above cannot make: they say the text is right, not that the text
;; means what the bytes did.
; RUN: llvm-objdump -d --mcpu=xc16x %t.o | sed -n '/<.text>/,$p' | tail -n +2 \
; RUN:   | sed -E 's/^[[:space:]]*[0-9a-f]+:[[:space:]]*([0-9a-f]{2} )+[[:space:]]*//' \
; RUN:   > %t.rt.s
; RUN: llvm-mc -triple=c166 -mcpu=xc16x -filetype=obj -o %t.rt.o %t.rt.s
; RUN: diff %t.o %t.rt.o
; RUN: not llvm-mc -triple=c166 -mcpu=xc16x %S/Inputs/mac-repeat-bad.s 2>&1 \
; RUN:   | FileCheck %s --check-prefix=BAD

;; The repeat prefix, which the ST10 Family Programming Manual (PM0036)
;; section 2.4.7 writes "Repeat #data5 times CoXXX..." or "Repeat MRW times
;; CoXXX...".  It puts two tokens in front of the mnemonic, which is why it
;; was not accepted before: the matcher keys on the mnemonic being first.  The
;; parser takes it apart instead, and the count goes on the end where its
;; operand is.
;;
;; The field is five bits at 31 to 27 - "rrrr:r" in every Format line the
;; manual gives a repeatable form.  What the manual does not give is a table
;; mapping a written count onto a field value; it gives three facts, and they
;; leave one reading.  Zero is the plain form, which is what every
;; non-repeatable form encodes.  One is MRW, which 2.4.7 says outright.  So a
;; literal count is the field itself, and the two values that would need a
;; guess - 0 and 1 - are refused rather than encoded.

;; Without a prefix nothing changes: the field is zero and this is the byte
;; this backend has always emitted.
; CHECK: comac r2, [r3+]  ; encoding: [0x83,0x23,0xd0,0x02]
        comac   r2, [r3+]

;; With one, the count is the top five bits of the last byte.  3 is 00011, so
;; 00011_010 with the pointer control below it.
; CHECK: repeat 3 times	comac r2, [r3+] ; encoding: [0x83,0x23,0xd0,0x1a]
        repeat 3 times comac r2, [r3+]

;; MRW is 1, not a count of one.
; CHECK: repeat mrw times	comac r2, [r3+] ; encoding: [0x83,0x23,0xd0,0x0a]
        repeat mrw times comac r2, [r3+]

;; 31 is the largest the field holds, which is what "must be less than 32"
;; means.
; CHECK: repeat 31 times	comac [idx0+], [r9+] ; encoding: [0x93,0x29,0xd0,0xfa]
        repeat 31 times comac [idx0+], [r9+]

;; The mnemonics ending in a minus are glued back together after the prefix
;; has been taken off, the same way they are without one.
; CHECK: repeat 3 times	comac- r3, [r7-qr0] ; encoding: [0x83,0x37,0xe0,0x1d]
        repeat 3 times comac- r3, [r7-qr0]

;; Every shape the manual marks repeatable, one of each: a register and a
;; pointer, two pointers, a pointer alone, a MAC pointer alone, a shift by a
;; register, a shift through a pointer, and a store.
; CHECK: repeat 4 times	coashr r5 ; encoding: [0xa3,0x55,0xaa,0x20]
        repeat 4 times coashr r5
; CHECK: repeat 4 times	coshl [r4] ; encoding: [0x83,0x44,0x8a,0x21]
        repeat 4 times coshl [r4]
; CHECK: repeat 4 times	comov [idx0+], [r8+] ; encoding: [0xd3,0x28,0x00,0x22]
        repeat 4 times comov [idx0+], [r8+]
; CHECK: repeat 4 times	conop [r9+] ; encoding: [0x93,0x19,0x5a,0x22]
        repeat 4 times conop [r9+]
; CHECK: repeat 4 times	costore [r2-], mal ; encoding: [0xb3,0x22,0x20,0x23]
        repeat 4 times costore [r2-], mal

;; Read back, a repeated instruction is written the way it was.  The plain
;; form has no prefix at all rather than "repeat 0 times", because zero is not
;; a repetition.
; DIS: comac	r2, [r3+]
; DIS: repeat 3 times	comac	r2, [r3+]
; DIS: repeat mrw times	comac	r2, [r3+]
; DIS: repeat 31 times	comac	[idx0+], [r9+]

;; A count outside the field, and the two the manual leaves ambiguous.
; BAD: error: repeat count must be in the range [2, 31], or MRW
; BAD: error: repeat count must be in the range [2, 31], or MRW
; BAD: error: repeat count must be in the range [2, 31], or MRW
;; And the forms whose Rep column says no.  It is not the addressing mode that
;; decides: CoLOAD and CoMUL take a pointer just as CoMAC does, and nothing
;; accumulates across their repetitions.
; BAD: error: this instruction cannot be repeated
; BAD: error: this instruction cannot be repeated
; BAD: error: this instruction cannot be repeated
; BAD: error: this instruction cannot be repeated
; BAD: error: this instruction cannot be repeated
