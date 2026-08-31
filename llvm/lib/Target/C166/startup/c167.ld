/* A linker script for a C167.
 *
 * The memory map is that part's rather than an XC164CM's, which is what makes
 * this a second script instead of a switch inside c166.ld: the program memory
 * is at the bottom of segment 0 instead of in a segment of its own, the RAM is
 * the extension RAM at 00'E000H and the internal RAM at 00'F600H, and there is
 * no PLL to program.  See README.txt in this directory.
 *
 * The lower 32 KByte of the on-chip ROM can be mapped to segment 0 or segment
 * 1, which SYSCON.ROMS1 chooses.  This assumes segment 0, its reset value, so
 * that a program in on-chip ROM runs without anything having configured the
 * part first - which is what the reset vector at 00'0000H needs.
 *
 * A romless derivative has no on-chip program memory at all and fetches from
 * external memory over the bus.  What is out there is the board's, so
 * -mmcu=c167sr-lm gives this script a rom length of zero and a board that has
 * external Flash says so by defining __c166_rom_length itself.
 *
 * Part of the LLVM Project, under the Apache License v2.0 with LLVM
 * Exceptions.  See https://llvm.org/LICENSE.txt for license information.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */

OUTPUT_FORMAT(elf32-c166)
ENTRY(__reset_vector)

/* The internal RAM runs from 00'F600H to 00'FDFFH on a 2 KByte part and is
 * shared between the system stack, the register bank and everything else.
 * These three are the values SP, STKUN, STKOV and CP hold after reset, so the
 * layout below is the one the hardware already assumes: the system stack grows
 * down from FC00H into FA00H, and R0 to R15 sit at FC00H just above it.
 *
 * That leaves 00'F600H to 00'F9FFH for the ABI stack, with the static data in
 * the extension RAM below. */
__system_stack_top   = 0xFC00;
__system_stack_limit = 0xFA00;
__register_bank      = 0xFC00;

/* The memory a part has.  -mmcu= supplies these; the defaults are what a
 * C167CR-16RM has, so that a script used on its own still links for a part
 * this file was written against.
 *
 * The extension RAM is at 00'E000H, which the User's Manual gives; the data
 * sheet the part table was read from has its size and not its address. */
__c166_rom_length    = DEFINED(__c166_rom_length)    ? __c166_rom_length    : 128K;
__c166_iram_length   = DEFINED(__c166_iram_length)   ? __c166_iram_length   : 2K;
__c166_xram_length   = DEFINED(__c166_xram_length)   ? __c166_xram_length   : 2K;

/* A warning about the idiom above, which cost a real bug.  A symbol written
 * "X = DEFINED(X) ? X : default" ends up with the right value, but every later
 * expression in this script that reads X sees the default instead - so a
 * symbol derived from one of these answers for the part this file defaults to
 * rather than for the part on the command line.  Region lengths are safe,
 * because MEMORY is evaluated where the override is visible; a symbol the
 * startup code relocates against is not.
 *
 * The three the startup code reads at run time - __c166_enable_xper, and on
 * an ST10 __c166_write_xpercon and __c166_xpercon - are therefore decided by
 * the driver, which knows the part, and the defaults below apply only when
 * this script is used on its own with nothing overridden.  Deriving a fourth
 * one here would reintroduce it. */

/* Whether crt0 sets SYSCON.XPEN, which is what turns the X-peripherals on and
 * with them the XRAM.  The C167CS manual documents that bit as the enable and
 * says XPERCON comes up selecting the 2 KByte XRAM, so setting it is what
 * makes the region below exist.  The default is a C167CR-16RM's, like the
 * rest here, and that part has the extension RAM; a board with none defines
 * this as 0.  It is a constant rather than a test on __c166_xram_length for
 * the reason above - a test would read that symbol's default and answer 1 for
 * a part that had been given none, which is what it used to do. */
__c166_enable_xper = DEFINED(__c166_enable_xper) ? __c166_enable_xper : 1;

