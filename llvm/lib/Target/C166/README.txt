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
for a mismatched pair of near and far functions, since a near callee's RET
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

One MUL leaves both halves of the product behind, so SMUL_LOHI and UMUL_LOHI
are kept whole rather than expanded: a widening multiply is one MUL and two
moves.  Splitting them would make mul and mulhs separate nodes again, and each
would issue a MUL of its own.  On a part with the coprocessor it is a CoMUL
and two CoSTOREs instead; see below.

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

Neither half of the address needs a register when both are settled at link
time: EXTS covers a long (mem) address just as it covers an indirect one, so a
far global is read with "exts #seg(g), #1" and then "mov r2, sof(g)" outright.

Two EXTends reaching the same object are folded into one, which is worth about
a quarter of the code of a far-data-heavy program.  That is sound only if the
object does not straddle a segment boundary, and nothing in an object file says
it does not: once the two have been folded there is no second segment left to
disagree with the first, so a straddling object would be reached at the right
offset in the wrong segment.  The linker rejects such a placement, which is
where the placement is finally known - c166.ld asserts the same of its regions,
earlier and more coarsely, so a script that gets it wrong is caught either way.
The check is a little stronger than the fold needs, since it also rejects a
straddling object in code that happened not to be folded; carrying the fold's
assumption into the object file as a relocation of its own would be a larger
thing to add than the case is worth.

A constant offset into a far object folds into its address rather than
becoming arithmetic on it, since both relocations carry an addend.  That only
applies to an object actually declared in the far address space: a near one
reached through a cast has the offset added to its 16 bit address before the
cast widens it, and folding that into the symbol would only be right if the
addition could not wrap.

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

    void __far *__memcpy_far (void __far *dst, const void __far *src, unsigned n);
    void __far *__memmove_far(void __far *dst, const void __far *src, unsigned n);
    void __far *__memset_far (void __far *dst, int c, unsigned n);

Both pointers are far, so a near operand is widened on the way in under the
same DPP assumption as an addrspacecast, and the size stays 16 bit.

The three live in startup/mem.c beside the near ones, and are the same byte at
a time loops with the pointer type changed.  That is deliberate: stepping a far
pointer is a 32 bit add whose carry out of the offset lands in the segment, so
the crossing is the compiler's arithmetic rather than something to open code,
and writing them any other way would be reimplementing it by hand.

Each one carries a no_builtin, without which it calls itself.  Loop idiom
recognition turns a byte at a time copy or fill back into a memcpy or a memset,
and declines to do so only inside a function actually named memcpy or memset -
which is how the near versions escape it.  These are not named that, so the
loop becomes a block move through far pointers, which is a call to the function
it is the body of.  It builds and links; it is an infinite recursion at run
time.  The attribute keeps that fixed wherever the file is compiled, which a
flag in the script that builds it would not.

Far pointer arithmetic across a segment boundary is what
utils/C166Sim/differential/farptr.c is for.  It walks a pointer one byte at a
time across one rather than sampling either side, checks the addresses it
produces as values as well as using them, and runs the block moves above with
both source and destination crossing.  The span it uses is chosen for the
crossing and not for the part: it is plain memory in the simulator and nothing
on a real XC164CM.  What is under test is the address arithmetic.

Two things about that test are worth keeping if it is ever rewritten.  A block
move only exercises the crossing if it is long enough to reach it - the first
version's moves all sat on one side of the boundary and passed against a
deliberately broken memcpy.  And whatever a move writes past the crossing has
to land inside the range the checksums cover, or a move that went to the wrong
segment corrupts only bytes nothing looks at and the test passes anyway.  Both
of those were found by breaking the implementation on purpose and watching the
test not notice.

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

The other direction is a constraint on the memory map rather than on the code.
A near call takes its segment from CSP, so it reaches the segment the caller is
in and no other, and code placed outside the segment .text ended up in can only
call functions that are themselves far.  That is not hypothetical: c166.ld puts
the PSRAM at E0'0000H and the Flash at C0'0000H, so a function in .psramtext
calling an ordinary one lands in the PSRAM instead - the sixteen bits that fit
are written and the branch goes to that offset in the wrong segment.

Nothing in the instruction, and nothing in a plain 16 bit relocation, would say
so, which is why a near branch or call carries R_C166_CADDR16 instead: the same
sixteen bits, under a name that tells the linker the field is a code address
reached without changing segments.  LLD then refuses a target that is somewhere
else, naming both addresses.  .text and .fartext do share a segment in c166.ld,
which is why an ordinary far function may still be called near from the Flash.

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

Wide shifts
-----------

A shift of a value two words wide by an amount only known at run time is
expanded inline rather than becoming a call to __ashlsi3 and its relatives.
The shift instructions take their count from a register, so the pieces are
there; what has to be worked around is that the count is read as its low four
bits, which makes a shift by sixteen a shift by nothing.

The obvious expansion brings the bits that cross between the halves over with
a shift by "16 - amount", and that is wrong at an amount of zero: instead of
contributing nothing it contributes the whole word.  Shifting by "15 - amount"
and then once more is right there and the same everywhere else, and costs one
instruction rather than a test and a branch.

The same masking is useful in the other direction.  When the amount is sixteen
or more the answer is one half shifted by "amount - 16", and the hardware is
already reading the count modulo sixteen, so that is the same instruction the
small case needs, computed once and used twice.

Which half ends up where is then two selects on bit 4 of the amount.  A 32 bit
division is still a call.

EXTS does not exist on the first generation of the family - the SAB 80C166 and
83C166 - and neither does ATOMIC.  FeatureExtInstr is what says so; -mcpu=c166
is that generation by name and clears it, and everything else in the processor
list has it, "generic" included, because every part -mmcu= knows is second
generation and so are the linker script and startup code here.  A far access
without it is a diagnostic, since nothing stands in for an EXTS: rewriting a
data page pointer would reach the object and also redirect every near address
sharing that pointer, an interrupt handler's included.  A read-modify-write
without it goes round the compare and exchange loop, which holds together by
clearing PSW.IEN rather than by counting instructions.

The predicate gates the assembler and the disassembler as well, which is the
point rather than a side effect: an EXTS in a listing for a part that has no
EXTS is not an EXTS.

Inside a class A or class B trap handler the EXTend instructions do nothing
while a class B trap flag is set, so a far access in one will read the wrong
place.  There is also only one instruction counter, so inline assembly must
not wrap a far access in an ATOMIC or EXTend sequence of its own.

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
either way.  JMPR counts the same way, and there are two fixup kinds because
how far the displacement byte is from the end of the instruction differs: two
bytes in the four byte bit test branches, one in the two byte JMPR.

There is only one relocation for the two, though, because only one of them can
need it.  A bit test branch has no long form, so a target the assembler cannot
place has to become R_C166_PCREL8W and let the linker check the range.  JMPR
does have one, so the same target grows it into a JMPA instead and no
relocation is left behind.  A disassembly names the target address, but only
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

