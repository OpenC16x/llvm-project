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
qXfer:features:read.  There are two numberings in that description and they are
different things, which is worth saying because getting it wrong here was a bug
that lasted until a real debugger arrived: regnum is the protocol's own number,
which the protocol defines as the position in the "g" packet, and dwarf_regnum
is the number the unwind information is written in, from C166RegisterInfo.td.
A client that reads the description uses the first; a client that cannot -
LLDB has no XML parser unless it is built with one - counts "g" packet
positions, which is the same numbering.  Advertising the DWARF numbers as
regnum made those two disagree above R15, and nothing noticed, because
rsp-client.py reads whole register dumps and never asks for one register.
llvm/test/tools/c166-sim/gdb-stub.test asks.

PC is not a register any instruction can name: it is the 24 bit CSP:IP pair,
reported as one 32 bit register because that is what an address in the debug
information is.

"target remote |" is GDB's spelling, and not every debugger has a pipe form -
LLDB has none.  So the stub will listen on a socket instead:

  $ c166-sim --gdb-port=0 prog.elf &
  listening on port 43505

A port of zero asks the system for a free one and prints the number it got,
which is what a script should use: guessing a port races whatever else wanted
it.  The stdin and stdout form above is still the default and is still what the
tests use, because it needs no port and leaves nothing listening if the
debugger goes away.

LLDB knows the C166 now - that is what the ArchSpec entry, the register
fallback and the generic register mapping under lldb/ are for - so it will
connect to the port above and debug:

  $ lldb prog.elf -o "gdb-remote 43505" -o "breakpoint set -n add" -o run

A breakpoint set by name, the source line and column at the stop, the argument
registers, disassembly with the current instruction marked, stepping, a
backtrace across both stacks, and locals in any frame all work:

  (lldb) bt
  * thread #1, stop reason = breakpoint 1.1
    * frame #0: 0x00c0014e dbg.elf`leaf(a=15, b=7) at dbg.c:3:13
      frame #1: 0x00c0019c dbg.elf`middle(n=5) at dbg.c:9:10
      frame #2: 0x00c001b0 dbg.elf`main at dbg.c:12:11
  (lldb) frame select 1
  (lldb) frame variable
  (int) n = 5
  (int) local = 15

Three things had to be true for that, and none of them was.

  The register list this stub serves had to carry the DWARF numbers.  Without
  them a local, which is described as an offset from DWARF register 0, is
  "unable to convert register kind=1 reg_num=0 to a native register number" -
  the registers could be printed and none of them could be found.

  The call frame information had to say that the caller's R0 is the canonical
  frame address.  On a machine with one stack nothing says that: the CFA is
  where the stack pointer was, so restoring the stack pointer restores it.
  Here the return address is on the other stack and R0 is a general purpose
  register, so a reader has no reason to believe it is a stack pointer at all.
  The rule is now in the CIE - llvm/test/CodeGen/C166/cfi-two-stacks.ll has it
  - and without it the walk got the caller's program counter, could not work
  out the caller's R0, and stopped one frame up with its locals unreadable.

  And LLDB had to be able to read that rule out of a CIE, which it could not:
  DW_CFA_val_offset was handled in the code that walks a frame description
  entry and not in the code both share, and a CIE stops at the first opcode
  that is not handled - so the rule was not merely ignored, it took the two
  expressions after it with it.

"sp" means the register that holds return addresses on this part and it also
means what a frame is measured from, and those are two different registers.
LLDB's generic stack pointer has to be R0, because that is what a frame is
measured from, so "register read sp" answers with R0.  The hardware one is
reachable as "syssp":

  (lldb) register read sp syssp
    r0 = 0xf9f0
    sp = 0xfbfa

A far pointer reads as one now too:

  (lldb) frame variable
  (const __far char *) fp = 0x00c0c000 "far string"
  (__far int *) ip = 0x00e00000
  (const char *) np = 0x01ce

