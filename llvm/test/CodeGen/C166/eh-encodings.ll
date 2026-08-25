; RUN: llc -mtriple=c166 -filetype=obj -o %t.o < %s
; RUN: llvm-readelf -r %t.o | FileCheck %s
; RUN: llc -mtriple=c166 -o - < %s | FileCheck %s --check-prefix=ASM

; The exception tables hold two kinds of address and DW_EH_PE_absptr cannot
; tell them apart: it is emitted at the code pointer size, which is four bytes
; here because a code address is the 24 bit CSP:IP.
;
; A type table entry is not one of those.  It holds a pointer to a type
; information object, which is what a program's own code dereferences, so it is
; a near data pointer - two bytes, the size uintptr_t has on this target and
; the size the type information objects use for their own contents.  Written at
; four it would not survive being read into a uintptr_t.
;
; The personality routine and the language specific data area are the other
; kind.  The unwinder resolves them as physical addresses rather than loading
; them into a pointer, and the sections they name are linked into Flash above
; the first 64 KByte, so they keep all four bytes.

; ASM: .byte 2 {{.*}} @TType Encoding
; ASM: .short _ZTI4Oops

; CHECK: .rela.gcc_except_table
; CHECK-NEXT: Offset
; CHECK-NEXT: R_C166_SOF16 {{.*}} _ZTI4Oops

; CHECK: .rela.eh_frame
; CHECK-NEXT: Offset
; CHECK-NEXT: R_C166_ABS32 {{.*}} __gxx_personality_v0
; CHECK: R_C166_ABS32 {{.*}} .gcc_except_table

@_ZTI4Oops = external constant ptr

declare i32 @risky(i32)
declare i32 @__gxx_personality_v0(...)
declare ptr @__cxa_begin_catch(ptr)
declare void @__cxa_end_catch()

define i32 @guarded(i32 %n) personality ptr @__gxx_personality_v0 {
entry:
  %r = invoke i32 @risky(i32 %n) to label %done unwind label %lpad

lpad:
  %ex = landingpad { ptr, i32 } catch ptr @_ZTI4Oops
  %p = extractvalue { ptr, i32 } %ex, 0
  %o = call ptr @__cxa_begin_catch(ptr %p)
  %v = load i32, ptr %o
  call void @__cxa_end_catch()
  br label %done

done:
  %res = phi i32 [ %r, %entry ], [ %v, %lpad ]
  ret i32 %res
}