A branch is selected as the two byte JMPR.  Whether the target is within the
127 words it reaches is not known until the layout is, so C166AsmBackend grows
the ones that are not into the four byte JMPA; the two take their operands in
the same order, so that is only a change of opcode.  A displacement written as
a number rather than a label is left alone, since it is a distance and the
long form takes an address.

Every arithmetic instruction also has a two byte form taking a three bit
constant, whose opcode is the register/register one plus eight, and that is
what a loop counter or a pointer step of two turns into.  The places that
build one of these by hand rather than selecting it - the frame adjustment,
the frame address expansion in eliminateFrameIndex(), and the compare that
expandPostRAPseudo() splits out of a fused branch - have to make the same
choice, or a disassembly stops assembling back to the bytes it came from.
That is what the soak test catches.

"[Rw]" is a two byte instruction of its own rather than "[Rw + #data16]" with
nothing added, and "[Rw+]", which reads and then steps the pointer by the
width of the access, is a third.  All three are separate instructions here,
which is why the displacement of the long one is always printed, zero
included.  A post-incrementing load is selected by hand in
C166ISelDAGToDAG.cpp, since writing the stepped pointer back makes it a two
result instruction that no pattern describes; there is no matching store,
because the only auto-stepping store form is the pre-decrementing "[-Rw]".  A frame slot that turns out
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

C front end
-----------

clang knows the c166 triple, so C sources compile without going through a
separate front end.  int is 16 bits, long is 32, long long is 64, and nothing
is aligned to more than a word because the bus is a word wide.

The driver's C166 toolchain passes -nostdsysteminc, since the build machine's
/usr/include describes the build machine and not a C166 part, which is what
lets clang's own stdint.h and friends be the ones that answer an #include.  It
links with LLD, which is the only linker that knows these relocations, and
puts crt0.o first because the reset vector is in it.  There is no default
linker script: the memory map belongs to the board, so -T is not optional.

There is no frame pointer by default.  Sixteen registers are not enough to
give one up, and nothing walks the stack anyway.

Three things the backend reads are spelled in C as attributes:

  __attribute__((interrupt))  the "interrupt" function attribute
  __attribute__((far))        the "far" function attribute
  __far, i.e. address space 1 far data

"far" is applied to declarations as well as definitions, because it is what
tells a caller in another translation unit to use CALLS rather than CALL.
Its spelling is shared with MIPS's long_call attribute, which means the same
thing, so the two share a parse kind and therefore a spelling list; long_call
is accepted here as well.

A near pointer converts to a far one without a cast, since the near space is a
page of the far one.  The reverse needs an explicit cast, because the top
eight bits have nowhere to go.

Linking, the runtime, and starting up
------------------------------------

lld/ELF/Arch/C166.cpp resolves the relocations.  Nothing is dynamic, so it is
a getRelExpr and a relocate; the emulation is "c166elf" and the output format
name is "elf32-c166", and the OS/ABI is ELFOSABI_STANDALONE, which is what the
assembler writes.  Padding between functions is "jmpr cc_UC, -1", which
branches to itself: padding is not meant to be reached, and hanging where the
mistake happened is more use on a bare part than sliding into whatever comes
next.

The compiler-rt builtins build for c166.  Everything in GENERIC_SOURCES
already compiles, since int_types.h is written in fixed width types; what is
added is compiler-rt/lib/builtins/c166, which holds the three shifts of a 32
bit value.  A 32 bit machine gets those from its instruction set and needs
ashldi3.c and friends for 64 bit ones instead; a 16 bit machine is one size
down and needs both.  With those, the only things left undefined after linking
are memcpy, memmove, memset and memcmp, which belong to a C library.

startup/ has the reset vector, a linker script and those four functions, with
a README of its own.  None of it is built by the LLVM build - it is code for
the part, not for the machine doing the building - so it is there to be copied
into a project and adjusted to the board.

Running it
----------

llvm/utils/C166Sim is an instruction set simulator for this target, so the
backend's output can be executed and not only read.  It decodes with this
target's own MCDisassembler, which means it cannot decode an instruction
differently from the way the backend encodes it, and an instruction it does
not know stops the run and says so rather than doing something quietly wrong.

Its differential tests are the useful part: each program is compiled twice,
once for the C166 and once for the machine running the test, and the two
outputs have to match.  That covers the backend, the compiler-rt builtins, the
linker, crt0 and the simulator in one go.  The C166 side is built at every
optimisation level, because an answer that depends on one has found something
just as surely as an answer that is always wrong.

  arithmetic.c  every comparison over every pair of a set of awkward values,
                word and byte arithmetic including the overflow edges, shifts
                of every count, and 32 bit arithmetic with the libcalls under
                it
  language.c    recursion, structures by value, jump tables, varargs, 64 bit
                arithmetic, the block functions including overlapping moves,
                far objects, and a function executed from the PSRAM
  abi.c         how values get from one function to another: arguments past
                the four that go in registers, in every width and mixed so
                that a wide one straddles the boundary; structs by value at
                every awkward size and nested; calls through a pointer;
                varargs of every width and a va_list passed on; enough live
                values across a call to need the callee saved registers and
                the spill slots; mutual recursion; variable sized locals
  bits.c        bit fields signed and unsigned at widths that do and do not
                divide a byte, unions and type punning, conversions between
                every pair of integer widths, byte work through pointers,
                switches dense and sparse and on a char and over negative
                values, and the control flow a structured statement does not
                give
  floating.c    every operation on both widths over zero, one, the denormals,
                the largest and smallest normals and both infinities, with
                every comparison; what a NaN does; conversion between the two
                widths and between each and every integer type; and floating
                point through the places a wide value has to travel

The programs are not all written by hand.  generate.py emits one from a seed,
and fuzz.sh sweeps seeds: the same program compiled both ways has to print the
same thing, and a seed that disagrees is a reproducer one command long.  Two
things every generated program has to avoid, or a disagreement would be the
generator's fault rather than the compiler's - undefined behaviour, which the
two are entitled to disagree about, and integer promotion, since promotion
stops at int and int is sixteen bits here and thirty two on the host.  So all
arithmetic is unsigned and therefore wraps, every divisor has a bit forced into
it, every shift count is masked, and each operation is written at a width that
promotes the same way on both machines.

That found a compiler crash on its first sweep, in generic code rather than in
this backend: the DAG combiner's foldSelectOfBinops rebuilds a binary operation
from exactly two operands, and ADDE and SUBE - the carry opcodes this target
makes legal - take a carry in as a third.  A select between two 32 bit
additions produced an ADDE with no carry in at all.  Most targets never reach
it because they use UADDO_CARRY, whose carry is an ordinary value rather than
glue; this one is among the few that still want the older nodes, because the
carry really does live in the PSW here.

