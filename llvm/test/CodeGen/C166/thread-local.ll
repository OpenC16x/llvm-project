; This part runs one thread, so per-thread storage and static storage are the
; same storage: C166LowerThreadLocal takes the thread-local marker off every
; global that has one, and everything after that treats them as ordinary data.
;
; RUN: llc -mtriple=c166 -O2 < %s | FileCheck %s
; RUN: llc -mtriple=c166 -O0 < %s | FileCheck %s --check-prefix=SECTIONS

@counter = dso_local thread_local global i16 0, align 2
@seeded = dso_local thread_local global i16 7, align 2

; An access is the same instruction a plain global would get: the address is a
; link time constant, with no call and no thread pointer anywhere in it.
;
; CHECK-LABEL: bump:
; CHECK:         mov r2, counter
; CHECK:         add r2, #1
; CHECK:         mov counter, r2
; CHECK-NOT:     calla
; CHECK-NOT:     __emutls
define i16 @bump() {
  %v = load i16, ptr @counter, align 2
  %n = add i16 %v, 1
  store i16 %n, ptr @counter, align 2
  ret i16 %n
}

; llvm.threadlocal.address is the address of the object itself here.  The
; verifier requires its operand to still be thread-local, so the pass has to
; replace these calls before it clears the markers rather than after.
;
; CHECK-LABEL: viaIntrinsic:
; CHECK:         mov r2, seeded
; CHECK-NOT:     llvm.threadlocal
define i16 @viaIntrinsic() {
  %p = call ptr @llvm.threadlocal.address.p0(ptr @seeded)
  %v = load i16, ptr %p, align 2
  ret i16 %v
}

declare nonnull ptr @llvm.threadlocal.address.p0(ptr nonnull)

; The initialised one lands in .data and the zero one in .bss, so crt0's
; existing copy and zero loops cover them and the linker script needs no
; .tdata or .tbss of its own.
;
; SECTIONS-NOT: .tbss
; SECTIONS: .section .bss
; SECTIONS: counter:
; SECTIONS-NOT: .tdata
; SECTIONS: .data
; SECTIONS: seeded:
; SECTIONS: .short 7
