Startup code for the C166
=========================

What is here is what has to exist between reset and main() on a C166, and a
memory map to link against.  None of it is built by the LLVM build: it is code
for the target, not for the machine doing the building, so it is here to be
copied into a project and adjusted rather than installed.

  crt0.S               the reset vector and the startup sequence, XC164CM
  c166.ld              a linker script for an XC164CM with nothing attached
  c167-crt0.S          the same for a C167, which has no PLL to program and
                       no RAM outside the data page pointers
  c167.ld              a linker script for a C167
  st10.ld              a linker script for an ST10, whose Flash arrives in
                       blocks with a hole in the middle and whose extension
                       RAM is not where a C167's is
  vectors.ld           the interrupt vector table, for a program with handlers;
                       it serves both parts, since a C167 has the layout an
                       XC164CM comes up with and no registers to change it
  xc164cm.h            the special function registers, for C
  xc164cm-vectors.inc  the interrupt vector numbers, for assembly
  c167-vectors.inc     the same for a C167

Which of each pair a program gets is the core's, and -mcpu= or -mmcu= decides:
the driver puts c167-crt0.o first for the c167 and st10 cores and crt0.o for
the rest, in the order those two options already resolve the core in.  An ST10
takes the C167's startup because it needs nothing else - no PLL of the kind
crt0.S programs, and data page pointers that come up holding what its script
wants.  The one thing it does need beyond a C167, XPERCON written before
SYSCON.XPEN, is in that file driven by symbols the script defines, so a C167
branches over it and an ST10 does not.
The linker script is passed with -T and is not chosen for you, because the
memory map is the board's rather than the part's; the three here are starting
points for the three parts they name.

crt0.S has to be assembled for its own part - "-mcpu=xc16x", or an -mmcu= that
implies it - because it programs the XC164CM's PLL through that part's extended
special function registers, and a name from one derivative's map is refused for
another.  That is the whole point of the map being chosen rather than assumed;
saying which part a file is for is how it gets past it.
  mem.c                memcpy, memmove, memset and memcmp
  unwind.c             the DWARF unwinder, for C++ exceptions
  unwind-asm.S         the register capture and restore it needs
  cxa.c                the personality routine and the __cxa_ calls
  unwind.h             what a program calls into the unwinder
  runtime.c            errno, exit, abort and the assert handler
  include/             the standard headers a freestanding part can mean

Building it
-----------

Four things have to exist that the LLVM build does not produce, because they
are code for the target rather than for the machine doing the building: crt0.o
and libc.a, which the driver looks for in <sysroot>/c166-elf/lib, the headers,
which it looks for in <sysroot>/c166-elf/include, and the compiler-rt builtins,
which are what the compiler calls for the things the instruction set does not
do - 32 bit shifts and division, 64 bit arithmetic, and all of floating point,
which this part has no unit for.

One script builds all four:

  llvm/utils/C166Sim/differential/mksysroot.sh <build-dir> <sysroot>

It is in that directory because the differential tests need a sysroot before
they can run anything, and there is no reason to have two ways of building the
same thing.

Then a whole program is one command:

  clang -target c166 -mmcu=xc164cm-8f -O2 --sysroot=sysroot -T c166.ld \
      hello.c -o hello.elf

-mmcu= names the part, and that is what supplies the memory map: the linker
script below asks for the sizes rather than having them written into it, so
moving to another derivative is a different name rather than an edited script.
It also selects the core, so the multiply-accumulate unit comes on by itself
where the part has one, and defines __XC164CM_8F__ and the sizes as macros so
that code can ask.  Leaving it out links for an XC164CM-8F, which is what the
defaults in the script are.

Naming a part that does not exist says so, and lists the ones that do:

  clang -target c166 -mmcu=xc164 -c hello.c
  error: unknown part 'xc164' for '-mmcu='; known parts are: xc164cm-8f, ...

The corpus in llvm/utils/C166Sim/corpus builds drivers over LLVM's own libc
and checks them against the host the same way, which is what reaches the shapes
a hand written program does not.

Every part in the table is built and run by

  llvm/utils/C166Sim/differential/parts.sh <build-bin> <sysroot> c166.ld