roundtrip.sh is the other standing check: what the compiler writes has to
assemble back to what it emitted directly.  Those are different code paths -
the printer and the parser against the code emitter - and -S and -save-temps
quietly depend on them agreeing.  A global named after a special function
register used to compile to "mov r2, t2" and assemble back into a load from the
T2 timer, with no relocation and no diagnostic, and nothing but this notices
that.

All of it runs in CI, which is .github/workflows/c166.yml: every workflow this
repository inherits from upstream is gated on the llvm organisation and skips
on a fork, so that one is ungated and sized for a stock runner.

Two things that suite says about the part rather than about the compiler.  A
program that uses double precision has almost no near ROM left: the soft float
builtins are about 44 KByte of the 48, so floating.c does not fit below -O2 and
the harness reports that rather than counting it as a failure.  And code that
runs from the PSRAM cannot call a compiler-rt builtin at all, since those are
in the Flash and a near call does not leave its segment - which is why
language.c's PSRAM function is written to need none.

That is also what established the condition codes.  Table 5 of the instruction
set manual, where the boolean form of each one is written down, did not survive
the extraction this backend was written against; the sixteen conditions were
taken as the conventional readings of their names and then checked by running
them.

Debug information
-----------------

An address in the debug information is four bytes and holds the whole physical
address, which is not what a pointer in the program does.  A pointer is a 16 bit
offset that the data page pointers or CSP place somewhere in the 16 MByte the
part addresses, so two bytes cannot say where anything is: on a part whose Flash
is at C0'0000H every function would be described as living in segment 0.  The
relocation is therefore ABS32 rather than the SOF16 an instruction operand
takes.  DW_AT_byte_size of a pointer type comes from the source type and is
unaffected; this is only how the debug information says where something is.

Unwinding is the harder half, because DWARF describes a frame with one canonical
frame address and a rule per register, and the return address here is in neither
of those places.  It is on the hardware stack, addressed by SP, while the CFA is
measured from R0 on the ABI stack.  So no offset from the CFA finds it and the
rule is a DWARF expression instead - and since the return address is 24 bits,
the expression assembles it out of a segment and an offset.

What the hardware stack holds at function entry depends on how the function was
entered:

  near        IP                 2 bytes, and the caller's CSP is our CSP
  far         IP, CSP            4 bytes, entered with CALLS
  interrupt   IP, CSP, PSW       6 bytes, put there by the hardware

The near case is what nearly every function looks like, so it is in the CIE and
those functions say nothing of their own; the other two override it.  So do the
handlers that save the multiply/divide unit, since those three PUSHes are on the
same hardware stack and move every offset the rules are written in terms of.

The return address column is a register called PC that no instruction can name.
DWARF needs the column to be some register number, and on this part nothing
holds the return address, so PC stands for the CSP:IP pair the expression
computes.  It has no assembly name on purpose: every register name here is a
word the assembler reserves, and this one would be reserving "pc" for something
nothing can write.

The callee saved registers are the ordinary case - they are on the ABI stack, so
they take an ordinary offset from the CFA.

None of this is checkable by reading the assembly, so it is checked by running
it: c166-sim --backtrace walks the stack with the call frame information the
executable carries, using LLVM's own reader for it, and lld/test/ELF has a test
that links a mixed near and far call chain and compares the walk against the
symbol table.

c166-sim --gdb serves the same machine over the GDB remote serial protocol, so
that something outside can drive it; llvm/utils/C166Sim/README.txt has what it
supports and how to connect.

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

The wide field is not confined to PUSH and POP.  The arithmetic and compare
instructions, and the two loading forms of MOV, have it too, so "add mdl, #1"
assembles and a startup sequence can write SP or a DPP without a register to go
through.  Those forms carry no pattern, which is what makes them safe: the
register class behind the field holds both kinds of register and is not
allocatable, so it cannot be what a pattern produces without the register
allocator being told it may leave a result in an SFR.  The general purpose
register forms keep the patterns and are marked codegen-only, which takes them
out of the matcher and the decoder, so "add r2, #1234" assembles as the wide
form and those bytes come back as it.

There are two such classes rather than one, because F0H + n names a word
register in a word instruction and a byte register in a byte one, while the
special function registers are reachable from either.  So "addb mdl, #1"
writes the low half of MDL and "add mdl, #1" writes all of it, and "addb
rl2, #200" and "add r2, #200" name different registers with the same field
value.

Where those addresses are written down is the register file itself: each
register carries its short address as its hardware encoding, and the mapping
from that to a memory address is a function rather than a second table, so what
the assembler accepts and what the disassembler prints back cannot drift apart:
"mov r2, mdl" disassembles as itself rather than as "mov r2, 65038", and still
assembles to the bytes it came from.  An address with no register at it stays
a number, and so does a bit-addressable word with nothing mapped at it, while
"bset psw.10" and "bclr mdc.0" say what they mean.

A register name is therefore not something a symbol can be called.  The parser
looks one up before it considers a symbol, and it cannot do otherwise: a symbol
may be defined after it is used, so at the point the name is read there is
nothing to consult.  "mov r2, t2" is a load from the T2 timer at FE40H, and a
program with a variable of that name meant something else.

That is why every register name, and every condition code, is in MCAsmInfo's
reserved identifiers: MCSymbol::print quotes a name that is, so the compiler
writes "mov r2, \"t2\"".  A quoted name reaches the parser as a string rather
than an identifier and can only be a symbol, which is what makes the two
spellings mean different things and what keeps the register's bare spelling
meaning the register.  Without it, compiling to assembly and assembling that
back produced a load from FE40H with no relocation and no diagnostic - a
program that went through the compiler correctly and through the assembler
broken.  X86 reserves its register names for the same reason, "call rsi" being
its version of the problem.

A symbol whose name merely starts "cc_" is still rejected rather than quoted,
since only the sixteen conditions themselves are in the set.  That is a
diagnostic and not a wrong program, which is the difference that mattered.

The extended special function registers are in the table too, but only as
addresses.  They sit at the same short addresses as the ordinary ones, mapped
from F000H and F100H instead of FE00H and FF00H, so a "reg" field cannot tell
the two apart and reaching one that way needs an EXTR the backend never emits.
By address there is no such problem - the default DPPs already cover F000H - so
"mov syscon1, r2" is a MOV mem, reg, and the registers are in a class of their
own that no "reg" field can name.

TRAP is the software entry to a vector.  It does not read a vector through the
table: it branches to the table entry itself, at 4 * the trap number, so the
entry has to hold a jump.  What it saves is what a hardware interrupt saves -
PSW, then CSP while segmentation is on, then IP - which is why RETI returns
from either.  Nothing selects it; it is there so that a handler is reachable
and so that the path through a vector can be tested.

Where the vector is comes from two registers rather than being fixed: the table
sits at the low end of the segment VECSEG names, and CPUCON1's VECSC field says
how many words apart the entries are.  Out of reset that is four bytes a vector
in whichever segment the part was started from, which is what an older C166
does unconditionally.

