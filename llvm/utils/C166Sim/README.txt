c166-sim
========

An instruction set simulator for the C166, so that what the backend emits can
be executed and not only looked at.  Everything else about this target is
checked by reading the bytes that come out; this is what checks that a machine
would do the right thing with them.

Decoding is done by the target's own MCDisassembler.  That has two
consequences worth knowing: the simulator cannot decode an instruction
differently from the way the backend encodes it, and it can only run what the
backend models.  An instruction it does not know stops the run and says which
one it was, rather than doing something quietly wrong.

Running a program
-----------------

  c166-sim hello.elf

The program is an ELF executable for the C166, loaded at the physical
addresses of its PT_LOAD segments - which is what puts .data where it is
stored rather than where it runs - and entered at e_entry.  It finishes when
execution reaches __c166_exit, which is where crt0.S parks a returned-from
main(); the exit code is R2, where the ABI returns a word.  --exit-symbol
names a different symbol.

  --trace            print each instruction as it executes
  --dump-state       print the registers when the program stops
  --backtrace        walk the stack when the program stops
  --max-steps        give up after this many instructions
  --interrupt-at     raise an interrupt request once, at a state count
  --interrupt-every  raise one every that many states
  --mcpu             the part, which is what the decoder reads registers for
  --mattr            what that part has on top of its core, e.g. +mac

--backtrace is the one that is a check rather than a convenience.  It walks the
stack using the call frame information in the executable, which on this target
is the only way to find out whether that information is right: the return
address is on the hardware stack rather than at any offset from the canonical
frame address, so the rule that finds it is a DWARF expression, and an
expression that is wrong is not something reading the assembly would show.
Stopping somewhere with a call chain under it - --exit-symbol names a leaf, for
instance - and looking at the walk is what tests it.

  $ c166-sim --exit-symbol=leaf --backtrace prog.elf
  #0 c00100 in leaf at prog.c:2
  #1 c0c006 in farmid at prog.c:3
  #2 c0010e in outer at prog.c:4

The tests
---------

differential/ holds the programs that get compiled twice - once for the C166
and run here, once for the host and run natively - with the two outputs
compared.  It is what checks the backend, the builtins, the linker, crt0 and
this simulator together, which is as close to running on the part as anything
here gets.

  mksysroot.sh   builds crt0.o, libc.a, the headers and the compiler-rt
                 builtins, which a program has to compile and link against and
                 the LLVM build does not make
  run.sh         the hand written programs, at every optimisation level
  generate.py    emits a program from a seed
  fuzz.sh        sweeps seeds through the same comparison
  roundtrip.sh   checks that what the compiler writes assembles back to the
                 bytes it emitted directly

All of it runs in CI - .github/workflows/c166.yml - and all of it can be run by
hand, which is what the arguments to each script are for.  A seed that
disagrees is reproduced with one command:

  generate.py --seed 110 > seed110.c

and made smaller with --depth, --statements and --functions.

Debugging a program
-------------------

  c166-sim --gdb prog.elf

speaks the GDB remote serial protocol on stdin and stdout, so the machine is
driven from outside instead of running to completion.  A debugger connects to
it with

  target remote | c166-sim --gdb prog.elf

which needs no port and leaves nothing listening if the debugger goes away.
What is supported is enough to look at a stopped machine and let it go again:
reading and writing registers and memory, software breakpoints, stepping,
continuing, and an interrupt while running.  Breakpoints are the simulator's
own - nothing is written into the program - which is something a simulator can
do and a part cannot.

The registers are described to the client rather than assumed, through
qXfer:features:read, and the numbers in that description are the DWARF ones
from C166RegisterInfo.td.  So there is one register numbering across the
assembler, the debug information and this.  PC is not a register any
instruction can name: it is the 24 bit CSP:IP pair, reported as one 32 bit
register because that is what an address in the debug information is.

No debugger knows the C166 architecture yet, so nothing can put a source level
front end on this today; a port of GDB or LLDB to this target is what that
would take.  The protocol itself is not architecture specific, though, so an
ordinary remote client works, and tools/rsp-client.py is one - it is what the
tests drive the stub with, and it prints what came back:

  $ rsp-client.py -- c166-sim --gdb prog.elf <<'END'
  send Z0,c00100,2
  send c
  reg pc
  END
  Z0,c00100,2 -> OK
  c -> S05
  pc = 00c00100

Two addresses are the simulator rather than storage.  They are in the top
segment, which no C166 part populates and no linker script here places
anything in, so a program reaches them with a far pointer or an EXTS:

  FF'0000H   a byte written here appears on stdout
  FF'0002H   a word written here stops the program with that value

The exit port is what lets a test be a handful of instructions with no symbols
and nothing to link:

  c166-sim --binary program.bin

loads a flat image at --load-address and runs it from there.  That is how the
tests in llvm/test/tools/c166-sim work, so they need only llvm-mc and
llvm-objcopy.

What is modelled
----------------