which is what checks the maps rather than only the driver: run.sh links
everything for the default part, so a derivative whose map is wrong would not
show up there.

The table is llvm/include/llvm/TargetParser/C166TargetParser.def, and every
row in it cites the derivative table it came from.  Adding a part means
finding that table, not working the memory out from the part number: the
letters in the middle of an XC164 name say which peripherals it has and
nothing about its memory, and the suffix says the memory and nothing about the
peripherals.

The driver puts crt0.o first, asks for -lc, and adds the compiler-rt builtins;
-nostartfiles, -nolibc and -nodefaultlibs turn those off one at a time.  There
is no default linker script, because the memory map belongs to the board: -T
is not optional.

What to change for a particular part
------------------------------------

The linker script covers a part with nothing outside the chip.  Which part is
-mmcu='s business now; what is left to change here is memory the chip does not
have - external Flash or RAM on a board - which goes in the MEMORY block at the
top of c166.ld alongside the regions the part supplies.

Two derivatives in the table show what the script has to cope with, and both
are handled without an edit.  The XC164xx-4F has no data SRAM at all, so its
static data goes at the bottom of the dual-port RAM with the ABI stack coming
down from the top of the same memory towards it.  The XC164CS-16F has 128
KByte of program memory, which is two segments, and a far access carries one
segment while a near branch cannot leave one - so the second segment is a
region of its own, and code goes there with

  __attribute__((far, section(".fartext2")))

Only 48 KByte of that Flash is in the rom region, and the reason is the thing
to understand before changing any of this.  A 16 bit address does not name a
physical location on a C166: the data page pointers map each quarter of it onto
a 16 KByte page, so what an instruction carries is an offset and the DPPs say
where it lands.  This script points DPP0 to DPP2 at the first three pages of
the Flash and leaves DPP3 at page 3, which is 00'C000H to 00'FFFFH - the DSRAM,
the DPRAM and both register spaces.  So a near address of 0000H to BFFFH is
Flash and C000H to FFFFH is RAM and registers, and in both cases it is the low
16 bits of the physical address.  The top 16 KByte of the Flash has no near
address left to give it, which is why the region stops at 48 KByte; anything up
there has to be reached far, which is what the farrom region is: .fartext and
.farrodata go there, so an object declared __far uses the part of the Flash a
near address cannot name.  Overflowing either region is an error from the
linker, which is what keeps a program honest about the limit.

The 48 KByte limit is on read-only data rather than on code.  A branch takes
its segment from CSP and its offset from the instruction, so code is reachable
anywhere in the segment; a program with more than 48 KByte of it can put .text
in farrom instead, and only .rodata and the ROM image of .data have to stay
below C0'C000H.

Writable far data - .fardata and .farbss - goes in the PSRAM at E0'0000H, which
is the only RAM here a near address cannot reach: it is data page 896 and all
four page pointers are spoken for.  crt0.S initialises it with a second pair of
loops that supply the segment with an EXTS, reading the ROM image near and
writing the destination far.

It is the last resort of the three RAMs for data rather than the first.  The
manual calls the PSRAM optimised for code fetches and slower than the data
memories for data, and a far access pays an EXTS on top of that, so __far on a
variable is for what does not fit in the DSRAM rather than for anything it
makes faster.

Code is what that memory is good at, and a function goes there with

  __attribute__((far, noinline, section(".psramtext")))
  int scale(int x) { return x * 3 + 1; }

The section says where it lives, the far says how to call it - the PSRAM is a
different segment from the Flash, so it takes a CALLS - and the noinline is
what keeps it a function at all, and therefore what keeps it in the section.
Without it the compiler may inline the body and --gc-sections then drops the
copy that is left, so the section comes out empty.

The image lives in the Flash and crt0.S copies it up beside the far data.  The
reason to want this is not only speed: a routine that erases or writes the
Flash cannot be fetched from the Flash while it does so, so it has to be
somewhere else, and this is the somewhere else.

