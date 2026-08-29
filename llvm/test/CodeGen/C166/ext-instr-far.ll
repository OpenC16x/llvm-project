; RUN: llc -mtriple=c166 -mcpu=c167 -O2 < %s | FileCheck %s --check-prefix=EXT
; RUN: not llc -mtriple=c166 -mcpu=c166 -O2 < %s 2>&1 \
; RUN:   | FileCheck %s --check-prefix=ERR

; A far object is a segment and an offset, and the only way to hand a segment
; to an ordinary MOV is the EXTS in front of it.  There is no sequence that
; stands in for one - rewriting a data page pointer would reach the object and
; silently redirect every near address sharing that pointer, including the ones
; an interrupt taken in the middle would use - so this is a diagnostic.

define i16 @far_load(ptr addrspace(1) %p) {
; EXT-LABEL: far_load:
; EXT:         exts r3, #1
; EXT-NEXT:    mov r2, [r2]
;
; ERR: error: {{.*}}in function far_load {{.*}}: cannot reach a far object on this part: a load needs an EXTS, which the first generation of the family does not have; -mcpu=c167 or later has it
  %v = load i16, ptr addrspace(1) %p
  ret i16 %v
}

define void @far_store(ptr addrspace(1) %p, i16 %v) {
; EXT-LABEL: far_store:
; EXT:         exts r3, #1
; EXT-NEXT:    mov [r2], r4
;
; ERR: error: {{.*}}in function far_store {{.*}}: cannot reach a far object on this part: a store needs an EXTS, which the first generation of the family does not have; -mcpu=c167 or later has it
  store i16 %v, ptr addrspace(1) %p
  ret void
}
