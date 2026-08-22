Startup code for the C166
=========================

What is here is what has to exist between reset and main() on a C166, and a
memory map to link against.  None of it is built by the LLVM build: it is code
for the target, not for the machine doing the building, so it is here to be
copied into a project and adjusted rather than installed.

  crt0.S      the reset vector and the startup sequence
  c166.ld     a linker script for a part with nothing attached to it
  vectors.ld  the interrupt vector table, for a program that has handlers
  xc164cm.h   the special function registers, for C
  mem.c       memcpy, memmove, memset and memcmp

Building it
-----------

The driver looks for crt0.o and -lc in <sysroot>/c166-elf/lib, so build them
and put them there:

  mkdir -p sysroot/c166-elf/lib
  clang -target c166 -c crt0.S -o sysroot/c166-elf/lib/crt0.o
  clang -target c166 -O2 -c mem.c -o mem.o
  llvm-ar rcs sysroot/c166-elf/lib/libc.a mem.o

The compiler-rt builtins are what the compiler calls for the things the
instruction set does not do, such as dividing two longs.  Build them into the
resource directory, where the driver already knows to look:

  cmake -S compiler-rt/lib/builtins -B build-crt -GNinja \
      -DCMAKE_C_COMPILER=<...>/clang -DCMAKE_ASM_COMPILER=<...>/clang \
      -DCMAKE_AR=<...>/llvm-ar -DCMAKE_RANLIB=<...>/llvm-ranlib \
      -DCMAKE_NM=<...>/llvm-nm \
      -DCMAKE_C_COMPILER_TARGET=c166 -DCMAKE_ASM_COMPILER_TARGET=c166 \
      -DCOMPILER_RT_BAREMETAL_BUILD=ON -DCOMPILER_RT_DEFAULT_TARGET_ONLY=ON \
      -DLLVM_ENABLE_PER_TARGET_RUNTIME_DIR=ON \
      -DCOMPILER_RT_INSTALL_PATH=<...>/lib/clang/<version> \
      -DCMAKE_C_COMPILER_WORKS=1 -DCMAKE_ASM_COMPILER_WORKS=1 \
      -DLLVM_CONFIG_PATH=<...>/llvm-config
  ninja -C build-crt install

Then a whole program is one command:

  clang -target c166 -O2 --sysroot=sysroot -T c166.ld hello.c -o hello.elf

The driver puts crt0.o first, asks for -lc, and adds the compiler-rt builtins;
-nostartfiles, -nolibc and -nodefaultlibs turn those off one at a time.  There
is no default linker script, because the memory map belongs to the board: -T
is not optional.

What to change for a particular part
------------------------------------

The linker script is written for an XC164CM with nothing outside the chip: the
64 KByte of program Flash at C0'0000H and the 2 KByte of DPRAM at 00'F600H.
The MEMORY block at the top of c166.ld is where a board's own memory goes.

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

Writable far data - .fardata and .farbss - still goes in the ordinary RAM.  The
part has 2 KByte of DSRAM at 00'C000H and 2 KByte of PSRAM at E0'0000H that
would suit it better, but crt0.S copies .data with near addressing, so moving
it out there means a copy loop that writes far as well as reads far.

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
__user_stack_top, which the script puts at the top of RAM.  On an XC164CM that
leaves the ABI stack the KByte from 00'F600H to 00'F9FFH and the system stack
the two below FC00H, which is not much; a board with external RAM should move
the user stack out there.

crt0.S writes SP, STKUN, STKOV and the DPPs with a plain "mov sp, #..." each,
which the assembler can do because the 8 bit "reg" field of MOV reaches the
special function registers.  There is no scratch register involved.

crt0.S leaves CP where reset put it.  CP says where R0 to R15 are, so an
instruction that changes it changes what every register operand around it
means; a program that wants a different register bank is better off doing that
deliberately than having the startup code do it quietly.

Naming the registers from C
---------------------------

xc164cm.h gives every special function register a name, so a peripheral is
written the way it is in the manual:

  #include "xc164cm.h"

  DP3 = 0xFF;          /* port 3 all outputs */
  P3  = 0x0055;

The addresses are the ones C166RegisterInfo.td holds, which is what the
assembler and the disassembler use, so the two cannot disagree about where a
register is.  That is worth knowing because it is checkable: compiling a store
through this header and looking at the assembly should show the register's
name, since the disassembler prints an address it recognises by name.

  clang -target c166 -O2 -I . -S -o - x.c

turning "SYSCON1 = 0x1000" into "mov syscon1, r2" rather than a bare address.

Both register spaces are inside the page DPP3 selects, so these are ordinary
near accesses: no EXTR, no far pointer, and nothing to set up beyond the DPP3
that crt0.S already writes.

Which trap number a peripheral raises is not in the header.  That is Table 5-2
of the XC164CM User's Manual, and guessing at it is exactly the kind of thing
that produces a handler wired to the wrong source.

Interrupt handlers
------------------

The vector table is the low 512 bytes of code segment 0, one double word per
trap number, so trap n is at 4*n.  Trap 0 is reset, which is why crt0.S puts
its JMPS there.  What a slot holds is a jump, not an address: a trap and a
hardware interrupt both branch to the slot rather than reading through it.

A handler is declared

  __attribute__((interrupt)) void handler(void) { ... }

which makes it save what it uses and return with RETI, and its slot is claimed
by putting a jump there:

  .section .vectors.026,"ax",@progbits
  jmps    #seg(handler), sof(handler)

The number is the trap number in decimal, padded to three digits.  vectors.ld
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
