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
  /* The part has 64 KByte of Flash, but only the first 48 fit under the three
   * data page pointers that map it; the top 16 KByte would collide with the
   * page the RAM and the special function registers need.  Code and data that
   * has to live up there is reachable far, not near, so it does not belong in
   * this region. */
  rom (rx) : ORIGIN = 0x00C00000, LENGTH = 48K
  /* The DPRAM below the system stack.  FC20H to FCFFH above the register bank
   * is free too, and FD00H to FDFFH is the bit-addressable part, which is
   * worth keeping for what needs it. */
  ram (rw) : ORIGIN = 0x0000F600, LENGTH = 1K
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
    *(.fartext .fartext.*)
  } > rom

  .rodata : {
    *(.rodata .rodata.*)
    *(.farrodata .farrodata.*)
    . = ALIGN(2);
  } > rom

  /* Initialised data lives in RAM but its image is in ROM; crt0 copies it
   * across.  Both ends are word aligned so that the copy can move words. */
  .data : ALIGN(2) {
    __data_start = .;
    *(.data .data.*)
    *(.fardata .fardata.*)
    . = ALIGN(2);
    __data_end = .;
  } > ram AT > rom
  __data_load = LOADADDR(.data);

  .bss (NOLOAD) : ALIGN(2) {
    __bss_start = .;
    *(.bss .bss.*)
    *(.farbss .farbss.*)
    *(COMMON)
    . = ALIGN(2);
    __bss_end = .;
  } > ram

  /* The ABI stack that R0 addresses grows down from the top of RAM. */
  __user_stack_top = ORIGIN(ram) + LENGTH(ram);
  __heap_start = __bss_end;
  __heap_end = __user_stack_top;

  /DISCARD/ : { *(.comment) *(.note.*) }
}