Known limitations / things to do
--------------------------------

* Three parts' worth of board support is here rather than one.  c167.ld and
  c167-crt0.S are the C167's, st10.ld is the ST10's, and -mmcu= picks the
  startup file while -T still picks the script, because the map is the board's
  rather than the part's and the scripts here are starting points.

  Two things make the C167 simpler rather than harder.  Its program memory is
  at the bottom of segment 0, so the data page pointers want the values they
  already hold after reset; and it has no RAM outside them, so nothing has to
  be copied or zeroed through an EXTS.  What it does not have is a PLL to
  program - the clock comes from the CLKCFG pins - which is half the length of
  the XC164CM's crt0.

  The 48 KByte a near address reaches is the same on both, because that limit
  is what the data page pointers cover rather than where the memory is.

  vectors.ld serves both parts unchanged.  A C167 has no VECSEG and no
  CPUCON1.VECSC, so its table is four bytes per vector at the bottom of segment
  0 and it has no choice about it; c167.ld's rom region starts there, so
  placing a slot at ORIGIN(rom) + 4n lands in the same place either way.
  c167-vectors.inc names that part's 56 interrupt sources and its four hardware
  traps.  Both tables pass the self-check the XC164CM's does - the location is
  four times the trap number in every row - and together they account for the
  vector space completely: 00H to 0AH are the traps, 0BH to 0FH are reserved,
  and 10H to 47H are the interrupts with none missing.

  Trap number 08H is the one slot nothing claims.  It is in the trap table
  neither as a trap nor among the reserved numbers, and it is where an XC164CM
  has a software break trap that this part does not name, so the file leaves it
  alone rather than guessing.  Its class B trap is raised by five conditions
  where the XC164CM's is raised by four: an illegal instruction access and an
  illegal external bus access are this part's own, and it has no Flash to raise
  a PMI access error.

  The C167's extension RAM is at 00'E000H, which the User's Manual gives - the
  data sheet the part table was read from has the size and not the address, and
  0800H of it is exactly the 2 KByte that sheet quotes.  Static data goes there
  and the two stacks keep the internal RAM, which is the same split c166.ld
  makes between the data SRAM and the dual-port RAM; a part with no extension
  RAM puts its static data at the bottom of the internal RAM instead, with the
  ABI stack coming down towards it.

  It is an X-peripheral, so c167-crt0.S sets SYSCON.XPEN before writing
  anything into it and before EINIT locks that register.  Whether to is the
  linker script's answer rather than the startup file's, because it depends on
  there being an extension RAM at all: the script defines __c166_enable_xper
  and the startup branches on it.  SYSCON is bit addressable, so it is one
  BSET and not a read-modify-write of a register whose other bits the reset
  pins set.

* An ST10 takes that same startup file and needs a script of its own, which is
  the opposite way round from the C167 and is what its data sheets say.  Two
  things about its memory are the part's rather than the core's.

  Its Flash is not one run.  256 KByte arrives as 32 KByte at 00'0000H, then
  nothing from 00'8000H to 01'7FFFH, then 32 KByte at 01'8000H and 64 KByte
  each at 02'0000H, 03'0000H and 04'0000H - which the ST10F269 and ST10F272
  data sheets give identically.  c167.ld computes its regions by dividing one
  length up from zero and would put code in that hole, so st10.ld has a region
  per block instead, one per segment because a far access carries one segment
  and a near branch cannot leave one.

  Its extension RAM is somewhere else, and not the same somewhere else on
  every ST10.  An ST10F269 has XRAM2's 8 KByte at 00'C000H running up to
  00'DFFFH and XRAM1's 2 KByte at 00'E000H, so the two adjoin and are 10 KByte
  in one region, all of it in page 3 and near.  An ST10F272 has the same
  XRAM1 and puts XRAM2 at 09'0000H, which no pointer here holds - so that one
  is a far region and the near RAM is 2 KByte.  Two parts, one core, one Flash
  size, two maps: that is why a part table row can name its own extension RAM
  address, and why every ST10 row does rather than leaving it to a default
  that would be the other part's.

  XPERCON is what makes the second one exist, and it is the reason that write
  is in the shared startup at all.  Its reset value is 05H - CAN1 and XRAM1 -
  so XRAM2 is off until bit 3 is set, and the data sheet says XPERCON cannot
  be changed after SYSCON.XPEN.  There is one window, before the enable, and
  c167-crt0.S uses it under a flag the script sets: an ST10 writes 0DH and a
  C167, whose one XRAM is already selected at reset, branches over it.

  That write names XPERCON by address rather than by name.  The file is
  assembled once for both cores and -mcpu=c167 does not know the name: this
  tree has the XC164CM's extended register map and the ST10F269's and no C167
  one, and a C167 does have the register.  What is missing is the map, so the
  address comes from the linker script - which is per part - rather than from
  a name asserting a map nobody has read.

  Which ST10s are rows, and which are not.  The ST10F269, ST10F272B and
  ST10F272E have their maps read and are here.  The ST10F276E and ST10F296E
  have 832 KByte and it is two Flash modules on two buses - 512 KByte of
  IFLASH and 320 KByte of XFLASH at 09'0000H - of which the data sheet says
  "the XFLASH is seen as external memory" and tabulates the modes it cannot be
  fetched from.  How much of that is program memory is a question about fetch
  modes rather than a size, so there is no row: a table whose header forbids
  inferring a map from a part number should not infer one from a headline
  figure either.

* The near addressing model is the XC164CM's: a near reference relocates as
  SOF16, the offset within a segment, and the data page pointers decide which
  page that offset lands in.  DPP0 to DPP2 cover the Flash and DPP3 the RAM and
  the register spaces, which leaves the top 16 KByte of a 64 KByte Flash with
  no near address; c166.ld stops its rom region at 48 KByte for that reason and
  the linker reports an overflow rather than wrapping.  What is left goes to a
  farrom region that .fartext and .farrodata land in, so an object declared
  __far uses it, and writable far data has the PSRAM at E0'0000H, which no data
  page pointer covers.  Code can be put there too, which is what that memory is
  actually for and what a routine that rewrites the Flash needs, since a part
  cannot fetch from the Flash while erasing it.
* MOV mem, reg keeps its narrow field, because widening it would make
  "mov mdl, mdh" match it as well as MOV reg, mem.  The two are different
  encodings of the same thing and there would be nothing to choose between
  them.
* The relocations are LLVM's own invention, like the rest of the C166 ELF
  scheme here; LLD implements them and nothing else does.
* Two far accesses to the same object share one EXTS, and two to different
  objects do not.  C166MergeExtend folds the second EXTend into the first when
  they name the same object and nothing between them touches memory - the gap
  instructions would otherwise have their own addressing redirected.  What
  makes "seg(g)" and "seg(g + 2)" the same segment is that no region in c166.ld
  crosses a segment boundary, which that script asserts rather than assumes.
  It cannot merge across a near access, and it never merges an EXTS whose
  segment comes from a register, because the register could be written in
  between.  Worth 44 bytes on the larger of the two differential programs and
  nothing at all on the other, which has no far accesses in it.
