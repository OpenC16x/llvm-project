/* A linker script for an ST10.
 *
 * This is a third script rather than a switch inside c167.ld because two
 * things about an ST10's memory are its own, and both are in its data sheet
 * rather than inferable from the core:
 *
 *   - the on-chip Flash is not one run.  256 KByte arrives as 32 KByte at
 *     00'0000H, then a hole, then 32 KByte at 01'8000H and three 64 KByte
 *     blocks at 02'0000H, 03'0000H and 04'0000H.  c167.ld computes its
 *     regions from one length on the assumption that the memory starts at
 *     zero and continues, and would place code in the hole.
 *
 *   - the extension RAM is somewhere else, and not the same somewhere else
 *     on every ST10.  An ST10F269 has 10 KByte at 00'C000H; an ST10F272 has
 *     2 KByte there at 00'E000H and a second 8 or 16 KByte at 09'0000H, which
 *     no data page pointer here holds.  -mmcu= supplies both.
 *
 * The internal RAM is the family's, 2 KByte at 00'F600H, and the reset vector
 * is at 00'0000H like any C166.  See README.txt in this directory.
 *
 * The lower 32 KByte of Flash can be remapped from segment 0 to segment 1 by
 * SYSCON.ROMS1.  This assumes segment 0, its reset value, so that a program in
 * on-chip Flash runs without anything having configured the part first.
 *
 * Part of the LLVM Project, under the Apache License v2.0 with LLVM
 * Exceptions.  See https://llvm.org/LICENSE.txt for license information.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */

OUTPUT_FORMAT(elf32-c166)
ENTRY(__reset_vector)

/* The internal RAM runs from 00'F600H to 00'FDFFH and is shared between the
 * system stack, the register bank and everything else.  These are the values
 * SP, STKUN, STKOV and CP hold after reset, so this is the layout the hardware
 * already assumes.  Unlike a C167 with no extension RAM, an ST10 always has
 * one, so the whole of the internal RAM below the system stack is the ABI
 * stack's and the static data is elsewhere. */
__system_stack_top   = 0xFC00;
__system_stack_limit = 0xFA00;
__register_bank      = 0xFC00;

/* The memory a part has.  -mmcu= supplies these; the defaults are an
 * ST10F269's, so that this script used on its own links for the part it was
 * written against. */
__c166_rom_length    = DEFINED(__c166_rom_length)    ? __c166_rom_length    : 256K;
__c166_iram_length   = DEFINED(__c166_iram_length)   ? __c166_iram_length   : 2K;
__c166_xram_length   = DEFINED(__c166_xram_length)   ? __c166_xram_length   : 10K;
__c166_xram_origin   = DEFINED(__c166_xram_origin)   ? __c166_xram_origin   : 0x0000C000;
__c166_xram2_length  = DEFINED(__c166_xram2_length)  ? __c166_xram2_length  : 0;
__c166_xram2_origin  = DEFINED(__c166_xram2_origin)  ? __c166_xram2_origin  : 0;

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

/* What crt0 writes to XPERCON before it sets SYSCON.XPEN, which is the only
 * window there is: the data sheet says XPERCON cannot be changed after the
 * global enable.  05H is the reset value - CAN1 and the 2 KByte XRAM1 - and
 * bit 3 is XRAM2EN, so 0DH turns the second extension RAM on and disturbs
 * nothing else.  A part whose XRAM2 is a region here needs it; one whose
 * extension RAM is all XRAM1 comes up with what it needs already.
 *
 * The F272 data sheet warns its XPERCON bit mapping is "not fully compatible
 * with ST10F269".  That is the peripherals it added in the upper bits; both
 * data sheets give XRAM1EN as bit 2 and XRAM2EN as bit 3, which is all of it
 * this touches.  A board that knows better defines the symbol itself. */
/* The default is an ST10F269's, like every other default here: 10 KByte of
 * extension RAM means XRAM2 is in use, so bit 3 goes on.  It is a constant
 * rather than a test on __c166_xram_length for the reason above - a test
 * would read that symbol's default and answer for the wrong part. */
__c166_xpercon = DEFINED(__c166_xpercon) ? __c166_xpercon : 0x0D;

/* Where XPERCON is.  The name is an ST10 one and this file's crt0 is shared
 * with the C167, so the address is supplied rather than assembled from the
 * name; see c167-crt0.S. */
__c166_xpercon_addr = DEFINED(__c166_xpercon_addr) ? __c166_xpercon_addr : 0x0000F024;
__c166_write_xpercon = DEFINED(__c166_write_xpercon) ? __c166_write_xpercon : 1;