It used to print 0xc000 for the first of those - two bytes of a four byte
pointer - because nothing said how wide it was.  The debug information says
now: clang gives such a pointer a DW_AT_address_class, whose values are the
target's to choose, and LLDB reads it back through the same target and builds
the pointer into that address space, which is what decides its width in the
type system.  Both halves of that mapping are one target hook,
getDWARFAddressSpace() and getAddressSpaceFromDWARFAddressClass(), so a target
that emits no address classes is unaffected and one that emits its own is
asked rather than assumed.

Dereferencing one in an expression works now as well, through either width,
and so does writing through it:

  (lldb) p *nip
  (int) 22136
  (lldb) p *ip
  (__attribute__((address_space(1))) int) 4660
  (lldb) p fp->x
  (__attribute__((address_space(1))) int) 11
  (lldb) expr *nip = 99

Two things were wrong and both were about a width.  The expression evaluator
runs the compiled expression over a map of made up memory, and a pointer
stored in that memory was written and read back as the architecture's address
size - four bytes here, because a far address is 24 bits - while the slot the
module's data layout had reserved for it was two.  So each pointer was read
four bytes wide out of a two byte slot, and consecutive slots overlapped.
IRInterpreter now says how wide the pointer it is reading or writing actually
is, which it can, because it has the data layout of the module it is running.

The second is where that made up memory sits.  The map put it at 0xEE000000,
which is where a target with four byte addresses puts it, and no near pointer
can hold that: an alloca's result and the address of the materialized struct
are both near pointers here.  A map is now told how wide a pointer that has to
hold one of its own addresses is - IRExecutionUnit reads it off the module -
and with two bytes it allocates from 0x8000 and keeps the frame it interprets
on to 512 bytes rather than half a megabyte, which is what the one other
16 bit target in LLDB asks its ABI plugin for.

What still does not work is naming a __far global directly - "p fpt", or even
"target variable fpt", answers "unimplemented opcode DW_OP_xderef".  That is
not about pointer widths and not about the expression path: clang describes
such a global with a location expression that ends in DW_OP_xderef, which is
DWARF's "load through an address in this address space", and LLDB decodes that
opcode without evaluating it.  Everything reached through a pointer, which is
what a program mostly does, works; the global by name does not.

lldb/unittests/Expression/IRMemoryMapTest.cpp covers the allocation half.  The
rest of it is not a lit test, and the reason is worth writing down rather than
leaving to be rediscovered.  The workflow that runs these tests does not build
LLDB, and LLDB's own test suite has no C166 toolchain to build a program with,
so a test there would be a test nothing runs.  What is tested is the half that
can be - the stub - and gdb-stub.test beside the others drives it with
rsp-client.py.

The protocol is not architecture specific, so an ordinary remote client works
too, and tools/rsp-client.py is one - it is what the tests drive the stub with,
and it prints what came back:

  $ rsp-client.py -- c166-sim --gdb prog.elf <<'END'
  send Z0,c00100,2
  send c
  reg pc
  END
  Z0,c00100,2 -> OK
  c -> S05
  pc = 00c00100

EXTR is modelled, which the others are not quite the same as.  EXTS and EXTP
replace the data page a "mem" operand goes through; EXTR replaces the base of
the register area, so for the next few instructions a "reg" field names the
register at F000H rather than the one at FE00H with the same short address.
Nothing else distinguishes the two - the short address is the same number
either way - so a simulator that did not model it would push the wrong register
and say nothing.  It is one flag beside the EXTend kind and one line in
regFieldAddress(); llvm/test/tools/c166-sim/esfr.s is what checks it.

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

The particular pipeline effects of section 4.2 are checked and modelled; see
Pipeline effects below.  One peripheral is modelled: GPT1, the block of three
timers, which is under Interrupts below.  Not modelled: every other peripheral, and the extended SFR
space that EXTR selects.  Of the MAC unit, MSW's flags and guard bits, the
limiter and the shifter are not modelled either.  A peripheral register that
is not GPT1's reads back what was written to it, which is enough to run code
that configures a peripheral it then never waits on.  EXTR is accepted and
counted so that the length of an EXTend sequence stays right, but the SFR
space it selects is not switched.

Interrupts
----------