* A handler that uses the multiply/divide unit saves it whole, and one that
  calls anything at all is assumed to: there is no way to see whether the
  callee multiplies, so three words go on the hardware stack either way.
* Four of the MAC unit's instructions are selected and the other 176 are
  assembled and disassembled only.  All 180 forms are there, in
  C166InstrMAC.td, which is generated from the Format lines
  the C166S V2 manual gives each instruction rather than written out by hand -
  the same table read from the opcode list agrees with those lines on every row
  the two share.  Each one assembles to the bytes the manual specifies and
  disassembles back to itself.

  Two things it does not do.  The repeat prefix, which the manual writes
  "- USR0 CoXXX", is not accepted: it puts two tokens in front of the mnemonic
  and the matcher keys on the mnemonic being first.  rrr is therefore always
  000, the plain form.  And the simulator models only the four instructions
  that are selected, stopping and naming any of the others rather than
  executing it, which is what it does with anything it does not know.

  Table 2-9 of that manual disagrees with its own Format lines about CoSTORE:
  it puts the CoREG selector at bits 31 to 27, where the repeat field already
  is, while "B3 nn wwww:w000 rrr0:0qqq" puts it at 23 to 19.  The Format lines
  win, being self-consistent across all 180.
* The instruction forms nothing generates are assembled and disassembled but
  their encodings are derived rather than read off the page.  Each ALU group's
  columns were already fixed by the forms the compiler emits - x0 register, x2
  memory source, x6 immediate, x8 short immediate - and the ones added since
  are the opcodes those columns left free: x4 for the memory destination and
  the top half of x8 for the indirect source.  The derivation was checked the
  one way it can be from inside this tree, and it held: the set of opcodes the
  disassembler could not decode beforehand was exactly the set predicted, with
  none left over on either side.

  That is strong agreement and it is not the manual, so anyone holding V2.0
  should read llvm/test/MC/C166/rest-of-set.s first; it lists every one of
  them with its bytes.  llvm/test/tools/c166-sim/rest-of-set.s runs them, so
  what they do is checked even though what they encode to is inferred, and
  llvm/utils/C166Sim/tools/coverage.sh reports anything missing - it covers
  137 of 137 forms today.

* The branch prediction bit of JMPA and CALLA is always left at 0, which the
  C166SV2 manual defines as "assumed taken".  The prefetch hint bit of JMPA is
  always 0 too; it is meant to be set for a backward branch of 32 bytes or
  less, which is a distance that always relaxes to a JMPR here, so nothing is
  lost by it.  Branch prediction is enabled out of reset on an XC164CM
  (CPUCON1.BP), so a conditional JMPA that is usually not taken mispredicts.
* Three SFR maps are modelled and the subtarget picks between them, because
  the same short address names different registers on different derivatives.
  What they all agree about - 97 names at the same short address, which is the
  classic C166 layout of timers, A/D converter, serial channels, peripheral
  event controller, interrupt control registers and ports 1, 3 and 5 on top of
  the CPU core - is always available.  What they do not is FeatureSFRXC164,
  FeatureSFRC167 and FeatureSFRST10, which -mcpu=xc16x, -mcpu=c167 and
  -mcpu=st10 select.

  They disagree about exactly seven short addresses, and the disagreement is
  the external bus: the XC164CM has none, so it put CPUCON1, CPUCON2, SPSEG,
  VECSEG and port 9 where a C167 has ADDRSEL1, ADDRSEL2, BUSCON0, SYSCON and
  BUSCON2 to 4.  The C167 also has CAPCOM1, the PWM unit and ports 0, 2, 4, 6,
  7 and 8, which the XC164CM does not have at all.

  The encodings were never part-specific; only the names are.  So this is a
  naming layer over one instruction set rather than a second instruction set,
  and it applies in three places: the assembler refuses a name from another
  part's map, the disassembler decodes a short address to the register this
  part has there, and the printer names an address the same way.  The same
  bytes therefore read back as "mov r2, cpucon1" for one part and
  "mov r2, addrsel1" for another, which is what they mean.

  The C167 map is the C167CS's and is named for that part rather than for the
  family, for the reason the other one is the XC164CM's.  Its addresses are
  from the Keil C167CS device header, which cites the Siemens C167CS User
  Manual V1.0 of 1999-05.  The check that both were read correctly is that the
  91 names the two maps share agree on the short address of every single one.

  The ST10 map is the ST10F269's, from its datasheet's Table 31, and it is
  where the guess this paragraph used to record got settled.  An ST10 is a
  C167 derivative and it does have the C167's map: all 163 of the registers
  Table 31 gives a short address to are names this tree already had at the
  same short address, 97 of them shared and 66 the C167CS's, with no
  disagreement anywhere.  So -mcpu=st10 selects that map rather than a third
  copy of it, less the two registers the C167CS has and this part does not -
  P1DIDIS at 52H and FOCON at D5H, neither of which is in Table 31 and neither
  of whose short addresses is used for anything else there.

  FOCON had been in the always-available set, which is documented as what
  every map here agrees about; that was true of two maps and a third that
  lacks it made it false, so FOCON now sits in the two maps that have it.  A
  shared set is only worth having while it is honestly shared.

  The extended registers are a different set on each part rather than the
  XC164CM's entire, which is what they were while there was only one of them.
  The ST10F269's are the classic C166 extended space - the PWM module, CAPCOM
  timers 7 and 8, the port output and open-drain controls, and the interrupt
  control registers for CAPCOM 16 to 31 - where the XC164CM's are its real-
  time clock, chip identification and alternate-select registers.  Twelve
  names are on both parts at the same address and belong to neither map;
  three are the same hardware under two spellings and so name different
  registers at one address (50H ADDAT2 or ADC_DAT2, EDH EXISEL or EXISEL0,
  CFH XP3IC or PLL_IC), which is why the printer had to start asking the
  subtarget before naming one.  It did not have to while one map could not be
  mistaken for another.

  Reading that table also settled something the XC164CM's manual could not.
  That manual gives CC2_T7IC as F17AH/BEH and CC2_T8IC as F17CH/BFH, and each
  pair disagrees with itself - BEH is F17CH and BFH is F17EH - so both were
  left out of its map for want of anything to say which half was the typo.
  The ST10F269 datasheet prints the identical contradiction, which says the
  pairing was copied from a document both manuals descend from rather than
  mistyped twice; and here it is resolvable, because this part has PWMIC at
  F17EH/BFH, a row that agrees with itself.  BFH is therefore taken, T8IC
  cannot have it, BDH is claimed by nothing, and F176H to F17EH is an unbroken
  run for BBH to BFH.  The physical column is right and the two short
  addresses are each one slot too high.  That settles it for this part only:
  the XC164CM has no PWM module and so no PWMIC to collide with, which leaves
  BFH free there and its manual still without a tiebreaker, so CC2_T7IC and
  CC2_T8IC stay out on the evidence they were left out on.

  The ST10 Family Programming Manual is not the book for any of this - it
  gives the instruction set, which is where it settled a different question:
  the ST10's MAC is this MAC, sharing every function code, so there is one mac
  feature rather than one per derivative.