The core CPU: the general purpose registers as the window into internal RAM
that they are, the condition flags, the multiply/divide unit, the system stack
with its STKOV and STKUN bounds, the DPP window and the EXTP/EXTS overrides
that replace it, and bit addressing.

Because the registers really are that window, moving CP moves them, which is
what a __attribute__((c166_bank)) handler does on the way in and undoes on the
way out.  A context pointer that does not name the internal RAM is refused: on
the part the window would read whatever that memory is, and here it would read
this simulator's own array and quietly work, which is worse - a bank placed
outside the internal RAM by a linker script with no banks region is exactly
the mistake nothing else catches.

101 of the MAC unit's 180 forms run: the 89 the manual marks repeatable,
which the repeat prefix runs as many times as its count says, and twelve
register forms on top of them - CoLOAD and CoSTORE, CoMUL and CoMAC with
their unsigned and negated variants, the two rounding forms of CoMUL, and
CoMIN and CoMAX.  The accumulator is held as a 40 bit signed value, MAL and
MAH being its low two words.  MCW resets to zero, so the product shift and the saturation are both
off and this is plain signed integer multiply-accumulate; a program that wants
either has to ask, and nothing here asks.  The other 79 forms stop the
simulator by name, as any unimplemented instruction does, rather than being a
quiet wrong answer.

Which part is being simulated matters and --mcpu says: the same short address
is a different special function register on different derivatives, and two of
the ones modelled here exist only on the XC16x.  --mattr says what the part has
on top of its core, in the compiler's spelling - the coprocessor is a feature
rather than a core, so an ST10 that has one is "--mcpu=st10 --mattr=+mac" here
exactly as it is "-mcpu=st10 -mmac" there, and without it the decoder refuses
the instructions the compiler just emitted for that part.  FF12H is its VECSEG, which
says where the interrupt vector table is, and a C167's SYSCON, which that
part's crt0 writes on the way up; FE18H is its CPUCON1, whose VECSC field sets
the space between slots.  On a part without them the table is four bytes a slot
at the bottom of segment 0, which is what both registers say out of reset, so
nothing else changes.

Not modelled: the peripherals, and the extended SFR space that EXTR selects.
Of the MAC unit, MSW's flags and guard bits, the limiter and the shifter are
not modelled either.  A peripheral register reads back what was written to it,
which is enough to run code that configures a peripheral it then never waits
on.  EXTR is accepted and counted so that the length of an EXTend sequence
stays right, but the SFR space it selects is not switched.

Interrupts
----------

There are no peripherals, so there is nothing to raise a request on its own.
A source is declared on the command line instead and fires on the clock:

  --interrupt-at=<states>:<vector>[:<level>]
  --interrupt-every=<states>:<vector>[:<level>]

The vector is the entry in the vector table the CPU branches to, 0 to 127; the
level is the source's priority, 1 to 15, and defaults to 15.  Either option
may be given more than once, so a program can have several sources at
different priorities.  Firing on a state count rather than on anything the
program does is what makes a test of this reproducible.

What the core does with the request is modelled, and that is the part worth
having.  The request is raised when its time comes and stays raised until it
is accepted - "if the requesting source has a lower (or equal) interrupt
priority than the current CPU task, it remains pending" (C166S V2 Architecture
Overview Handbook, section 7.2), so a request that arrives while interrupts
are off is not lost.  The highest priority pending request wins arbitration,
and is accepted when PSW.IEN is set and its level is strictly above PSW.ILVL.
Entry saves PSW, CSP and IP on the system stack and branches to the vector
table entry, which is exactly what TRAP does - they share the code - and then
sets PSW.ILVL to the accepted source's level, which is what stops a handler
being re-entered by its own level or below.  RETI puts back all three, and
with PSW both the level and the condition flags.  An ATOMIC or an EXTend locks
the request out for the instructions it covers, which is what ATOMIC is for.

Two things are deliberately absent.  The source's own enable bit is one: it
lives in a peripheral control register that does not exist here, and which
register belongs to which vector is a property of the derivative rather than
of the core, so an injected request is by definition enabled and PSW.IEN and
the priority are what gate it.  The PEC is the other - a request here is
always serviced by the CPU.

Entering a handler costs four states, TRAP's figure for the same work, and is
not counted as an instruction.  The real response time is longer, because of
arbitration and the pipeline being thrown away, in the same way and for the
same reason as the other lower bounds in the state count below.

test/tools/c166-sim/interrupt.s is the check.  Its last case is the one that
matters most: a hundred rounds of arithmetic ending in a conditional branch,
run against four unrelated interrupt rhythms and once against none, all of
which have to give the same answer - which they only can if the condition
flags survive an interrupt landing between the SUB and the JMPR that reads
them.

Counting time
-------------

--count-states prints how long the program took, in states.  One state is one
CPU clock period, which is the unit the instruction set manual counts in, and
the figures are its own: Table 11 for what each instruction costs and section
7.3 for what gets added.  Most instructions are two states, a multiply is ten
and a divide twenty, the branches are four when they branch and two when they
do not, and six states are added once for filling the pipeline.

