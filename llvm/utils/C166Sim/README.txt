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

  --trace        print each instruction as it executes
  --dump-state   print the registers when the program stops
  --backtrace    walk the stack when the program stops
  --max-steps    give up after this many instructions

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

  mksysroot.sh   builds crt0.o, libc.a and the compiler-rt builtins, which a
                 program has to link against and the LLVM build does not make
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

Four of the MAC unit's instructions run: CoLOAD, CoMUL, CoMAC and CoSTORE,
which is what a multiply-accumulate needs and no more.  The accumulator is
held as a 40 bit signed value, MAL and MAH being its low two words.  MCW
resets to zero, so the product shift and the saturation are both off and this
is plain signed integer multiply-accumulate; a program that wants either has
to ask, and nothing here asks.  The other 176 forms stop the simulator by
name, as any unimplemented instruction does - including the rounding variants
of the four above, which are separate opcodes that add 00 0000 8000H and clear
MAL, and would otherwise be a quiet wrong answer rather than a loud one.

Not modelled: interrupts and traps, the peripherals, and the extended
SFR space that EXTR selects.  Of the MAC unit, MSW's flags and guard bits, the
repeat prefix, the limiter and the shifter are not modelled either.  A peripheral register reads back what was
written to it, which is enough to run code that configures a peripheral it
then never waits on.  ATOMIC and EXTR are accepted and counted so that the
length of an EXTend sequence stays right, but they lock nothing, because there
is nothing to lock.

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
