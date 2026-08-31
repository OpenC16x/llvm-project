; RUN: not llc -mtriple=c166 -mattr=+mac -O2 -o /dev/null %s 2>&1 | FileCheck %s --check-prefix=MAC
; RUN: not llc -mtriple=c166 -O2 -o /dev/null %s 2>&1 | FileCheck %s --check-prefix=NOMAC

; Asking for a copy the machine cannot make is a user error, because the only
; way to ask is to name the register: it used to abort the compiler instead.

; MAS is the saturated view of the accumulator's high word and has no address
; of its own - CoSTORE names it by a five bit code - so no move can reach it.
; MAC: error: {{.*}}cannot copy mas: it has no address, so no move can reach it
define void @mas(i16 %v) {
  call void asm sideeffect "nop", "{mas}"(i16 %v)
  ret void
}

; A byte write to a word wide special function register writes the whole word,
; with 00H in the half that was not addressed, so pinning a byte value to one
; would throw away the other half of it without saying so.
; MAC: error: {{.*}}cannot copy mal: a byte access to a special function register writes the whole word, so only a word value can be pinned to one
define void @byte(i8 %v) {
  call void asm sideeffect "nop", "{mal}"(i8 %v)
  ret void
}

; The register file holds every part's map at once, so a name is spelled the
; same whatever is selected.  Whether the part has the register is a separate
; question, and one that has to be asked here: this never passes the
; assembler, so an address that means something else on this part would
; otherwise be written with nothing said.  QX0 belongs to the coprocessor.
; NOMAC: error: {{.*}}cannot copy qx0: the selected processor does not have it
define void @qx0(i16 %v) {
  call void asm sideeffect "nop", "{qx0}"(i16 %v)
  ret void
}
