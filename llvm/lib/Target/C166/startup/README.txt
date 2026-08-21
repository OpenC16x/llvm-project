Startup code for the C166
=========================

What is here is what has to exist between reset and main() on a C166, and a
memory map to link against.  None of it is built by the LLVM build: it is code
for the target, not for the machine doing the building, so it is here to be
copied into a project and adjusted rather than installed.

  crt0.S   the reset vector and the startup sequence
  c166.ld  a linker script for an SAB 83C166 with nothing attached to it
  mem.c    memcpy, memmove, memset and memcmp

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

The linker script is written for the smallest thing this family comes in: the
8 KByte of internal ROM at the bottom of code segment 0 and the 1 KByte of
internal RAM at 00'FA00H that an SAB 83C166 has, with nothing outside the
chip.  Almost every real board has external memory, and the MEMORY block at
the top of c166.ld is where that goes.

Three symbols in the script are addresses the hardware already knows, because
SP, STKUN, STKOV and CP come out of reset holding them:

  __system_stack_top    00'FC00H  where the system stack starts, growing down
  __system_stack_limit  00'FA00H  how far down it may go before STKOV fires
  __register_bank       00'FC00H  where R0 to R15 are, which is what CP says

The system stack is the hardware one, which holds return addresses and is not
addressable by the compiler.  The other stack, the one the ABI uses for
arguments, locals and spills, is addressed through R0 and grows down from
__user_stack_top, which the script puts at the top of RAM.  Both are in the
same 1 KByte to start with, which does not leave much; a board with external
RAM should move the user stack out there and give the system stack the room.

crt0.S leaves CP where reset put it.  CP says where R0 to R15 are, so an
instruction that changes it changes what every register operand around it
means; a program that wants a different register bank is better off doing that
deliberately than having the startup code do it quietly.

Interrupt handlers
------------------

The vector table is the low 512 bytes of code segment 0, one double word per
trap number, so trap n is at 4*n.  Trap 0 is reset, which is why crt0.S puts
its JMPS there.  A handler is declared

  __attribute__((interrupt)) void handler(void) { ... }

and needs a jump to it in its own slot.  There is nothing here that builds the
table, because which sources a part has is a property of the part; a project
does it with a section per vector and an ordered linker script, or with one
.vectors section written out by hand.

What is missing
---------------

mem.c is four functions, not a C library.  A project that needs printf, malloc
or anything else should build picolibc or newlib for c166 and link that
instead; mem.c exists so that a program can be linked and run with nothing
else present at all.

None of this has been executed on hardware, or on a simulator, because there
is no free C166 simulator to run it on.  It assembles, links, and disassembles
back to what it should be.
