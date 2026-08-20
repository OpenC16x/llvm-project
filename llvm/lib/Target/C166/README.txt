//===---------------------------------------------------------------------===//
// Infineon C166 backend
//===---------------------------------------------------------------------===//

This is a backend for the Infineon C166 family of 16 bit microcontrollers
(C166/C167, and the closely related ST10 and XC16x parts).  It generates
assembly and ELF objects, assembles C166 assembly, and disassembles it again.

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

Encodings and the MC layer
--------------------------

Encodings are taken from the C166 Family Instruction Set Manual (V2.0,
2001-03).  An instruction is one or two 16 bit words emitted least significant
byte first, so the encoding is simply the 2 or 4 bytes of Inst.  The layout is
spelled out per instruction rather than inherited from shared format classes,
because several instructions deviate from the usual nibble order: MOVBZ and
MOVBS encode their operands as "mn", DIV repeats the register number in both
nibbles, and an immediate shift count goes in the high nibble ahead of the
register.  A GPR appearing in an 8 bit "reg" field is addressed as F0H + n.

Every relocatable field of a C166 instruction lives in the second word, so the
code emitter always records a fixup at byte offset 2.  There is no published
psABI for C166 ELF; ELFRelocs/C166.def defines the small set of absolute and
PC relative data relocations the backend needs and documents that it is a
scheme LLVM defines for itself.

The disassembler tries the single word table and then the double word table -
the opcode byte alone decides the length, so no opcode appears in both.

Operand parsing is syntax directed, because the printed form is what tells the
three flavours of 16 bit operand apart: "#1234" is an immediate, "label" is an
address, "[r1+#4]" is a memory reference.  A special function register name
stands for its address (MDL is FE0EH, MDH is FE0CH), since no instruction here
takes an SFR as a register operand.

Known limitations / things to do
--------------------------------

* Only GPRs are modelled in the 8 bit "reg" field, so an instruction that puts
  an SFR there - "ADD MDL, #1" and friends - can be neither assembled nor
  disassembled.  The assembler does understand an SFR name used as an address.
* The disassembler prints an SFR address numerically rather than by name, so
  "mov r2, mdl" comes back as "mov r2, 65038".
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