* An extended special function register is reachable by address but not
  through a "reg" field: "mov syscon1, r2" works, "push syscon1" does not.
  Getting at one that way needs an EXTR, and once encoded it is the same bytes
  as the register with the same short address in the ordinary space, so there
  would be nothing for a disassembly to go on.  Three of the XC164CM's are
  still missing because its manual contradicts itself about where they are and
  nothing in it breaks the tie; the register file names which.  Two of the
  three are the pair the ST10F269 datasheet resolves for its own map and not
  for this one, which is written down there rather than acted on here.
* The interrupt jump table cache is reachable but not used.  This part can
  hold two 24 bit pointers in FINT0ADDR/FINT0CSP and FINT1ADDR/FINT1CSP and
  branch straight to those two service routines, skipping the vector table's
  second branch entirely.  Nothing here writes them, and nothing decides which
  two interrupts would deserve them, which is a policy question rather than a
  missing encoding.
* Only the X-peripheral registers the XC164CM's own two manuals name are
  modelled.  The CAN module has a manual of its own that this was not built
  from, so its registers have to be written as addresses.
* thread_local means static here.  This part runs one thread, so per-thread
  storage and static storage are the same storage, and C166LowerThreadLocal
  takes the thread-local marker off every global that carries one before
  anything looks at a section or an address.  From there the rest of the
  compiler sees ordinary data: .data and .bss rather than .tdata and .tbss,
  STT_OBJECT rather than STT_TLS, and a GlobalAddress instruction selection
  already knows how to lower.  An access is byte for byte what a plain global
  would have produced.

  An interrupt handler is why that is the right answer rather than merely the
  cheap one.  A handler is not a thread: it runs on the interrupted code's
  stack, in the middle of that code, and has to see the same objects that code
  sees.  Emulated thread-local storage would keep the per-thread shape and pay
  a call to __emutls_get_address per access to arrive back at one object
  anyway.  A handler and main sharing one thread_local is checked by running
  it, not by reading the code.

  What this does not do is make a second thread work.  There is no thread
  library on this target and no thread pointer for an ABI to use, so a program
  that schedules tasks of its own gets one object shared between them rather
  than a diagnostic.  That is the same bargain errno already makes here.  A
  thread_local with a constructor is built on first use and its destructor is
  registered with __cxa_thread_atexit, which records nothing, so it never runs
  - which is what exit already does with atexit on this part.
* Two of the clang bugs fixed for this target were not this target's, and
  neither has been sent upstream.  A __atomic_compare_exchange whose failure
  order is not a constant built a switch whose cases were i32 over a value
  typed as the language's int, which is malformed IR on any target with a
  sixteen bit int; three lines of C reproduce it on MSP430 and AVR, and the fix
  here covers all three.  The char32_t defect is still live on MSP430, whose
  Char32Type is TargetInfo's default of unsigned int and so is sixteen bits
  there, too narrow to hold a code point.  Setting it is one line.  It was left
  alone because changing another target's type mapping means changing its ABI,
  and there is no way to run that target's tests from here.
* 105 of the 129 libc sources in string, stdlib, ctype and inttypes compile for
  this target.  Of the 24 that do not, 23 are not about this target: seventeen
  fail on an #error inside libc itself, which declares locale_t,
  __atexithandler_t and mbstate_t unavailable in overlay mode; four assume a
  thirty two bit int or a four byte wchar_t and fail on MSP430 too; and two
  want internals that exist only in a full build, the startup object's app and
  a Mutex.  wchar_t is sixteen bits here, which is a choice rather than an
  oversight - it is what the existing C166 compilers use - and libc's wcrtomb
  static_asserts that it is four.  That leaves one that is this target's:
  mkstemp, which wants an fcntl.h for a part with no filesystem.
  llvm/lib/Target/C166/startup/README.txt says what the sysroot headers are and
  why almost everything in them is declared and not defined;
  llvm/utils/C166Sim/corpus/README.txt has how that number was measured and
  what it found.
* No debugger knows the c166 architecture, so nothing puts a source level front
  end on this yet.  What is in place is everything under one: the debug
  information is right, the unwind information is right and the simulator walks
  it, and "c166-sim --gdb" serves the GDB remote serial protocol over stdin and
  stdout - registers, memory, breakpoints, stepping - with the registers
  described to the client rather than assumed.  A port of GDB or LLDB to this
  target is what is left.
* Nothing has been executed on silicon.  llvm/utils/C166Sim runs what comes
  out, and its differential tests agree with a host compiler over the whole
  language, but a simulator agreeing with itself about the manual is not the
  same as a part agreeing with both.

Memory destination arithmetic
-----------------------------

A load, an operation and a store back are one instruction when all three name
the same place, so "g |= x" on a global is four bytes rather than ten.  Add,
subtract, and, or and exclusive or are selected this way, in both widths.

ADDC and SUBC are deliberately not.  Their carry out is a result the rest of a
wide chain reads, and folding one into a store would leave that result with
nothing to say where the chain continues, so a wide add through memory stays a
pair of register operations with a store each.

The fold only applies when nothing else wants what was loaded or computed: a
result used again afterwards has to exist in a register, and a load from one
place feeding a store to another is two accesses rather than one.  A volatile
word is folded, on the same reasoning as the bit instructions - the instruction
still reads it and writes it back, in that order, and what it loses is the gap.

The indirect source form was tried and dropped.  "add Rw, [Rw]" folds the load
too, but its pointer field is two bits, so selecting it asks the register
allocator to keep every folded pointer in R0 to R3.  Measured over the
differential programs it cost 8 bytes more than it saved, so the instruction is
assembled and not selected.  Anything that revisits this should measure the
same way rather than reasoning about it.

Division
--------

One DIV answers both "a / b" and "a % b": the quotient is left in MDL and the
remainder in MDH whether or not anything wanted the second one.  A function
asking for both therefore issues one divide, not two.  The generic combiner
declines to pair them when plain division is legal, on the reasoning that a
legal divide means the ordinary expansion is fine, so the pairing is done in
the target instead.  That reasoning does not hold here: DIV is the slowest
instruction on the core, and the second answer is already sitting in a
register.  Where only one half is wanted, the move that would read the other
is not made at all.

