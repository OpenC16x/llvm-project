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

A call in tail position becomes a jump once the frame is down, so the callee's
RET goes straight back to our caller and no return address is ever pushed onto
the small hardware stack.  That needs both ends to agree about how returning
works, so it is off for an interrupt handler, which comes back with RETI, and
and for a mismatched pair of near and far functions, since a near callee's RET
would pop half of what a far caller left on the hardware stack.  Far to far is
fine, but only through JMPS, which names the target segment: the JMPA a near
tail call turns into stays inside the segment this function happens to have
been called into, and nothing promises the linker put both functions in the
same one.  There is no inter-segment CALLI, so a far tail call has to be
direct.  Arguments that do not fit in registers rule one out as well: they
would be written where the frame is about to stop being.

A function carrying the "interrupt" attribute returns with RETI and saves
every general purpose register it modifies.  That includes the registers
arguments arrive in, so unlike a normal function it can be saving a register
that is still carrying a live incoming value; the spill therefore must not be
marked as the end of that value's life.

It also has to preserve the multiply/divide unit.  The C166 interrupts MUL and
DIV part way through rather than running them to completion, so MDL, MDH and
MDC can hold state belonging to whatever was interrupted, and a handler that
touches the unit - directly, or through a call to something that might -
pushes and pops all three.  MDC is saved first and restored last, because
reading MDL (which is what pushing it does) clears MDC.MDRIU, and writing MDL
or MDH back sets it again.  PSW.MULIP, the other half of that state, rides
along on the PSW the hardware stacks on entry and RETI restores.

Condition flags
---------------

Nearly every C166 instruction updates the zero and negative flags, MOV
included, so nothing at all may be scheduled between a compare and the jump
that reads its result.  Conditional branches and selects are therefore selected
as fused pseudo instructions that carry the comparison operands, and only get
split into a real cmp/jmpa pair by C166InstrInfo::expandPostRAPseudo(), once
every pass that could have inserted an instruction in between has run.

Multiply and divide go through the MDL/MDH register pair and are expanded from
pseudos at the same point.

The carry is different, and the difference is what makes wide arithmetic
cheap.  MOV, MOVB, MOVBZ, MOVBS, PUSH and POP are all documented as leaving V
and C alone, and those - plus the branches, the calls and the EXTend
instructions - are the only things the register allocator and the frame code
insert.  So a carry can survive being spilled, reloaded, copied or
two-address-copied, and a wider addition can be a real ADD/ADDC chain instead
of recomputing the carry with a compare.  The chain is held together by glue
through instruction selection and by the PSW def/use pair afterwards.

The PSW register is therefore modelled exactly to the extent of the carry:
every instruction that changes C says so, and the instructions above, which do
not, say nothing.  That is sound only because the fused pseudos mean no other
flag is ever live across an instruction boundary.  Anything that starts
consuming Z, N, E or V somewhere else has to model them separately first.

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

Switches
--------

