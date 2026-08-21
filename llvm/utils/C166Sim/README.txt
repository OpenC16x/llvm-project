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
  --max-steps    give up after this many instructions

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

Not modelled: interrupts and traps, the peripherals, timing, and the extended
SFR space that EXTR selects.  A peripheral register reads back what was
written to it, which is enough to run code that configures a peripheral it
then never waits on.  ATOMIC and EXTR are accepted and counted so that the
length of an EXTend sequence stays right, but they lock nothing, because there
is nothing to lock.

Where the semantics come from
-----------------------------

The C166 Family Instruction Set Manual, V2.0, 2001-03.  Three things in it are
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