What such a function may call is the constraint to know about.  The PSRAM is a
different segment from the Flash, and a near call reaches only the segment it is
made from, so a function in .psramtext can only call functions that are
themselves far - the far is what makes the call a CALLS, which names the segment
it is going to.  Calling an ordinary function from there would land at that
offset in the PSRAM rather than in the Flash.  The linker refuses it and says
which call and which two addresses, so this is a build error and not something
to find at run time; the fix is __attribute__((far)) on the callee as well.

That applies to the calls the compiler makes on its own as well as to the ones
in the source, and those are the ones easy to be surprised by: the compiler-rt
builtins are ordinary near functions in the Flash, so a routine in the PSRAM
must not need one.  A 32 bit multiply and a shift by a constant are emitted
inline and are fine.  A shift by a variable amount is __ashlsi3, a 32 bit
division is __udivsi3, and anything in floating point or 64 bit arithmetic is a
call as well.  A Flash-erase routine has no reason to do any of that, which is
what makes the restriction bearable, but it is worth knowing before writing one
- and again the linker says so rather than leaving it to run time.

The pages are named by the script, not by crt0.S:

  __dpp0_page  __dpp1_page  __dpp2_page  __dpp3_page

so moving the memory means changing the MEMORY block and nothing else.  A part
whose ROM really is in segment 0 sets them to 0, 1, 2 and 3, which is the
identity and also the reset state.

Three symbols in the script are addresses the hardware already knows, because
SP, STKUN, STKOV and CP come out of reset holding them:

  __system_stack_top    00'FC00H  where the system stack starts, growing down
  __system_stack_limit  00'FA00H  how far down it may go before STKOV fires
  __register_bank       00'FC00H  where R0 to R15 are, which is what CP says

The system stack is the hardware one, which holds return addresses and is not
addressable by the compiler.  The other stack, the one the ABI uses for
arguments, locals and spills, is addressed through R0 and grows down from
__user_stack_top.

The two RAMs are split along that line.  Static data goes in the DSRAM, which
is 2 KByte and has nothing else in it; the KByte of DPRAM from 00'F600H to
00'F9FFH is the ABI stack's, with the system stack growing down into FA00H from
the other side.  So the two stacks meet at a known address and neither of them
grows into the program's variables, which is what would happen if all three
shared the DPRAM.

One thing does share it, and only because it has to.  The multiply-accumulate
unit's IDX0 and IDX1 reach the dual-port RAM and nothing else, so an array a
CoMAC walks through them has to be there; __dpram is what puts it there, at the
bottom of that KByte with the ABI stack coming down towards it.  How much room
that leaves the stack is not a thing a link can know, so the script asserts
only that the data itself fits below where the stack starts.  A program that
puts a lot here should move the system stack up, which is __system_stack_limit
at the top of the script.  crt0 copies and zeroes the two halves of it the way
it does .data and .bss, so a script of your own has to define
__dpramdata_load, __dpramdata_start, __dpramdata_end, __dprambss_start and
__dprambss_end - an empty pair is fine, and is what a part with no coprocessor
ends up with anyway.  The same goes for __bitdata_load, __bitdata_start,
__bitdata_end, __bitbss_start and __bitbss_end, which are the bit-addressable
RAM's.

Above the register bank, from 00'FD00H to 00'FDFEH, is the bit-addressable RAM
- the 128 words a bit instruction can name, and the only memory it can.  That
is where __bitaddr places a variable, and it is a region of its own in the
script, so a program with more than 256 bytes of them is told at the link
rather than one relocation at a time.  Nothing else here uses that part of the
memory: the two stacks and the register bank are all below it.

The variant of this part with 32 KByte of Flash has no DSRAM, and the script
handles that without an edit: the dsram region becomes the dual-port RAM, and
the __dpram data follows the static data in it rather than starting underneath.

crt0.S writes SP, STKUN, STKOV and the DPPs with a plain "mov sp, #..." each,
which the assembler can do because the 8 bit "reg" field of MOV reaches the
special function registers.  There is no scratch register involved.

crt0.S leaves CP where reset put it.  CP says where R0 to R15 are, so an
instruction that changes it changes what every register operand around it
means; a program that wants a different register bank is better off doing that
deliberately than having the startup code do it quietly.

The clock
---------