A 32 bit division whose divisor fits in a word is two divides rather than a
call to __udivsi3.  The high word is divided on its own, which is what DIVU
does, and its remainder is then divided together with the low word, which is
what DIVLU does.  Nothing carries the remainder between them - DIVU leaves it
in MDH and DIVLU reads it from there - so only MDL is written for the second
divide, and the whole thing is one pseudo rather than two so that the register
allocator cannot put a move in between.

The reason the pair is safe is that neither divide can overflow, and that is
worth stating because it is invisible in the generated code.  The first is a
16 by 16 divide, whose quotient cannot exceed its dividend.  The second
divides r:lo by d where r is the first one's remainder, so r < d, so the
dividend is below d * 65536 and the quotient is below 65536.  DIVLU on a
dividend that did not arise this way has no such guarantee - it sets V and
leaves garbage behind - which is why a divisor that is genuinely 32 bits keeps
the library call.

What the divisor is tested for is that its high word is known to be zero, not
that it has a zero extension on it.  That is what catches the shapes a front
end produces: "a / b" written next to "a % b" leaves the divisor behind a
freeze, and a masked value never had an extension to find.  Matching the
extension alone looked correct and fired on nothing.

The high quotient is read out of MDL between the two divides, while the
divisor and the low word of the dividend are both still live, so it is an
earlyclobber.  Without that the allocator is free to hand it the divisor's
register - which it does, the second divide then dividing by the first one's
quotient.

Signed 32 bit division by a word goes through the same two divides, on
magnitudes, with the signs put back afterwards - each one negated by an
exclusive or with its sign mask and a subtract of it, so there is no branch.
Both magnitudes are representable, which is what to check before believing it
works at the edges: the dividend's is at most 2^31, a 32 bit value, and the
divisor's at most 32768, a 16 bit one.

It is not done when the function is being compiled for size.  The sign handling
is about twenty instructions on top of the seven the division takes, against
four bytes for a call, and that trade goes both ways depending on what is being
measured.  On a program doing nothing but these divisions it is 28970 states
and 808 bytes before, 3510 states and 412 bytes after - faster and smaller,
because the routine leaves the image once nothing needs it.  Over the
differential programs, where one call site is not enough to unlink anything, it
is 62 bytes more at -O2 and unchanged at -Os.

On a program doing nothing but 32 bit divisions by a word the change is 41530
states and 728 bytes before, 2536 states and 354 bytes after: the routine drops
out of the image entirely when nothing else needs it.  Over the differential
programs the size is unchanged, because the signed division there keeps
__udivsi3 linked in for its own use.

That is 16 times faster, not the 29 times the instruction counts say.  The
inline sequence is two divides, twenty states each, against a loop of two state
instructions - which is exactly the sort of thing counting instructions gets
wrong, and why the simulator counts states.

Multiply-accumulate
-------------------

"acc += a * b", with a and b words and acc 32 bits, is CoMAC on a part that
has the coprocessor.  "acc -= a * b" is CoMAC-, and an unsigned product picks
CoMACu or CoMACu- instead - four instructions behind one pseudo, whose $kind
operand says which, because they differ only in the middle instruction the
expansion emits and share one accumulator lifetime for C166MACChain to reason
about.  MUL is ten states and CoMAC is two, so this wins
even with the accumulator loaded and stored around each one: eight states
against the eighteen of a multiply, the two reads out of MDL and MDH, and the
add and the add with carry.

The accumulator never leaves the pseudo, and that is what keeps this simple.
It is one piece of state shared with everything else on the part - like MDL
and MDH, and unlike them saved by nothing - so a MAC left live across a call
or an interrupt would have to be saved somewhere.  Holding it only between the
CoLOAD and the CoSTOREs of a single accumulate means it never is.

By the time the combine runs the widening multiply is an SMUL_LOHI or a
UMUL_LOHI and the 32 bit add is the ADDC and ADDE pair that carries between the
words, so what is matched is the ADDE.  Three things have to hold: the carry
comes from the ADDC of the other half, both halves are the two results of the
same multiply, and that multiply feeds nothing else - a product wanted twice
would have to be computed twice.  An ADDE whose own carry is used is part of
something wider than 32 bits and is left alone, because the MAC produces no
carry to continue with.

A subtraction is the same shape in SUBC and SUBE.  What differs is that
subtraction does not commute, so there the product has to be the right hand
operand of both halves rather than either one: "a * b - acc" is not a
multiply-accumulate and is left as a multiply.

Which of the two multiply nodes the type legalizer built is what says whether
the product is signed.  There is no node for a mixed sign widening multiply and
the legalizer does not make one either - its check for a signed pair wants
seventeen sign bits and a value zero extended from a word has sixteen - so
CoMACsu and CoMACus have no shape here to match and are left to hand written
assembly.

Measured on a dot product of 32 elements run eight times: 9584 states without
and 7024 with, which is 1.36 times.  The difference is exactly ten states per
multiply-accumulate, 256 of them.  Written by hand with the accumulator kept
in the unit across the loop the same shape is a little over twice as fast, so
what the loads and stores cost is real: chaining consecutive MACs, and taking
the CoLOAD and CoSTOREs out of the loop, is where the rest of it is.  That is
also the change that would make the accumulator live across instructions and
so make saving it somebody's problem.

One shape it does not catch: an unrolled loop whose adds have been
reassociated no longer has a multiply feeding the accumulator, so nothing
matches.  A sum of two products could be a CoMUL followed by a CoMAC, which is
what the unit's CoMUL is for, and is not done.

Rounding a fixed point product
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

"(a * b + 0x8000) >> 16" is a fixed point product rounded to its high word,
and it is one instruction: the rounding forms add 00 0000 8000H to the
accumulator and clear MAL, so MAH alone is the answer.  Two instructions
against the six the accumulate onto a materialised constant used to be.

The shape reaching the combiner is an accumulate whose accumulator is the
constant pair 0x8000 and 0, with the low word of the sum thrown away, so it is
recognised where the rest of the multiply-accumulate matching already happens
rather than in a pass of its own.

It is exactly the C answer and not merely close, because the addend cannot
carry out of thirty two bits: a signed 16 by 16 product reaches 2^30 and an
unsigned one 2^32 - 2^17, so neither wraps once 0x8000 is added.

That argument is also what confines this to a bare product.  With a real
accumulator in play the forty bit accumulator can hold a sum that a thirty two
bit one would have wrapped, and reading MAH with no CoSTORE of MAL to truncate
through would then differ from what the program asked for - so an accumulating
round is left alone, and there is a test saying so.  The same goes for a low
word that is wanted: the instruction clears MAL, so it can only stand in where
that word is thrown away.

The simulator implements the two rounding multiplies and still refuses the
rounding accumulates, which is what it does with anything it does not model:
it stops and names the instruction rather than guessing.

The accumulator is loaded and stored the same way for all four.  CoLOAD sign
extends into a forty bit accumulator and the two CoSTOREs truncate back to
thirty two on the way out, and 2^32 divides 2^40, so what the top eight bits
hold never reaches the answer - which is why an unsigned accumulate needs no
unsigned load.