/* Whether crt0 writes XPERCON before that.  A C167 does not need to: its
 * XPERCON comes up selecting the one 2 KByte XRAM it has, which is the whole
 * of its extension RAM.  An ST10 has a second one that is off at reset and a
 * register that cannot be written after XPEN, which is why crt0 has the
 * sequence at all; see st10.ld.  A board with X-peripherals of its own to
 * select defines these two and gets the same window. */
__c166_write_xpercon = DEFINED(__c166_write_xpercon) ? __c166_write_xpercon : 0;
__c166_xpercon = DEFINED(__c166_xpercon) ? __c166_xpercon : 0;

/* Where XPERCON is, for the write above.  It is an extended register, which
 * a "reg" field cannot name, and this file's crt0 is assembled once for the
 * C167 and the ST10 - so the address comes from here rather than from a name
 * that only one of the two -mcpu= values knows. */
__c166_xpercon_addr = DEFINED(__c166_xpercon_addr) ? __c166_xpercon_addr : 0x0000F024;

/* How much of the program memory a near address reaches, and what is left.
 * This is the same 48 KByte the XC164CM script has, and for the same reason:
 * the limit is what the data page pointers cover rather than where the memory
 * is.  DPP0 to DPP2 hold pages 0 to 2, which is 0000H to BFFFH, and DPP3 has
 * to hold page 3 because that is where the RAM and both register spaces are. */
__c166_rom_near   = __c166_rom_length > 48K ? 48K : __c166_rom_length;
__c166_rom_far    = __c166_rom_length - __c166_rom_near > 16K
                      ? 16K : __c166_rom_length - __c166_rom_near;
__c166_rom_far2   = __c166_rom_length - __c166_rom_near - __c166_rom_far;

MEMORY
{
  /* The reset vector is at 00'0000H, so this region starts there and a part
   * with on-chip ROM comes up executing out of it. */
  rom (rx) : ORIGIN = 0x00000000, LENGTH = __c166_rom_near
  /* The rest of the first segment, which only a far access reaches for data;
   * code is reachable anywhere in the segment because a branch takes its
   * segment from CSP. */
  farrom (rx) : ORIGIN = 0x00000000 + __c166_rom_near,
                LENGTH = __c166_rom_far
  /* Program memory past the first segment.  A 128 KByte part has one, and it
   * has to be a region of its own: a far access carries one segment, so an
   * object may not straddle the boundary, and a near branch cannot cross it. */
  farrom2 (rx) : ORIGIN = 0x00010000, LENGTH = __c166_rom_far2

  /* Static data goes in the extension RAM at 00'E000H.  It is page 3 like the
   * internal RAM, so DPP3 reaches it and a near address names it directly.
   *
   * Three things the manual says about it shape what goes here.  It permits 16
   * bit accesses at full speed, so it is not a slow memory to put data in; it
   * cannot hold the system stack or a register bank, which is why those stay
   * where reset put them; and it is not bit addressable, which costs nothing
   * because the bit addressable window is FD00H to FDFFH and static data was
   * never in it.
   *
   * A part with no extension RAM puts its static data at the bottom of the
   * internal RAM instead, with the ABI stack coming down towards it.  That is
   * this region becoming the other one rather than a second set of output
   * sections, which is how c166.ld handles the same choice. */
  xram (rw) : ORIGIN = __c166_xram_length ? 0x0000E000 : 0x0000F600,
              LENGTH = __c166_xram_length
                         ? __c166_xram_length
                         : __system_stack_limit - 0x0000F600

  /* The internal RAM under the system stack, which the ABI stack has to itself
   * on a part with an extension RAM and which is empty on one without, since
   * the static data is already there. */
  iram (rw) : ORIGIN = 0x0000F600,
              LENGTH = __c166_xram_length
                         ? __system_stack_limit - 0x0000F600 : 0
}

/* The pages the data page pointers select.  With the program memory in segment
 * 0 these are 0, 1, 2 and 3 - which is exactly what they hold after reset, so
 * unlike the XC164CM script nothing has to program them before a near address
 * means anything.  crt0 writes them anyway, because writing what is already
 * there costs four instructions once and saves the reader wondering. */
__dpp0_page = 0;
__dpp1_page = 1;
__dpp2_page = 2;
__dpp3_page = 3;

