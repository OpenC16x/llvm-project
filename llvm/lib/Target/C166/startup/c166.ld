/* A linker script for an XC164CM.
 *
 * The memory map is that part's: program Flash at C0'0000H and DPRAM at
 * 00'F600H.  See README.txt in this directory for what to change for another
 * part, and for the DSRAM and PSRAM this does not use.
 *
 * Nothing is in segment 0, which is what shapes this script.  A 16 bit address
 * does not name a physical location on its own: for data the DPPs supply the
 * page and for code CSP supplies the segment, and the instruction carries only
 * the low 16 bits.  So the DPPs below are chosen to make the Flash and the RAM
 * both reachable by a near address, and crt0.S programs them from the symbols
 * this script defines.  Near references relocate as SOF16, the offset within
 * the segment, which is what makes that work.
 *
 * Part of the LLVM Project, under the Apache License v2.0 with LLVM
 * Exceptions.  See https://llvm.org/LICENSE.txt for license information.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */

OUTPUT_FORMAT(elf32-c166)
ENTRY(__reset_vector)

/* The DPRAM runs from 00'F600H to 00'FDFFH and is shared between the system
 * stack, the register bank and everything else.  These three are the values
 * SP, STKUN, STKOV and CP hold after reset, so the layout below is the one the
 * hardware already assumes: the system stack grows down from FC00H into
 * FA00H, and R0 to R15 sit at FC00H just above it.
 */
__system_stack_top   = 0xFC00;
__system_stack_limit = 0xFA00;
__register_bank      = 0xFC00;

MEMORY
{
  /* Only the first 48 KByte of the Flash fits under the three data page
   * pointers that map it; the top 16 KByte would collide with the page the RAM
   * and the special function registers need.  So this region is what a near
   * address can reach, which is the constraint on read-only data rather than
   * on code: a branch takes its segment from CSP and its offset from the
   * instruction, so code is reachable anywhere in the segment. */
  rom (rx) : ORIGIN = 0x00C00000, LENGTH = 48K
  /* The rest of the Flash, which only a far access reaches.  A program with
   * more than 48 KByte of code can put .text here instead, since code does not
   * go through the data page pointers; read-only data cannot follow it. */
  farrom (rx) : ORIGIN = 0x00C0C000, LENGTH = 16K
  /* Static data goes in the DSRAM, which is 2 KByte and entirely free.  It is
   * a near address like everything else here: the part reaches it through a
   * data page pointer holding page 3, which is what DPP3 already holds.  The
   * variant of this part with 32 KByte of Flash has no DSRAM at all, and wants
   * this region deleted and .data and .bss moved to dpram. */
  dsram (rw) : ORIGIN = 0x0000C000, LENGTH = 2K
  /* The DPRAM below the system stack, which the ABI stack gets to itself.
   * FC20H to FCFFH above the register bank is free too, and FD00H to FDFFH is
   * the bit-addressable part, which is worth keeping for what needs it. */
  dpram (rw) : ORIGIN = 0x0000F600, LENGTH = 1K
  /* The PSRAM, which is the only RAM here that a near address cannot reach:
   * it is data page 896 and the four pointers are spoken for.  So this is
   * where __far data goes, and it is worth knowing that the manual calls it
   * optimised for code fetches and slower than the data memories for data - a
   * far access pays an EXTS on top of that.  It is for what does not fit. */
  psram (rw) : ORIGIN = 0x00E00000, LENGTH = 2K
}

/* The pages the data page pointers select, which is what makes a near address
 * mean anything on this part.  DPP0 to DPP2 cover the Flash, so a near address
 * of 0000H to BFFFH is an offset into it; DPP3 stays at page 3, which is
 * C000H to FFFFH of segment 0 - the DSRAM, the DPRAM and both register spaces.
 * A near address is the low 16 bits of the physical one either way, which is
 * why one relocation serves both. */
__dpp0_page = ORIGIN(rom) >> 14;
__dpp1_page = (ORIGIN(rom) >> 14) + 1;
__dpp2_page = (ORIGIN(rom) >> 14) + 2;
__dpp3_page = 3;

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

  /* What a far access reaches, which is what can live above the pages the data
   * page pointers cover. */
  .fartext : {
    *(.fartext .fartext.*)
  } > farrom

  .farrodata : ALIGN(2) {
    *(.farrodata .farrodata.*)
    . = ALIGN(2);
  } > farrom

  /* Initialised data lives in RAM but its image is in ROM; crt0 copies it
   * across.  Both ends are word aligned so that the copy can move words. */
  .data : ALIGN(2) {
    __data_start = .;
    *(.data .data.*)
    . = ALIGN(2);
    __data_end = .;
  } > dsram AT > rom
  __data_load = LOADADDR(.data);

  .bss (NOLOAD) : ALIGN(2) {
    __bss_start = .;
    *(.bss .bss.*)
    *(COMMON)
    . = ALIGN(2);
    __bss_end = .;
  } > dsram

  /* The same two again for what is declared __far, in the RAM that only a far
   * access reaches.  crt0.S copies and zeroes these with the segment supplied
   * by an EXTS, which is the only difference. */
  .fardata : ALIGN(2) {
    __fardata_start = .;
    *(.fardata .fardata.*)
    . = ALIGN(2);
    __fardata_end = .;
  } > psram AT > rom
  __fardata_load = LOADADDR(.fardata);

  .farbss (NOLOAD) : ALIGN(2) {
    __farbss_start = .;
    *(.farbss .farbss.*)
    . = ALIGN(2);
    __farbss_end = .;
  } > psram

  /* The ABI stack that R0 addresses grows down from the top of the DPRAM, and
   * has that KByte to itself: the system stack is above it and the static data
   * is in the DSRAM, so the two stacks grow towards each other from either end
   * of FA00H rather than the ABI stack growing into the program's variables. */
  __user_stack_top = ORIGIN(dpram) + LENGTH(dpram);
  __heap_start = __bss_end;
  __heap_end = __user_stack_top;

  /DISCARD/ : { *(.comment) *(.note.*) }
}