/* SYSCON.XPEN turns the X-peripherals on, and the extension RAM is one.  There
 * is always one on an ST10, so unlike c167.ld this is not conditional on
 * having any - but a board still gets the last word. */
__c166_enable_xper = DEFINED(__c166_enable_xper) ? __c166_enable_xper : 1;

/* How the Flash arrives, which is the block table in the data sheet rather
 * than a length divided up.  Each region is inside one segment, because a far
 * access carries one segment and a near branch cannot leave one.
 *
 * All of the first block is under a data page pointer: DPP0 and DPP1 hold
 * pages 0 and 1, which is 0000H to 7FFFH, so a near address reaches the whole
 * of it and there is no 48 KByte limit to apply here - the Flash stops at
 * 32 KByte before the pointers do. */
__c166_rom0_length = __c166_rom_length > 32K ? 32K : __c166_rom_length;
__c166_rom1_length = __c166_rom_length - __c166_rom0_length > 32K
                       ? 32K : __c166_rom_length - __c166_rom0_length;
__c166_rom_rest1   = __c166_rom_length - __c166_rom0_length - __c166_rom1_length;
__c166_rom2_length = __c166_rom_rest1 > 64K ? 64K : __c166_rom_rest1;
__c166_rom_rest2   = __c166_rom_rest1 - __c166_rom2_length;
__c166_rom3_length = __c166_rom_rest2 > 64K ? 64K : __c166_rom_rest2;
__c166_rom4_length = __c166_rom_rest2 - __c166_rom3_length;

MEMORY
{
  /* The reset vector is at 00'0000H, so this region starts there and the part
   * comes up executing out of it.  Pages 0 and 1, so all of it is near. */
  rom (rx) : ORIGIN = 0x00000000, LENGTH = __c166_rom0_length

  /* The second Flash block, after the hole at 00'8000H to 01'7FFFH.  It is in
   * segment 1, which a near branch cannot reach from segment 0, so what goes
   * here is what a program puts in .fartext by name. */
  farrom (rx) : ORIGIN = 0x00018000, LENGTH = __c166_rom1_length

  /* One region per remaining segment, for the same reason: an object may not
   * straddle a segment boundary, so a 192 KByte run of Flash is three regions
   * and not one. */
  farrom2 (rx) : ORIGIN = 0x00020000, LENGTH = __c166_rom2_length
  farrom3 (rx) : ORIGIN = 0x00030000, LENGTH = __c166_rom3_length
  farrom4 (rx) : ORIGIN = 0x00040000, LENGTH = __c166_rom4_length

  /* The extension RAM the part names, which is where static data goes.  On an
   * ST10F269 that is 10 KByte at 00'C000H: XRAM2's 8 KByte runs up to 00'DFFFH
   * and XRAM1's 2 KByte starts at 00'E000H, so the two adjoin and are one
   * region.  On an ST10F272 it is XRAM1 alone and XRAM2 is the far region
   * below.
   *
   * Either way it is page 3, which DPP3 holds, so a near address names it.  It
   * cannot hold the system stack or a register bank - the data sheet says the
   * XRAM appears like external memory - which is why those stay in the
   * internal RAM where reset put them, and it is not bit addressable, which
   * costs nothing because static data was never in the bit addressable
   * window. */
  xram (rw) : ORIGIN = __c166_xram_origin, LENGTH = __c166_xram_length

  /* An ST10F272's second extension RAM at 09'0000H.  No pointer here holds
   * that page, so this is far memory: __far data lands here and near data
   * does not. */
  xram2 (rw) : ORIGIN = __c166_xram2_origin, LENGTH = __c166_xram2_length

  /* The internal RAM under the system stack.  This is the dual-port RAM, which
   * is the only memory the multiply-accumulate unit's IDX pointers reach, so
   * what __dpram places is here; the rest of it is the ABI stack's, since the
   * static data is in the extension RAM that every ST10 has. */
  iram (rw) : ORIGIN = 0x0000F600, LENGTH = __system_stack_limit - 0x0000F600
}

/* The pages the data page pointers select.  With the Flash in segments 0 and 1
 * and the RAM in page 3 these are 0, 1, 2 and 3, which is what they hold after
 * reset; crt0 writes them anyway, because writing what is already there costs
 * four instructions once and saves the reader wondering. */
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
ASSERT(LENGTH(farrom3) == 0 ||
       (ORIGIN(farrom3) >> 16) == ((ORIGIN(farrom3) + LENGTH(farrom3) - 1) >> 16),
       "the farrom3 region crosses a segment boundary")
