//===---------------------------------------------------------------------===//
// Infineon C166 backend
//===---------------------------------------------------------------------===//

This is a backend for the Infineon C166 family of 16 bit microcontrollers
(C166/C167, and the closely related ST10 and XC16x parts).  It currently
generates assembly only; there is no assembly parser, disassembler or object
file writer yet, so "llc -filetype=obj" is not available.

Machine model
-------------

The C166 general purpose registers R0-R15 are a window into internal RAM
selected by the context pointer.  R0-R7 can additionally be accessed as pairs
of byte registers RL0/RH0 ... RL7/RH7; R8-R15 have no byte aliases, which is
modelled with sub-registers on the first eight word registers only.

Two stacks are in play.  Return addresses live on the small hardware stack
addressed by the SP special function register, which the CALL and RET
instructions manage on their own and which the compiler never has to address.
Everything the ABI cares about - incoming arguments, locals, spill slots and
outgoing arguments - lives on a second stack whose pointer is kept in R0.  R0
is an ordinary general purpose register, so frame slots are reachable with the
[Rw + #data16] addressing mode; the hardware stack pointer is not.  R1 is the
frame pointer when a function needs one.

Calling convention
------------------

Up to four word sized arguments are passed in R2-R5 and the rest on the ABI
stack; values are returned in R2-R5.  Narrow arguments and return values are
promoted to a full word.  Variadic functions receive every argument on the
stack so that the callee can walk the list with a plain pointer.  R1 and
R12-R15 are callee saved, R2-R11 are caller saved.

A function carrying the "interrupt" attribute returns with RETI and saves
every general purpose register it modifies.

Condition flags
---------------

Nearly every C166 instruction updates the condition flags, MOV included, so
nothing at all may be scheduled between a compare and the jump that reads its
result.  Conditional branches and selects are therefore selected as fused
pseudo instructions that carry the comparison operands, and only get split
into a real cmp/jmpa pair by C166InstrInfo::expandPostRAPseudo(), once every
pass that could have inserted an instruction in between has run.

Multiply and divide go through the MDL/MDH register pair and are expanded from
pseudos at the same point.

Because of that structure the flags are never live across an instruction
boundary, which is why the moves and the ALU instructions are free to leave
their PSW definition implicit.  Anything that starts consuming the flags
somewhere else has to model them properly first.

Known limitations / things to do
--------------------------------

* No assembly parser, disassembler or ELF object writer.  Adding the
  instruction encodings to C166InstrFormats.td is the first step.
* The segmented (24 bit) address model is not implemented; code and data are
  addressed with 16 bit near pointers only.  EXTP/EXTS/EXTR and the DPP
  registers are unused.
* Interrupt handlers do not save MDL/MDH/MDC, so an interrupted multiply or
  divide can be corrupted by an ISR that itself multiplies or divides.
* The ADDC/SUBC instructions are described but not used: wide integer
  arithmetic is expanded with explicit compares instead of a carry chain.
* Jump tables are disabled; switches become compare and branch chains.
* No tail calls, and no support for the C166 bit addressing instructions
  (BSET/BCLR/BAND/...) or the XC16x MAC unit.