A request reaches the CPU from one of two places.

The GPT1 timers raise one the way the part does.  A program writes T2CON,
T3CON or T4CON to start a timer, the timer counts on the clock, and an overflow
or underflow sets the request flag in its own interrupt control register -
T2IC, T3IC or T4IC, at bit 7, with the enable at bit 6, the group level at 5-4
and the priority at 3-0.  Nothing on the command line takes part, and a
program written for the part is written the same way here.  What GPT1 does and
does not do is below.

Everything else in the vector table is declared on the command line instead
and fires on the clock:

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
with a tie going to the higher group level, and is accepted when PSW.IEN is
set and its level is strictly above PSW.ILVL.  A source's request flag is
cleared on entry, which is what the manual says happens and is why a handler
does not have to clear one itself.
Entry saves PSW, CSP and IP on the system stack and branches to the vector
table entry, which is exactly what TRAP does - they share the code - and then
sets PSW.ILVL to the accepted source's level, which is what stops a handler
being re-entered by its own level or below.  RETI puts back all three, and
with PSW both the level and the condition flags.  An ATOMIC or an EXTend locks
the request out for the instructions it covers, which is what ATOMIC is for.

One thing is deliberately absent.  An injected source has no enable bit and no
group level: both are fields of a control register, and a source declared on
the command line has no control register to put them in, so such a request is
by definition enabled, arbitrates as group 0, and is gated by PSW.IEN and the
priority alone.  A timer's request has all of them, because it has the
register.

The Peripheral Event Controller
-------------------------------

A request that wins arbitration does not always reach a handler.  The PEC is
this part's answer to DMA: eight channels, each of which moves one byte or word
per request, counts down, and only lets the request through when the count runs
out.  Chapter 5 of the C167CR Derivatives User's Manual V3.1 is what is
modelled, and all of it that a program can observe is:

  - which channel a source asks for, which is not in the channel's own register
    but in the source's.  "Interrupt requests that are programmed to priority
    levels 15 or 14 will be serviced by the PEC", and "the associated PEC
    channel number is derived from the respective ILVL (LSB) and GLVL" - so
    level 15 reaches channels 7 to 4 and level 14 channels 3 to 0, with the
    group level choosing among the four.  Level 13 and below have no channel.
    The arbitration needs no change for any of this: the channel number is
    built from the two fields the round already compared, in the same order, so
    the winner by level and group is the winner by channel.

  - PECCx at FEC0H and one every two bytes after it, with COUNT in the low
    byte, BWT at bit 8 and INC at bits 10-9.  The reserved INC combination 11
    behaves as 10, which is what the manual says hardware does with it; the
    register is left holding what was written, because the manual says the
    combination is changed but not when.

  - SRCPx and DSTPx, which are not registers: "these pointers do not reside in
    specific SFRs, but are mapped into the internal RAM", at FCE0H + 4x and two
    bytes above.  A program that uses a channel owns those words.

  - Table 5-5's four rows for COUNT, which needs all four.  FFH is continuous
    and is not decremented; FEH to 02H transfer and count down; 01H transfers,
    reaches zero and leaves the request flag set, "which triggers another
    request", so the handler runs straight after and a program is told the
    block is finished; and 00H is not a PEC request at all - the handler runs
    instead.

A transfer costs two states, which is what the injected transfer "instruction"
of section 5.6 costs, and how many happened is printed beside the interrupt
count under --count-states.

One thing about it catches people and is worth knowing before it does.  A
channel moves "between two locations in segment 0 (data pages 3 ... 0)", so
each pointer is a physical address in that segment rather than a near address
through a data page pointer.  On a part whose program memory is at C0'0000H
that puts read-only data out of the controller's reach entirely: a const array
has a near address the PEC will happily use and find nothing at.  Anything a
channel touches has to be in RAM, which is why differential/pec.c's string is
not const and says so where a reader will be looking for it.

Pipeline effects
----------------