The other thing in c166.ld that belongs to the board rather than to the part is
the crystal, and everything downstream of it is arithmetic:

  __fosc_hz = 8000000;
  __pll_p   = 1;
  __pll_n   = 20;
  __pll_k   = 4;
  __cpsys   = 0;

The PLL divides the oscillator by P, multiplies by N and divides by K, so

  fIN  = fOSC / P    which the PLL takes only between 4 and 35 MHz
  fVCO = fIN  * N    which the VCO covers only between 100 and 250 MHz
  fMC  = fVCO / K    which this part runs at up to 40 MHz

and fCPU is fMC, or half of it when __cpsys is 1.  The default is an 8 MHz
crystal taken to the 40 MHz the part runs at, which is 8 / 1 * 20 / 4.

The script works those three out, checks every one of them, and turns them into
the single word crt0.S writes to PLLCON.  So a board with a different crystal
changes __fosc_hz and whichever of the dividers gets it back to the speed it
wants, and a combination that does not work is a link error naming which of the
limits it missed rather than a part that runs at the wrong speed or does not
start.  A 4 MHz crystal reaches the same 40 MHz as 4 / 1 * 30 / 3, and a 20 MHz
one as 20 / 2 * 20 / 5.

The VCO's band is not among the parameters because it is not a choice: the
three bands are 100 to 150, 150 to 200 and 200 to 250 MHz, and the field has to
name the one fVCO landed in.  __pll_vb is derived from fVCO for that reason.

The script also publishes what came out:

  __fosc_hz  __fin_hz  __fvco_hz  __fmc_hz  __fcpu_hz

but those are for the arithmetic and the checks above them and not for a
program to load.  A reference to a symbol is sixteen bits wide on this part -
that is what a near address is - so "mov r2, #__fcpu_hz" loads 5A00H and says
nothing at all about the 0262H it dropped.

The two a program actually computes from are published in halves for that
reason, and those do fit:

  __fmc_hz_lo  __fmc_hz_hi  __fcpu_hz_lo  __fcpu_hz_hi

An absolute symbol is one whose address is the value, so from C:

  extern char __fcpu_hz_lo[], __fcpu_hz_hi[];
  #define FCPU (((unsigned long)(unsigned)__fcpu_hz_hi << 16) | \
                (unsigned long)(unsigned)__fcpu_hz_lo)

which is what a baud rate divisor or a timer reload value is computed from.

crt0.S does three things with all of this, in an order that matters.  It sets
CPSYS first, while the CPU is still on the oscillator, so that the divider is
right before the faster clock arrives rather than after.  Then it starts the
VCO with PLLCTRL = 01B, which runs the VCO while leaving the CPU on the
oscillator, and waits for SYSSTAT's PLLLOCK.  Only then does it write PLLCON
again with PLLCTRL = 11B, which is what puts the CPU on the PLL.

All of that is before EINIT, and has to be: PLLCON and SYSCON1 are protected
registers, and the protection is off between reset and EINIT and on afterwards.

The wait is bounded.  The CPU is on the bypass clock while it waits, which is
fOSC/(P*K), and the checks above hold that above 250 kHz - fOSC/P is at least
4 MHz and K is at most 16 - so the 65535 turns the loop allows are seconds
rather than microseconds.  That is far longer than the PLL takes to lock, which
leaves an oscillator that never started as the only way to reach the end of it.
When it is reached crt0.S jumps to __pll_lock_failed, which is a weak symbol
whose default is to hang; a program with something to say about a dead crystal
defines its own and gets control on the bypass clock.

Naming the registers from C
---------------------------

xc164cm.h gives every special function register a name, so a peripheral is
written the way it is in the manual:

  #include "xc164cm.h"

  DP3 = 0xFF;          /* port 3 all outputs */
  P3  = 0x0055;

What is here is what an XC164CM has.  A register whose address appears nowhere
in that part's two manuals has been left out rather than offered to be written
to by mistake, so the classic CAPCOM1 block, the PWM unit and ports 0, 2, 4, 6,
7 and 8 are absent - this part has CAPCOM1 and CAPCOM2 under other names and
addresses, CAPCOM6 in the X-peripheral space that no short address reaches, and
only ports 1, 3, 5 and 9.  Writing to one that is not here is a compile error
rather than a store to whatever the address turned out to be.

