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

/* The clock, which is the other thing besides the memory map that belongs to
 * the board rather than to the part.  Only the crystal below is a fact about a
 * particular board; the three dividers are a choice, and the arithmetic and
 * the checks that follow are the part's.
 *
 * The PLL takes the oscillator through three stages:
 *
 *   fIN  = fOSC / P      P = __pll_p,  1 to 4,   and fIN must be 4 to 35 MHz
 *   fVCO = fIN  * N      N = __pll_n,  1 to 32,  and fVCO must be in a band
 *   fMC  = fVCO / K      K = __pll_k,  1 to 16,  and fMC must be at most 40 MHz
 *
 * and fCPU is fMC, or half of it when __cpsys is 1.  crt0.S programs this from
 * __pllcon below and waits for the PLL to lock; nothing else here is read at
 * run time, so the checks are the point of the rest.
 *
 * The default is an 8 MHz crystal taken to the 40 MHz this part runs at, which
 * is 8 / 1 * 20 / 4.  A board with a different crystal changes __fosc_hz and
 * whichever of the three dividers gets it back to 40 MHz; getting it wrong is
 * a link error rather than a part that runs at the wrong speed or not at all.
 */
__fosc_hz = 8000000;
__pll_p   = 1;
__pll_n   = 20;
__pll_k   = 4;
__cpsys   = 0;

__fin_hz  = __fosc_hz / __pll_p;
__fvco_hz = __fin_hz * __pll_n;
__fmc_hz  = __fvco_hz / __pll_k;
__fcpu_hz = __fmc_hz / (__cpsys + 1);

/* The same two frequencies in halves, because a program cannot use the symbols
 * above.  A reference to a symbol is sixteen bits wide here - that is what a
 * near address is - so "mov r2, #__fcpu_hz" would load 5A00H and say nothing
 * about the 0262H it dropped.  These are what a baud rate divisor or a timer
 * reload value is computed from; see README.txt for how to reach them from C.
 */
__fmc_hz_lo  = __fmc_hz & 0xFFFF;
__fmc_hz_hi  = (__fmc_hz >> 16) & 0xFFFF;
__fcpu_hz_lo = __fcpu_hz & 0xFFFF;
__fcpu_hz_hi = (__fcpu_hz >> 16) & 0xFFFF;

/* Which of the VCO's three bands covers fVCO.  This is derived rather than
 * chosen because it is not a choice: the bands are 100 to 150, 150 to 200 and
 * 200 to 250 MHz and the field has to name the one fVCO is in.  They meet at
 * their endpoints, where either value is legal and this takes the upper. */
__pll_vb = __fvco_hz < 150000000 ? 0 : (__fvco_hz < 200000000 ? 1 : 2);

ASSERT(__pll_p >= 1 && __pll_p <= 4, "__pll_p is the input divider, 1 to 4")
ASSERT(__pll_n >= 1 && __pll_n <= 32, "__pll_n is the VCO multiplier, 1 to 32")
ASSERT(__pll_k >= 1 && __pll_k <= 16, "__pll_k is the output divider, 1 to 16")
ASSERT(__cpsys == 0 || __cpsys == 1, "__cpsys is 0 for fMC or 1 for fMC/2")
ASSERT(__fin_hz >= 4000000 && __fin_hz <= 35000000,
       "fOSC / __pll_p is outside the 4 to 35 MHz the PLL input takes")
ASSERT(__fvco_hz >= 100000000 && __fvco_hz <= 250000000,
       "fIN * __pll_n is outside the 100 to 250 MHz the VCO covers")
ASSERT(__fmc_hz <= 40000000, "fVCO / __pll_k is above the 40 MHz fMC maximum")
ASSERT(__fcpu_hz <= 40000000, "fMC / (__cpsys + 1) is above the 40 MHz fCPU maximum")

/* PLLCON, which is one write: the three dividers are held one less than they
 * multiply or divide by, the band goes in the middle, and PLLCTRL at the top
 * says what the CPU is running on.  11B is the PLL, 01B is the oscillator with
 * the VCO running beside it, which is where the CPU waits while the PLL locks.
 *
 * PLLWRI, bit 15, is set by the hardware and not by this. */
__pllcon = (3 << 13) | ((__pll_n - 1) << 8) | (__pll_vb << 6) |
           ((__pll_p - 1) << 4) | (__pll_k - 1);
__pllcon_bypass = (1 << 13) | ((__pll_n - 1) << 8) | (__pll_vb << 6) |
                  ((__pll_p - 1) << 4) | (__pll_k - 1);

/* CPSYS is bit 8 of SYSCON1, already in place: crt0.S ORs this in rather than
 * shifting, because an assembler cannot fold an expression over a symbol the
 * linker has not defined yet. */
__cpsys_bit = __cpsys << 8;

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

/* A far access carries its segment as an immediate, and the compiler folds two
 * of them into one EXTend when both reach the same object - "seg(g)" and
 * "seg(g + 2)" are the same segment unless g straddles a segment boundary.
 * Nothing in an object file rules that out, so it is ruled out here: no region
 * above crosses one, and therefore nothing placed in one does either.  Moving
 * any of them keeps that true or the link fails. */
ASSERT((ORIGIN(rom) >> 16) == ((ORIGIN(rom) + LENGTH(rom) - 1) >> 16),
       "the rom region crosses a segment boundary")
ASSERT((ORIGIN(farrom) >> 16) == ((ORIGIN(farrom) + LENGTH(farrom) - 1) >> 16),
       "the farrom region crosses a segment boundary")
ASSERT((ORIGIN(dsram) >> 16) == ((ORIGIN(dsram) + LENGTH(dsram) - 1) >> 16),
       "the dsram region crosses a segment boundary")
ASSERT((ORIGIN(dpram) >> 16) == ((ORIGIN(dpram) + LENGTH(dpram) - 1) >> 16),
       "the dpram region crosses a segment boundary")
ASSERT((ORIGIN(psram) >> 16) == ((ORIGIN(psram) + LENGTH(psram) - 1) >> 16),
       "the psram region crosses a segment boundary")

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
   * decide what to do with a frame.  A hosted program finds these by asking
   * the dynamic loader where the program headers are; there is no loader here,
   * so the bounds are symbols instead and the unwinder walks between them.
   *
   * They live in the Flash with the rest of the read only data, which is above
   * the first 64 KByte, so the addresses these symbols carry do not fit in a
   * near pointer.  The unwinder holds them as far pointers, the same way crt0
   * reaches the load addresses below. */
  .eh_frame : ALIGN(4) {
    __eh_frame_start = .;
    KEEP(*(.eh_frame))
    __eh_frame_end = .;
  } > rom

  .gcc_except_table : ALIGN(4) {
    KEEP(*(.gcc_except_table .gcc_except_table.*))
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

  /* Code that runs from the PSRAM rather than from the Flash.  That is what
   * that memory is for - the manual calls it optimised for code fetches - and
   * it is also the only way to run a routine that rewrites the Flash, since a
   * part cannot fetch from Flash while erasing it.  The image lives in ROM and
   * crt0.S copies it up, so a function goes here with
   *
   *   __attribute__((far, section(".psramtext")))
   *
   * where the section says where it lives and the far says how to call it: the
   * PSRAM is a different segment from the Flash, so it takes a CALLS. */
  .psramtext : ALIGN(2) {
    __psramtext_start = .;
    *(.psramtext .psramtext.*)
    . = ALIGN(2);
    __psramtext_end = .;
  } > psram AT > rom
  __psramtext_load = LOADADDR(.psramtext);

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