/* No region may cross a segment boundary, because the compiler folds two
 * EXTends that reach the same object into one and "seg(g)" and "seg(g + 2)"
 * are only the same segment if g does not straddle one. */
ASSERT((ORIGIN(rom) >> 16) == ((ORIGIN(rom) + LENGTH(rom) - 1) >> 16),
       "the rom region crosses a segment boundary")
ASSERT(LENGTH(farrom) == 0 ||
       (ORIGIN(farrom) >> 16) == ((ORIGIN(farrom) + LENGTH(farrom) - 1) >> 16),
       "the farrom region crosses a segment boundary")
ASSERT(LENGTH(farrom2) == 0 ||
       (ORIGIN(farrom2) >> 16) == ((ORIGIN(farrom2) + LENGTH(farrom2) - 1) >> 16),
       "the farrom2 region crosses a segment boundary")

ASSERT(__system_stack_top <= 0x0000F600 + __c166_iram_length,
       "the system stack is above the end of this part's internal RAM")
ASSERT(__system_stack_limit >= 0x0000F600,
       "the system stack starts below the internal RAM")
ASSERT(LENGTH(xram) == 0 ||
       (ORIGIN(xram) >> 16) == ((ORIGIN(xram) + LENGTH(xram) - 1) >> 16),
       "the xram region crosses a segment boundary")

SECTIONS
{
  /* Trap 0 is reset, so the first four bytes of segment 0 are the jump the
   * part executes.  Anything else in the vector table goes here too. */
  .reset : {
    KEEP(*(.reset))
    KEEP(*(.vectors))
  } > rom

  .text : {
    *(.text .text.*)
  } > rom

  .rodata : {
    *(.rodata .rodata.*)
    . = ALIGN(2);
  } > rom

  /* The call frame information, and the tables a personality routine reads to
   * decide what to do with a frame.  There is no dynamic loader here, so the
   * bounds are symbols and the unwinder walks between them. */
  .eh_frame : ALIGN(4) {
    __eh_frame_start = .;
    KEEP(*(.eh_frame))
    __eh_frame_end = .;
  } > rom

  .gcc_except_table : ALIGN(4) {
    KEEP(*(.gcc_except_table .gcc_except_table.*))
  } > rom

  /* Code and read-only data above the first 48 KByte. */
  .fartext : {
    *(.fartext .fartext.*)
  } > farrom

  .farrodata : ALIGN(2) {
    *(.farrodata .farrodata.*)
    . = ALIGN(2);
  } > farrom

  .fartext2 : {
    *(.fartext2 .fartext2.*)
  } > farrom2

  .farrodata2 : ALIGN(2) {
    *(.farrodata2 .farrodata2.*)
    . = ALIGN(2);
  } > farrom2

  /* Initialised data lives in RAM but its image is in ROM; crt0 copies it
   * across.  Both ends are word aligned so that the copy can move words. */
  .data : ALIGN(2) {
    __data_start = .;
    *(.data .data.*)
    . = ALIGN(2);
    __data_end = .;
  } > xram AT > rom
  __data_load = LOADADDR(.data);

  .bss (NOLOAD) : ALIGN(2) {
    __bss_start = .;
    *(.bss .bss.*)
    *(COMMON)
    . = ALIGN(2);
    __bss_end = .;
  } > xram

  /* This part has no RAM that a near address cannot reach, so there is nothing
   * for __far data to live in and these are empty rather than absent: the
   * runtime refers to their bounds whether or not anything landed in them, and
   * an empty pair copies and zeroes nothing. */
  __fardata_start = .;
  __fardata_end = .;
  __fardata_load = .;
  __farbss_start = .;
  __farbss_end = .;
  __psramtext_start = .;
  __psramtext_end = .;
  __psramtext_load = .;

  /* The ABI stack comes down from just under the system stack, towards the
   * static data at the bottom of the same memory. */
  __user_stack_top = __system_stack_limit;
  __heap_start = __bss_end;
  __heap_end = __user_stack_top;

  /DISCARD/ : { *(.comment) *(.note.*) }
}