The addresses are the ones C166RegisterInfo.td holds, which is what the
assembler and the disassembler use, so the two cannot disagree about where a
register is.  That is worth knowing because it is checkable: compiling a store
through this header and looking at the assembly should show the register's
name, since the disassembler prints an address it recognises by name.

  clang -target c166 -O2 -I . -S -o - x.c

turning "SYSCON1 = 0x1000" into "mov syscon1, r2" rather than a bare address.

All three register spaces are inside the page DPP3 selects, so these are
ordinary near accesses: no EXTR, no far pointer, and nothing to set up beyond
the DPP3 that crt0.S already writes.  That includes the on-chip X-peripherals
at E800H and up - the CAPCOM6 unit, the LXBus controller and the interrupt jump
table cache - which have no short address at all and so can only be reached
this way.

Which trap number a peripheral raises is not in the header; it is in
xc164cm-vectors.inc, because a vector is claimed from assembly rather than
from C.

Interrupt handlers
------------------

The vector table is the low 512 bytes of code segment 0, one double word per
trap number, so trap n is at 4*n.  Trap 0 is reset, which is why crt0.S puts
its JMPS there.  What a slot holds is a jump, not an address: a trap and a
hardware interrupt both branch to the slot rather than reading through it.

A handler is declared

  __attribute__((interrupt)) void handler(void) { ... }

which makes it save what it uses and return with RETI, and its slot is claimed
by name:

  #include "xc164cm-vectors.inc"
  VECTOR_ASC0_RIC  uart_rx_isr

That claims the slot ASC0 raises on a received character.  Every source the
part has is in that file, named after its interrupt control register, so
nothing has to be looked up and typed - a handler wired to the wrong source is
not a failure anyone sees quickly.  A slot can be claimed by number instead,
if the number is already known:

  .section .vectors.043,"ax",@progbits
  jmps    #seg(handler), sof(handler)

where the number is the trap number in decimal, padded to three digits.

The hardware traps are in that file too, under the names the manual gives them
- VECTOR_NMITRAP, VECTOR_STOTRAP for stack overflow, VECTOR_STUTRAP for stack
underflow, VECTOR_SBRKTRAP, and VECTOR_BTRAP for the four class B traps, which
share one vector and leave TFR to say which of them it was.  They sit in every
other slot rather than consecutive ones.  Vector 0 is reset and crt0.S claims
it, so there is no macro for that one.  vectors.ld
places each of the 128 slots at the address it has to be at, so slots that
nothing claims can be left out rather than counted:

  SECTIONS
  {
    INCLUDE vectors.ld
    .text : { ... }
  }

It replaces the .reset section of c166.ld, and is a separate file because the
table is 512 bytes of ROM whether its slots are filled or not, which a program
with no handlers should not pay.  Unclaimed slots end up holding LLD's trap
pattern, JMPR cc_UC, -1, so a spurious interrupt stops rather than running into
whatever follows.

On an XC164CM this is the layout the part has at reset.  Two of its registers
can change it: CPUCON1.VECSC sets the space between vectors, and resets to 00,
which is the two words assumed here; VECSEG selects the segment the table lives
in, and resets to C0H for a standard start from internal program memory, which
is where this script puts the Flash.  A program that writes either wants a
different script from this one.  Both are protected after EINIT, so a startup
sequence that changes them has to do it before that.

The headers
-----------

include/ holds what a part with one thread, no operating system, no heap and no
floating point unit can honestly mean of the standard headers.  mksysroot.sh
copies them into <sysroot>/c166-elf/include, which is the directory the driver
already searches; clang brings its own <stddef.h>, <stdint.h>, <limits.h> and
the rest of the freestanding set, so those are not here.  Neither are
<stdio.h>, <math.h> or <time.h>: there is no I/O, no floating point unit and no
clock, so there is nothing behind them to declare.

  errno.h     one int, reached through the errno macro, with Linux's numbers
  string.h    the five mem.c defines, and the rest of <string.h> declared
  stdlib.h    the integer and search parts, div_t and friends, RAND_MAX
  assert.h    assert, calling __c166_assert_failed
  inttypes.h  intmax_t, imaxdiv_t and the PRI and SCN macros
  wchar.h     wint_t, mbstate_t and the declarations
  uchar.h     char16_t, char32_t and the declarations
  locale.h    locale_t, struct lconv and the LC_ macros
  fenv.h      an environment with no exceptions and one rounding direction

