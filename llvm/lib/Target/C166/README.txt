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

Segmented addressing
--------------------

A C166 in segmented mode reaches 16 MByte.  Data addressing normally goes
through the four Data Page Pointers: the top two bits of a 16 bit long or
indirect address select a DPP, whose 10 bit page number supplies A23-A14.  The
EXTend instructions override that for the next 1 to 4 instructions, either with
an explicit page (EXTP) or by treating the address as a plain 16 bit offset
into an explicit segment (EXTS).  Code crosses segments with JMPS and CALLS and
returns with RETS, which pops the code segment pointer along with the
instruction pointer.

Far data
~~~~~~~~

Address space 1 holds far pointers.  A far pointer is a linear 24 bit address
zero extended into 32 bits, so bits 15-0 are the offset within a segment, bits
23-16 are the segment, and pointer arithmetic is a plain 32 bit add that
crosses segment boundaries correctly rather than wrapping inside a page.  An
access through one becomes an EXTS naming the segment followed by an ordinary
16 bit access.

EXTS rather than EXTP is what fits that representation: EXTP would want the
address split into a 10 bit page and a 14 bit offset, which is a different
pointer layout, not a cheaper encoding of this one.  Neither buys anything over
the other in instructions or bytes.  Nothing here programs or tracks the DPP
registers; the standard reset mapping is assumed throughout, and startup code
that changes it is on its own.

Since EXTS only covers the single instruction after it, the pair is selected as
one pseudo, split by expandPostRAPseudo(), and left bundled so the post-RA
scheduler cannot put anything between them.  The hardware locks interrupts for
the duration, so an interrupt handler never sees the sequence half done.  A
displacement is deliberately never folded into a far access: [Rw + #data16]
wraps inside the segment instead of carrying into it, so the fold would only be
correct when the sum is known not to cross a 64 KByte boundary.

An addrspacecast widens or narrows the pointer, which assumes the reset
configuration of the DPP registers, where a 16 bit address maps onto the
identical physical address in segment 0.  Code that reprograms the DPPs has to
avoid the cast and build far pointers itself.

An object declared in address space 1 has its address built from two
relocations, one for the segment and one for the offset, and the segment goes
straight into the EXTS as an immediate rather than through a register.  Such
objects are placed in .fardata, .farbss or .farrodata so that a linker script
can put them outside segment 0; an explicit section still wins.

The C library's block moves take near pointers, so llvm.memcpy, llvm.memmove
and llvm.memset reaching into the far address space call entry points of their
own once they are too big or too dynamic to expand inline:

    void *__memcpy_far (void __far *dst, const void __far *src, unsigned n);
    void *__memmove_far(void __far *dst, const void __far *src, unsigned n);
    void *__memset_far (void __far *dst, int c, unsigned n);

Both pointers are far, so a near operand is widened on the way in under the
same DPP assumption as an addrspacecast, and the size stays 16 bit.

Far code
~~~~~~~~

A function carrying the "far" attribute is entered with CALLS and left with
RETS, and is placed in .fartext.  An interrupt handler still returns with RETI
whichever segment it sits in, because the hardware rather than a CALLS put the
return address on the stack.

A far function can only be called by name.  CALLI stays inside the current
segment and there is no indirect form of CALLS, so a pointer to a far function
would be a near address that nothing could call correctly; taking one is
diagnosed, both in code and in an initialiser.

Relocations and assembler syntax
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Four operators pick a field out of a symbol's 24 bit address, each with a
relocation of its own: seg(x) and sof(x) are the segment and the offset within
it, as EXTS, JMPS and CALLS want them, and pag(x) and pof(x) are the 10 bit
page and the 14 bit offset within it, as EXTP wants them.  Applied to something
the assembler can already work out they are folded on the spot instead.  They
are recognised at the top of an operand only, since a field of an address is
not something to do further arithmetic on.

Caveats the hardware imposes
~~~~~~~~~~~~~~~~~~~~~~~~~~~~

EXTS does not exist on the SAB 8XC166(W) devices, and no subtarget feature
guards that yet.  Inside a class A or class B trap handler the EXTend
instructions do nothing while a class B trap flag is set, so a far access in
one will read the wrong place.  There is also only one instruction counter, so
inline assembly must not wrap a far access in an ATOMIC or EXTend sequence of
its own.

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
* The far relocations are LLVM's own invention, like the rest of the C166 ELF
  scheme here: no linker implements them yet.
* A far access always costs an EXTS, even for several accesses in a row to the
  same segment, which one EXTS covering up to four instructions could do.
* Interrupt handlers do not save MDL/MDH/MDC, so an interrupted multiply or
  divide can be corrupted by an ISR that itself multiplies or divides.
* The ADDC/SUBC instructions are described but not used: wide integer
  arithmetic is expanded with explicit compares instead of a carry chain.
* Jump tables are disabled; switches become compare and branch chains.
* No tail calls, and no support for the C166 bit addressing instructions
  (BSET/BCLR/BAND/...) or the XC16x MAC unit.