Only the XC16x has the unit.  -mcpu=xc16x is what turns it on, and defines
__C166_MAC__ so that code can ask.

Multiplying without the multiply unit
-------------------------------------

The same coprocessor does a plain multiply, and it is not close.  MUL is ten
states and leaves the answer in MDL and MDH, so a multiply is that plus a move
out of one of them and a widening multiply is that plus two: twelve states in
six bytes, or fourteen in ten.  CoMUL is two states and the answer comes out
with CoSTOREs, which is four states in eight bytes, or six in twelve.

Nothing loads the accumulator first.  CoMUL replaces what is in it rather than
adding to it, so the accumulator is dead going in, which is what keeps this to
three instructions where an accumulate needs four.

A 32 bit multiply is where it shows, because it is three of these rather than
a library call: both widening multiply nodes are legal here, so "long * long"
is expanded inline into two low multiplies and one widening one, and __mulsi3
is never reached.  Forty two states become eighteen.

Eight states for two bytes is the trade, and it is not free on a part whose
near addresses reach 48 KByte, so these stand down where a function asks to be
small and the MUL forms are selected there instead.  That is the only place in
this backend where -Os changes which instruction is chosen rather than how many
of them there are.

Two things the coprocessor leaves alone that MUL does not.  PSW is untouched,
so a comparison can survive a multiply - which matters here, where nearly every
instruction writes the zero and negative flags and the fused compare-and-branch
pseudos exist because of it.  And the multiply/divide unit is untouched, which
is state the C166 interrupts MUL and DIV part way through and that a handler
reaching it therefore has to save and put back.

Compares the flags already answer
---------------------------------

Nearly every arithmetic and logical instruction sets Z and N from the value it
produced, so "and r6, #16" followed by "cmp r6, #0" asks a question the AND has
already answered.  C166FoldCompare removes the compare.  Over the differential
programs that is 91 of them, 182 bytes, and 91 instructions that no longer run.

It is a pass rather than the usual optimizeCompareInstr() hook because of when
the compare exists.  Until the post register allocation expansion a conditional
branch is a single BRCC pseudo carrying both the comparison and the condition;
the compare only becomes an instruction of its own when that pseudo is split,
which is after the peephole optimiser has run and the hook would have been
called.

The condition has to read Z and N and nothing else.  A compare against zero
also sets C and V, to zero both times, and the instruction that produced the
value will generally have left something else there - a shift leaves the last
bit shifted out in C, an add leaves its carry.  cc_SGT after a subtract is the
case that rules out, and it occurs.

The instruction that set the flags has to be the one immediately before the
compare.  The obvious alternative - walk back while nothing writes PSW - is
wrong here, and quietly.  MOV is deliberately not modelled as writing PSW,
because it leaves C alone and that is what lets a carry survive the register
shuffling around a wide addition; but it does clobber Z and N on the part.  So
the machine description's account of what touches the flags is not the part's,
and a gap that looks empty can hold a move that has already destroyed the
answer.  That is not a theory: the version that walked back removed half as
many compares again and turned two of the differential programs into infinite
loops.

Adjacency is not free, and what it costs was measured rather than guessed: of
the compares it gives up, the gap is a move in every case but one.  A move is
exactly what cannot be stepped over, so no rule about what preserves the flags
reaches them - only reordering would, by lifting the move above the instruction
that set the flags, and 46 compares is not enough to justify moving
instructions around after register allocation.

Looking at the moves did pay, though, in the other direction.  A move sets Z
and N from the value it moved, so a move that writes the register being
compared answers the question itself - and the list of which forms do that is
taken from the simulator, which implements the flag behaviour, rather than from
the machine description, which does not model it.  Two near misses came out of
that check: the pre-decrement stores set no flags at all, and ADDC and SUBC set
Z only if it was already set, so that a wide value tests as zero exactly when
every word of it did.  Neither is in the list.

Scheduling
~~~~~~~~~~

There is still no scheduling model, but the reason has changed.  It used to be
that nothing here counted anything but instructions, which is the wrong unit
for a question about latency; the simulator now counts states, so the question
can be asked.  What is missing is the second half: a scheduling model describes
which instructions can issue together and where the stalls are, and the state
counts below are a per-instruction cost, not a pipeline.  Writing one would
still be writing numbers with nothing to check them against.

Counting leading zeroes
-----------------------

PRIOR reports how far the leftmost set bit is from the top, which is what
CTLZ_ZERO_POISON asks for, so that is the instruction on its own.  PRIOR leaves
zero behind for a source of zero where CTLZ wants sixteen, so the full one is
that instruction and a test for zero - still most of the twenty five
instruction bit smear and population count it used to be.

Atomics
-------

Atomics up to a word are instructions rather than library calls.  A byte, or a
word at an even address, crosses the bus in one cycle, so an atomic load or
store of one is already indivisible and is the ordinary MOV; the ordering asked
for needs nothing either, because this is one core with no store buffer and no
cache, so what an interrupt handler sees is whatever the last instruction to
retire left behind.

Changing a word is not indivisible, and ATOMIC is what makes it so: it holds
interrupts and PEC transfers off for the next 1 to 4 instructions, which is
exactly long enough to read a word, copy it so the old value survives, change
it, and write it back.  Add, subtract, and, or, xor and exchange fit; an
exchange needs no copy and no arithmetic, so it is two instructions rather than
four.  NAND is an AND and a complement, and the minimum and maximum need a
comparison and a choice, so those go round a compare and exchange loop instead,
which is longer per turn but has no count to overrun.

The sequences stay one instruction until after register allocation.  ATOMIC
counts instructions rather than marking a region, so a spill dropped into the
middle would not merely lengthen the sequence, it would push the write out from
under the count and leave it interruptible with nothing to say so.

A compare and exchange stores only when what it read matched, so it branches,
and a branch is one of the two things a sequence must not contain.  That one
clears PSW.IEN instead and puts the whole word back afterwards, which restores
the bit along with flags that are dead by then - the only reader was the branch
above.  Having no counter is also why that one is built before register
allocation: a spill landing inside only makes the window longer.

Caveats the hardware imposes
~~~~~~~~~~~~~~~~~~~~~~~~~~~~

The single instruction counter is shared with the EXTend instructions, and it
keeps counting whatever runs next.  So a sequence must contain nothing that
changes the flow - the rest of the count goes with it, protecting instructions
nobody meant to protect and leaving the ones that needed it uncovered - and
nothing that extends again, which overwrites the count still in use.

That is a rule the compiler has to remember, so it is held to it: the simulator
stops with an error if a sequence ever reaches either kind of instruction, and
every program the differential suite runs goes through that check.

An atomic on a far pointer is refused with a diagnostic.  Reaching one needs an
EXTend, and an ATOMIC sequence has no counter left to give it.