A dense switch becomes a table of 16 bit block addresses in .rodata, indexed
by the scaled selector and jumped through with JMPI.  The table's address is a
relocatable constant, so the lookup folds into the displacement of a single
[Rw + #data16] load rather than building the address in a register first.
Sparse switches still become chains of compares.

Bit addressing
--------------

The bit-addressable space is named by an 8 bit "bitoff" word address rather
than by a full address: 00H to 7FH is internal RAM at FD00H + 2*bitoff, 80H to
EFH is the special function registers at FF00H + 2*(bitoff - 80H), and F0H to
FFH is R0 to R15.  An SFR's short register address is therefore also its
bitoff, since reg and bitoff land on the same word; but only FF00H to FFDEH is
bit addressable, so MDL, MDH, CP, SP and the DPPs, which live at FE00H and up,
have a short address and no bitoff.

BSET, BCLR, BAND, BOR, BXOR, BMOV, BMOVN, BCMP, BFLDL, BFLDH and the four bit
test branches are assembled and disassembled.  A bit address is written <word>.<bit>, where the word is a
register name, a bit-addressable SFR name, or the bitoff number itself.  The
assembly lexer counts '.' as part of an identifier, so "psw.3" arrives as a
single token and the bit instructions take their operands apart themselves;
that also means a hexadecimal word has to be spaced away from its bit, as
"0x88 . 15", while a decimal one can close up.

Two encoding details are easy to get backwards.  The two operand instructions
name the destination first and encode the source first, and pack the source
bit position into the high nibble of the last byte.  BFLDL and BFLDH are byte
swapped with respect to each other: BFLDL is "0A QQ @@ ##" and BFLDH is
"1A QQ ## @@", so the mask and the value change places.

The bit test branches JB, JNB, JBC and JNBS take a target that is a signed 8
bit count of words from the instruction after them, which reaches 127 words
either way.  That is the one relative field in the instruction set, so it has
the one target fixup kind, fixup_c166_rel8w, and the one relocation that is not
plain data, R_C166_PCREL8W.  A disassembly names the target address, but only
llvm-objdump asks for that; left to itself the printer gives the distance,
which is what can be handed back to the assembler.

Nothing generates any of these: whether an address is bit addressable is not
something the compiler can see from the IR, so it would take an intrinsic or
an address space to express it.

Short encodings
---------------

Several addressing modes exist in both a two byte and a four byte form, and
the short one is picked wherever it fits, which is worth about a tenth of the
code the backend emits.

Every arithmetic instruction also has a two byte form taking a three bit
constant, whose opcode is the register/register one plus eight, and that is
what a loop counter or a pointer step of two turns into.  The places that
build one of these by hand rather than selecting it - the frame adjustment,
the frame address expansion in eliminateFrameIndex(), and the compare that
expandPostRAPseudo() splits out of a fused branch - have to make the same
choice, or a disassembly stops assembling back to the bytes it came from.
That is what the soak test catches.

"[Rw]" is a two byte instruction of its own rather than "[Rw + #data16]" with
nothing added, so the two are separate instructions here and the displacement
of the long one is always printed, zero included.  A frame slot that turns out
to sit at offset zero is switched over by eliminateFrameIndex() once the
offset is known.  Likewise a constant of 0 to 15 goes in a two byte MOV with
the value in the high nibble of the second byte; short constants are always
zero extended (manual 6.5), so that is the whole range.

The assembler has to agree with the compiler about which form a given piece of
text means, otherwise a disassembly would not assemble back to the bytes it
came from.  For "[Rw]" the syntax settles it.  For a constant it does not, so
the immediate operand classes are chained narrowest last - Imm4 inside Data8
inside Data16 - which is what makes the matcher rank the short encoding first.
Only the first entry of SuperClasses counts towards that ranking, so it has to
be a chain and not a list.

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
address, "[r1+#4]" is a memory reference.

A special function register name means one of two things.  The 8 bit "reg"
field of PUSH and POP names a register directly - it addresses a GPR as F0H + n
and an SFR by its short address, which is why the modelled SFRs carry that
short address as their hardware encoding.  Everywhere else an SFR name stands
for the address it is mapped to (MDL is FE0EH, MDH is FE0CH).  The parser
produces one operand that can be either and lets the matcher decide, since
which one is meant is a property of the instruction rather than of the name.

Known limitations / things to do
--------------------------------

* PUSH and POP are the only instructions whose 8 bit "reg" field reaches the
  special function registers.  Everything else that could put an SFR there -
  "ADD MDL, #1" and friends - can be neither assembled nor disassembled.
  Widening them means the operand class has to hold both kinds of register,
  which PUSH and POP can do because they have no pattern to satisfy: a class
  covering GPRs and SFRs cannot be the result of a codegen pattern without
  becoming what the register allocator constrains those results to.  Word and
  byte forms would need one such class each, since "reg" F0H + n names a word
  register in a word instruction and a byte register in a byte one.
* Only the handful of special function registers the backend has a use for are
  modelled, so "push t0" is not understood and its encoding does not decode.
  The assembler does know the address of a few more, which is enough to name
  them where an address is what is wanted.
* Outside the "reg" field an SFR is just an address, and the disassembler
  prints it numerically: "mov r2, mdl" comes back as "mov r2, 65038".
* The far relocations are LLVM's own invention, like the rest of the C166 ELF
  scheme here: no linker implements them yet.
* A far access always costs an EXTS, even for several accesses in a row to the
  same segment, which one EXTS covering up to four instructions could do.
* A handler that uses the multiply/divide unit saves it whole, and one that
  calls anything at all is assumed to: there is no way to see whether the
  callee multiplies, so three words go on the hardware stack either way.
* No support for the XC16x MAC unit.