Section 4.2 of the C167CR Derivatives User's Manual V3.1 names four places
where "the circumstance that the C167CR is a pipelined machine requires
attention by the programmer".  Nothing else about the pipeline is modelled -
there are no stages here and every instruction retires whole - but these four
are, because each of them changes what a correct program may be written as.

Three are a rule about what not to write, and those are checked rather than
emulated:

  - an instruction that reaches a general purpose register must not be the one
    immediately after an instruction that wrote CP, or it addresses the old
    register bank;
  - a long or indirect address must not go through a DPPn the instruction
    immediately before wrote, or it addresses the old page;
  - RET, RETI, RETS, RETP and POP must not be the instruction immediately after
    an explicit write to SP.  A write to the stack is exempt, because the
    manual says so: "conflicts with instructions writing to the stack (PUSH,
    CALL, SCXT) are solved internally by the CPU logic".

A program that writes one of these stops with the manual's own sentence.
Checking rather than emulating is the point: the part does not report any of
them, it uses the value the pipeline still holds and carries on, so the wrong
answer a program gets there is not one this could reproduce - "mostly not
capable of using a new CP value" does not say which cases "mostly" leaves out.
What can be checked exactly is the sequence the manual says must not appear.

The fourth is behaviour rather than a rule, so it is modelled: a change to
PSW.IEN or PSW.ILVL is not arbitrated on until one instruction later, because
"the current interrupt prioritization round does not consider these changes",
and the same delay applies to enabling as to disabling.  That is why the
compare-and-exchange sequence the compiler emits has a NOP between clearing
IEN and the load it protects, and why a BSET PSW.11 immediately followed by an
ATOMIC leaves no window for a request at all.

--no-pipeline-effects turns all four off, for running a program written before
they were here.  The part would run such a program wrongly rather than refuse
it; refusing is the more useful answer and not the only one that should be
available.

Two of these were put into the compiler on the strength of reading the manual,
at a time when nothing here could tell whether they were needed: the NOP after
SCXT CP in a banked handler's prologue, and the one after crt0's write to
DPP3.  Taking either out now stops a program that used to run.

GPT1
----

The three timers of the general purpose timer block: the core timer T3 and the
auxiliary timers T2 and T4, at the addresses and with the bit layouts of the
C167CR Derivatives User's Manual V3.1 chapter 10.  All nine registers are in
the set the three SFR maps in this tree agree about, so this is the same block
at the same addresses on every part, and none of it is gated on which one is
selected.

Timer mode is modelled for all three.  The rate is the manual's:

  fT3 = fCPU / (8 x 2^<T3I>)

which is a count every 8 << TxI states, a state being a CPU clock period.  The
direction is TxUD, an overflow or underflow raises the timer's own request, and
T3's toggles T3OTL as well.

Reload mode is modelled for the two auxiliary timers, triggered by a T3OTL
transition: "upon a trigger signal T3 is loaded with the contents of the
respective timer register (T2 or T4) and the interrupt request flag (T2IR or
T4IR) is set".  That is how a periodic interrupt is actually written, and it
is why the handler in test/tools/c166-sim/timer.s has nothing to do but count.

Counter mode, the two gated modes, capture mode, incremental interface mode, a
reload triggered by the TxIN pin and the external up/down control are not
modelled, because all of them are driven by a pin and this simulator has none.
A program that asks for one of them while the timer is running stops with the
reason, at the write that asked - never counting would be a worse answer than
saying so.  A timer configured for one of them and left stopped is left alone,
because it does nothing on the part either.

The prescaler starts again whenever what a timer is doing changes, which is a
choice: the manual gives the rate and says nothing about the phase.  The rate
is what a program can observe.

Entering a handler costs four states, TRAP's figure for the same work, and is
not counted as an instruction.  The real response time is longer, because of
arbitration and the pipeline being thrown away, in the same way and for the
same reason as the other lower bounds in the state count below.

test/tools/c166-sim/interrupt.s and timer.s are the checks, the first for what
the core does with a request and the second for where a timer's comes from;
differential/timer.c is the whole chain from C, where the handler is written
with the interrupt attribute and the vector slot and the linker script row are
the compiler's.  interrupt.s's last case is the one that
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
