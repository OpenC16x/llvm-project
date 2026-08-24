; RUN: llc -mtriple=c166 -O2 < %s | FileCheck %s

; Setting one bit of a bit-addressable word is one instruction.  Two windows
; are: internal RAM at FD00H to FDFEH and the special function registers at
; FF00H to FFDEH, both by word address.
;
; It matters beyond the size.  The read, change, write form has a gap between
; the read and the write in which an interrupt touching the same word loses
; whatever it set; the bit instruction does both as one indivisible operation.
; tools/c166-sim/bit-rmw-race.s runs that difference rather than describing it.

define void @sfr_set() {
; CHECK-LABEL: sfr_set:
; CHECK:      bset p3.8
; CHECK-NEXT: ret
  %v = load volatile i16, ptr inttoptr (i16 -60 to ptr)
  %r = or i16 %v, 256
  store volatile i16 %r, ptr inttoptr (i16 -60 to ptr)
  ret void
}

define void @sfr_clear() {
; CHECK-LABEL: sfr_clear:
; CHECK:      bclr p3.8
; CHECK-NEXT: ret
  %v = load volatile i16, ptr inttoptr (i16 -60 to ptr)
  %r = and i16 %v, -257
  store volatile i16 %r, ptr inttoptr (i16 -60 to ptr)
  ret void
}

; FD20H is bit-addressable RAM, whose bitoff is (FD20H - FD00H) / 2 = 16.  It
; has no name to print, so the number is what comes out.
define void @ram_set() {
; CHECK-LABEL: ram_set:
; CHECK:      bset 16.0
; CHECK-NEXT: ret
  %v = load volatile i16, ptr inttoptr (i16 -736 to ptr)
  %r = or i16 %v, 1
  store volatile i16 %r, ptr inttoptr (i16 -736 to ptr)
  ret void
}

; FE00H is a special function register but not a bit-addressable one - only
; FF00H upwards is - so no bit instruction reaches it.  It is still a load, a
; change and a store, but the memory destination ALU form does all three, so
; what used to be three instructions is two.
define void @sfr_not_bit_addressable() {
; CHECK-LABEL: sfr_not_bit_addressable:
; CHECK: mov r2, #16
; CHECK-NEXT: or dpp0, r2
  %v = load volatile i16, ptr inttoptr (i16 -512 to ptr)
  %r = or i16 %v, 16
  store volatile i16 %r, ptr inttoptr (i16 -512 to ptr)
  ret void
}

; Two bits is not one bit.
define void @two_bits() {
; CHECK-LABEL: two_bits:
; CHECK: mov r2, #768
; CHECK-NEXT: or p3, r2
  %v = load volatile i16, ptr inttoptr (i16 -60 to ptr)
  %r = or i16 %v, 768
  store volatile i16 %r, ptr inttoptr (i16 -60 to ptr)
  ret void
}

; The loaded word is wanted afterwards, so the load has to stay a load: fusing
; it into the bit instruction would leave nothing to return.
define i16 @loaded_value_reused() {
; CHECK-LABEL: loaded_value_reused:
; CHECK: mov r2, p3
; CHECK: mov p3, r3
  %v = load volatile i16, ptr inttoptr (i16 -60 to ptr)
  %r = or i16 %v, 1
  store volatile i16 %r, ptr inttoptr (i16 -60 to ptr)
  ret i16 %v
}

; Another volatile access sits between the read and the write, and fusing the
; two would move it across one of them.
define void @access_in_between() {
; CHECK-LABEL: access_in_between:
; CHECK: mov r2, p3
; CHECK: mov dp3, r3
; CHECK: mov p3, r2
  %v = load volatile i16, ptr inttoptr (i16 -60 to ptr)
  store volatile i16 5, ptr inttoptr (i16 -58 to ptr)
  %r = or i16 %v, 1
  store volatile i16 %r, ptr inttoptr (i16 -60 to ptr)
  ret void
}

; Both are bit-addressable, but they are not the same word, so this is a read
; of one and a write of the other with the bit changed in between.
define void @load_and_store_differ() {
; CHECK-LABEL: load_and_store_differ:
; CHECK: mov r2, p3
; CHECK: bset r2.8
; CHECK: mov dp3, r2
  %v = load volatile i16, ptr inttoptr (i16 -60 to ptr)
  %r = or i16 %v, 256
  store volatile i16 %r, ptr inttoptr (i16 -58 to ptr)
  ret void
}

; An odd address names no bit-addressable word: they are word addresses.
define void @odd_address() {
; CHECK-LABEL: odd_address:
; CHECK-NOT: bset
; CHECK: or 65281, r2
  %v = load volatile i16, ptr inttoptr (i16 -255 to ptr)
  %r = or i16 %v, 2
  store volatile i16 %r, ptr inttoptr (i16 -255 to ptr)
  ret void
}