Two rules run through all of them.

Nothing is written down here that the compiler already knows.  Every format
string in inttypes.h is one of clang's own __INT32_FMTd__ macros, so PRId32 is
"ld" here because int32_t is long here, and would be "d" on a target where it
is int; wchar.h uses __WINT_TYPE__ and uchar.h uses __CHAR32_TYPE__.  A change
to the target's type mapping updates the headers by itself.

Almost everything is declared and not defined.  malloc, strcpy, qsort,
setlocale, mbrtowc, fegetround: a program that calls one gets a link error
naming it, which is a better answer than a header that hides the function or a
one-size-fits-nobody implementation of it in a library nobody chose.  What is
defined is what already had to be: the five functions in mem.c that the
compiler calls by itself, and errno, exit, _Exit, abort and the assert handler
in runtime.c, which the headers promise.  Everything in runtime.c but errno is
weak, so a program with a watchdog to kick or something to print replaces the
one it cares about; the defaults stop the machine, which is where crt0 goes
when main returns.

<stdlib.h> deliberately has no atexit.  exit here runs no handlers - there is
nothing on this part for one to clean up, and nothing to return to - so
declaring atexit would promise something exit does not do.

thread_local compiles, and means static: this part runs one thread, so
per-thread storage and static storage are the same storage.  An interrupt
handler and the code it interrupted therefore see the same object, which is
what a handler needs, since it is not a thread.  A thread_local with a
constructor is built on first use; its destructor is handed to
__cxa_thread_atexit in cxa.c, which records nothing and so never runs it, for
the same reason exit runs no handlers.  llvm/lib/Target/C166/README.txt says
what this does not do, which is make a second thread work.

What is missing
---------------

mem.c is four functions, not a C library.  A project that needs printf, malloc
or anything else should build picolibc or newlib for c166 and link that
instead; mem.c exists so that a program can be linked and run with nothing
else present at all.

None of this has been executed on hardware.  It has been executed on a
simulator: llvm/utils/C166Sim runs it, and the differential tests there link
this crt0.S and this mem.c into every program they check, so the startup
sequence, the block functions and the linker script are all exercised on every
run.  What that does not establish is that a part agrees with the simulator.


C++ exceptions
--------------

Throwing and catching work, and are built into libc.a by mksysroot.sh along
with everything else.  Nothing has to be turned on:

  clang++ -target c166 -O2 --sysroot=sysroot -T c166.ld prog.cpp -o prog.elf

What makes it work is that a return address on this part is on the system
stack, which no generated code touches, so the call frame information says
where it is with a DWARF expression rather than an offset.  The unwinder runs
that expression.  The tables are found between __eh_frame_start and
__eh_frame_end, which c166.ld defines, because there is no loader to ask.

Three things are smaller than a hosted implementation, and all three are
choices rather than oversights.

  Type matching is on the address of the type information object, which is
  exact-type matching.  catch(...) catches everything and catching a base class
  by reference does not catch a derived one.  A catch that would need the class
  hierarchy does not take the wrong branch - it does not match, and the
  exception carries on to whatever does.

  There is no heap, so thrown objects live in a pool of four slots of 32 bytes.
  Four is enough for a throw from inside a catch from inside a catch; a program
  that needs more, or an object bigger than 32 bytes, reaches
  __cxa_call_terminate rather than corrupting what is already in flight.

  __cxa_call_terminate is a weak symbol that stops the machine.  A program that
  wants to say something first defines its own.

The cost is worth knowing before designing around it.  Over a throw caught
three frames up, one throw is about 330,000 states - some 16 ms at 20 MHz -
because the frame description entries are searched linearly and the call frame
instructions are interpreted.  The unwinder and the C++ ABI together are about
13 KByte of Flash, which on a 64 KByte part is a fifth of it.  Exceptions here
are for what has gone wrong, not for control flow.