ASSERT(LENGTH(farrom4) == 0 ||
       (ORIGIN(farrom4) >> 16) == ((ORIGIN(farrom4) + LENGTH(farrom4) - 1) >> 16),
       "the farrom4 region crosses a segment boundary")

/* The Flash blocks past the first do not follow it, so a length that fills
 * every region is the most this script can place.  Saying so here is better
 * than placing part of a program in a hole that reads as erased Flash. */
ASSERT(__c166_rom_length <= 32K + 32K + 64K + 64K + 64K,
       "this part has more program memory than st10.ld has regions for")

ASSERT(__system_stack_top <= 0x0000F600 + __c166_iram_length,
       "the system stack is above the end of this part's internal RAM")
/* The __dpram data shares the internal RAM with the ABI stack coming down.
 * How much room that leaves the stack is not a thing a link can know; running
 * past where the stack starts is. */
ASSERT(__dprambss_end <= __system_stack_limit,
       "the __dpram data does not fit under the ABI stack; the internal RAM holds both")

ASSERT(__system_stack_limit >= 0x0000F600,
       "the system stack starts below the internal RAM")
ASSERT(LENGTH(xram) == 0 ||
       (ORIGIN(xram) >> 16) == ((ORIGIN(xram) + LENGTH(xram) - 1) >> 16),
       "the xram region crosses a segment boundary")
ASSERT(LENGTH(xram2) == 0 ||
       (ORIGIN(xram2) >> 16) == ((ORIGIN(xram2) + LENGTH(xram2) - 1) >> 16),
       "the xram2 region crosses a segment boundary")

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

  /* Code and read-only data in the Flash blocks past the first, one output
   * section per region because one region is one segment. */
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

  .fartext3 : {
    *(.fartext3 .fartext3.*)
  } > farrom3

  .farrodata3 : ALIGN(2) {
    *(.farrodata3 .farrodata3.*)
    . = ALIGN(2);
  } > farrom3

  .fartext4 : {
    *(.fartext4 .fartext4.*)
  } > farrom4

  .farrodata4 : ALIGN(2) {
    *(.farrodata4 .farrodata4.*)
    . = ALIGN(2);
  } > farrom4

  /* Initialised data lives in RAM but its image is in Flash; crt0 copies it
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

  /* __far data, which lives in the second extension RAM where there is one.
   * On a part without one these collapse to an empty pair at the end of the
   * near data rather than being absent: the runtime refers to their bounds
   * either way, and an empty pair copies and zeroes nothing. */
  .fardata : ALIGN(2) {
    __fardata_start = .;
    *(.fardata .fardata.*)
    . = ALIGN(2);
    __fardata_end = .;
  } > xram2 AT > rom
  __fardata_load = LOADADDR(.fardata);

  .farbss (NOLOAD) : ALIGN(2) {
    __farbss_start = .;
    *(.farbss .farbss.*)
    . = ALIGN(2);
    __farbss_end = .;
  } > xram2

  /* What __dpram places, at the bottom of the internal RAM with the ABI stack
   * coming down towards it.  That memory is the only memory the coprocessor's
   * IDX0 and IDX1 can address - PM0036 section 2.1 - so an array a CoMAC walks
   * through [idx0+] has to be here.  Unlike the other two scripts this needs
   * no MAX: every ST10 has an extension RAM, so the static data is never in
   * this memory and the bottom of it is always free. */
  .dpramdata 0x0000F600 : ALIGN(2) {
    __dpramdata_start = .;
    *(.dpramdata .dpramdata.*)
    . = ALIGN(2);
    __dpramdata_end = .;
  } > iram AT > rom
  __dpramdata_load = LOADADDR(.dpramdata);

  .dprambss (NOLOAD) : ALIGN(2) {
    __dprambss_start = .;
    *(.dprambss .dprambss.*)
    . = ALIGN(2);
    __dprambss_end = .;
  } > iram

  /* No program SRAM on this part; the XC16x's E0'0000H window is its own. */
  __psramtext_start = .;
  __psramtext_end = .;
  __psramtext_load = .;

  /* The ABI stack comes down through the internal RAM from just under the
   * system stack.  The static data is in the extension RAM rather than below
   * it, so the whole of this is the stack's and the heap follows the data. */
  __user_stack_top = __system_stack_limit;
  __heap_start = __bss_end;
  __heap_end = ORIGIN(xram) + LENGTH(xram);

  /DISCARD/ : { *(.comment) *(.note.*) }
}
