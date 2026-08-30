/* A linker script for a C167.
 *
 * The memory map is that part's rather than an XC164CM's, which is what makes
 * this a second script instead of a switch inside c166.ld: the program memory
 * is at the bottom of segment 0 instead of in a segment of its own, the RAM is
 * the internal RAM at 00'F600H and nothing else, and there is no PLL to
 * program.  See README.txt in this directory.
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
 * That leaves 00'F600H to 00'F9FFH for static data and the ABI stack, which on
 * a C167CR is all the RAM there is once the system stack has its KByte.  It is
 * not much, and it is what the part has. */
__system_stack_top   = 0xFC00;
__system_stack_limit = 0xFA00;
__register_bank      = 0xFC00;

/* The memory a part has.  -mmcu= supplies these; the defaults are what a
 * C167CR-16RM has, so that a script used on its own still links for a part
 * this file was written against.
 *
 * The extension RAM is deliberately not a region here.  Its size is in the
 * part table and reaches this script as __c166_xram_length, but where it is
 * mapped is in the C167 User's Manual rather than in the data sheet the table
 * was read from, and a region placed at a guessed address is worse than no
 * region at all: a program would link and then write somewhere else.  A board
 * script that knows the address can add it. */
__c166_rom_length    = DEFINED(__c166_rom_length)    ? __c166_rom_length    : 128K;
__c166_iram_length   = DEFINED(__c166_iram_length)   ? __c166_iram_length   : 2K;

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

  /* The internal RAM under the system stack.  Static data and the ABI stack
   * share it, growing towards each other from either end. */
  iram (rw) : ORIGIN = 0x0000F600,
              LENGTH = __system_stack_limit - 0x0000F600
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
  } > iram AT > rom
  __data_load = LOADADDR(.data);

  .bss (NOLOAD) : ALIGN(2) {
    __bss_start = .;
    *(.bss .bss.*)
    *(COMMON)
    . = ALIGN(2);
    __bss_end = .;
  } > iram

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