It counts a program executing from the internal program memory, which is where
the linker script here puts .text.  Running from RAM or through the external
bus controller costs more, by an amount the manual gives in ALE cycle times -
which depend on the bus mode and the programmed waitstates, and so are a fact
about a board rather than about a program.  That is why none of it is here.

Two of the additions in section 7.3 are charged: a conditional branch pays a
state when the instruction before it wrote PSW, and reading PSW as an operand
pays two.  The rest are not, each for a reason written down in Execute.cpp
next to the ones that are.  So a state count is a lower bound - exact for
straight line code in Flash, and optimistic by a state here and there
elsewhere.

It is worth having because instruction counts mislead in the direction that
flatters.  Inlining a 32 bit division by a word is 29 times fewer instructions
than the library call and 16 times fewer states, because what replaced the
call is two divides at twenty states each while the loop it replaced was
almost all two state instructions.

test/tools/c166-sim/states.s is the check on all of this: a short program
whose count is worked out by hand from Table 11 in the comments, so that the
simulator is measured against the document rather than against itself.

Where the semantics come from
-----------------------------

The MAC unit's are the C166S V2 User's Manual's, from the detailed description
of each instruction:

  CoLOAD Rwn, Rwm   ACC <- sign extended (Rwm || Rwn), Rwn being the low word
  CoMUL  Rwn, Rwm   ACC <- the signed 32 bit product, sign extended to 40
  CoMAC  Rwn, Rwm   ACC <- ACC + that product
  CoSTORE Rwn, creg the named MAC register into Rwn

What that is worth, measured rather than assumed: a sixteen element dot
product written both ways is 488 states through MUL and 236 through CoMAC, a
little over twice as fast.  The win is per multiply-accumulate rather than per
loop - MUL alone is ten states and CoMAC is two, so even one "acc += a * b"
with the accumulator loaded and stored around it is eight states against
eighteen.  Nothing selects any of this yet; the point of the number is that
it says what selecting it would be worth.

with the product one-bit left shifted first when MCW.MP is set, which it is
not at reset.  Their state times come from the same handbook's instruction set
summary, which counts cycles: each of the four is one cycle and the rounding
forms are two.  One cycle is two states - that summary gives "MOV mem, reg" as
4 bytes and 1 cycle where Table 11 gives it two states - so a MAC instruction
costs what an ordinary instruction does.  MCW.MP is bit 10 and MCW.MS, the saturation control, is bit 9;
the register resets to 0000H.

Reading that rather than assuming it is what stopped CoMAC being implemented
as the rounding form: the manual gives them separate opcodes and separate
descriptions, and the rounding one adds 00 0000 8000H to the accumulator and
clears MAL afterwards.  Selecting that for an integer dot product would have
added 32768 to every term.

The rest are the C166 Family Instruction Set Manual, V2.0, 2001-03.  Three things in it are
not what a reader used to other machines would assume, and all three are
commented at the point they are implemented:

 * SUB, SUBC, CMP and NEG set C when a borrow is generated, so C is set
   exactly when an unsigned subtraction wrapped.  It is not an inverted borrow.
 * ADDC and SUBC set Z only if the result is zero and Z was already set, which
   is how a multi-word sequence accumulates one zero test.
 * BAND, BOR, BXOR and BCMP report the two bits they read rather than the
   result: Z is their NOR, V their OR, C their AND and N their XOR.

One thing did not survive the extraction this was written against: Table 5,
which is where the boolean form of each condition code is written down.  The
sixteen conditions here are the conventional readings of their names, and they
agree with what C166ISelLowering.cpp selects for each comparison, but they were
not read off the page.  differential/arithmetic.c exercises all sixteen over
every pair of a set of awkward values, which is what actually establishes them.

Testing it
----------

llvm/test/tools/c166-sim has the simulator's own tests: a flat image, the three
flag behaviours above, the addressing overrides, the console, and the ways a
run can fail.  They need no linker.

differential/ is worth more.  Each program there is compiled twice, once for
the C166 and once for the machine running the test, and the two outputs have to
match.  That tests the backend, the compiler-rt builtins, the linker, crt0 and
the simulator at once, and it is how the condition codes and the flag
semantics were established rather than assumed.  It is not a lit test because
it needs a C166 crt0 and a C166 compiler-rt, neither of which the LLVM build
produces; llvm/lib/Target/C166/startup/README.txt says how to build them, and
then:

  llvm/utils/C166Sim/differential/run.sh <build>/bin <sysroot> <linker script>

corpus/ is the same comparison over code nobody here wrote: drivers over LLVM's
own libc, which reaches shapes the hand written programs do not because nobody
chose them.  It has its own README; what it is for is finding the things a test
written against this backend would not think to look for.

  llvm/utils/C166Sim/corpus/run.sh <build>/bin <sysroot> <linker script>
