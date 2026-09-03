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

/* The memory a part has.  -mmcu= supplies these; the defaults are what an
 * XC164CM-8F has, so that a script used on its own still links for the part
 * this file was written against.
 *
 * Naming a part is the way to change them.  Editing the numbers here works
 * too, and is what a board with memory outside the chip has to do, but a part
 * that Infineon makes is better named than transcribed: the letters in the
 * middle of an XC164 part number say which peripherals it has and nothing
 * about its memory, and the suffix says the memory and nothing about the
 * peripherals, so a map worked out from the name rather than from the
 * derivative table is wrong in a way that only shows up when a program
 * overflows a region that was never there. */
__c166_rom_length    = DEFINED(__c166_rom_length)    ? __c166_rom_length    : 48K;
__c166_farrom_length = DEFINED(__c166_farrom_length) ? __c166_farrom_length : 16K;
__c166_farrom2_length = DEFINED(__c166_farrom2_length) ? __c166_farrom2_length : 0;
__c166_dsram_length  = DEFINED(__c166_dsram_length)  ? __c166_dsram_length  : 2K;
__c166_dpram_length  = DEFINED(__c166_dpram_length)  ? __c166_dpram_length  : 2K;
__c166_psram_length  = DEFINED(__c166_psram_length)  ? __c166_psram_length  : 2K;

MEMORY
{
  /* Only the first 48 KByte of the program memory fits under the three data
   * page pointers that map it; the top of the segment would collide with the
   * page the RAM and the special function registers need.  So this region is
   * what a near address can reach, which is the constraint on read-only data
   * rather than on code: a branch takes its segment from CSP and its offset
   * from the instruction, so code is reachable anywhere in the segment. */
  rom (rx) : ORIGIN = 0x00C00000, LENGTH = __c166_rom_length
  /* The rest of it, which only a far access reaches.  A program with more
   * code than the region above can put .text here instead, since code does not
   * go through the data page pointers; read-only data cannot follow it.  On a
   * part whose program memory is 48 KByte or less this is empty. */
  farrom (rx) : ORIGIN = 0x00C00000 + __c166_rom_length,
                LENGTH = __c166_farrom_length
  /* Program memory past the first segment.  A part with more than 64 KByte of
   * it has a second segment, and that has to be a region of its own rather
   * than more of the one above: a far access carries one segment, so an object
   * may not straddle the boundary, and a near branch cannot cross it at all.
   * Code goes here with __attribute__((far, section(".fartext2"))), the same
   * way .psramtext takes code that runs from the PSRAM.  It is empty on a part
   * whose program memory fits in one segment, which is most of them. */
  farrom2 (rx) : ORIGIN = 0x00C10000, LENGTH = __c166_farrom2_length

  /* Static data goes in the DSRAM, which is a near address like everything
   * else here: the part reaches it through a data page pointer holding page 3,
   * which is what DPP3 already holds.
   *
   * Some derivatives have no DSRAM at all - the XC164xx-4F is 32 KByte of
   * Flash and none of it - and there static data goes at the bottom of the
   * DPRAM instead, with the ABI stack coming down from the top of the same
   * memory towards it.  That is the classic arrangement and it is what the
   * region below becomes when the part has nothing else. */
  dsram (rw) : ORIGIN = __c166_dsram_length ? 0x0000C000 : 0x0000F600,
               LENGTH = __c166_dsram_length ? __c166_dsram_length
                                            : __system_stack_limit - 0x0000F600
  /* The DPRAM under the system stack.  On a part with a DSRAM the ABI stack
   * and whatever __dpram places have it to themselves; on one without, the
   * region above is this same memory and the two names deliberately cover the
   * same bytes, because it is the same memory.  Nothing is placed twice: the
   * sections that go here take their address from the location counter, so
   * they follow the static data rather than starting under it, and the ASSERT
   * on __dprambss_end below is what checks the total either way.
   * Where the system stack starts is a choice rather than a part property, so
   * the ASSERTs below are what say the choice fits the part. */
  dpram (rw) : ORIGIN = 0x0000F600,
               LENGTH = __system_stack_limit - 0x0000F600
  /* The bit-addressable RAM: the 128 words from FD00H to FDFEH, which is what
   * a bit instruction can name and the only memory that is.  It sits above the
   * register bank at FC00H, in the part of this memory nothing else here uses,
   * so it is a region of its own rather than a slice of one above. */
  bitram (rw) : ORIGIN = 0x0000FD00, LENGTH = 256

  /* The interrupt banks: where __attribute__((c166_bank)) handlers get their
   * own sixteen registers.  The reset register bank is the sixteen words at
   * FC00H, so FC20H is the first free word above it, and FD00H is where the
   * bit-addressable RAM starts - 224 bytes, which is seven banks.  It has to
   * be this memory and not any other: a context pointer may only name the
   * internal RAM.  A program that wants an eighth bank gets a region overflow
   * naming this one. */
  banks (rw) : ORIGIN = 0x0000FC20, LENGTH = 0x00E0

  /* The PSRAM, which is the only RAM here that a near address cannot reach:
   * it is data page 896 and the four pointers are spoken for.  So this is
   * where __far data goes, and it is worth knowing that the manual calls it
   * optimised for code fetches and slower than the data memories for data - a
   * far access pays an EXTS on top of that.  It is for what does not fit. */
  psram (rw) : ORIGIN = 0x00E00000, LENGTH = __c166_psram_length
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
ASSERT(LENGTH(farrom2) == 0 ||
       (ORIGIN(farrom2) >> 16) == ((ORIGIN(farrom2) + LENGTH(farrom2) - 1) >> 16),
       "the farrom2 region crosses a segment boundary; this script covers two segments of program memory and this part has more")
ASSERT(__system_stack_top <= 0x0000F600 + __c166_dpram_length,
       "the system stack is above the end of this part's dual-port RAM")
ASSERT(__system_stack_limit >= 0x0000F600,
       "the system stack starts below the dual-port RAM")

ASSERT((ORIGIN(dsram) >> 16) == ((ORIGIN(dsram) + LENGTH(dsram) - 1) >> 16),
       "the dsram region crosses a segment boundary")
ASSERT((ORIGIN(dpram) >> 16) == ((ORIGIN(dpram) + LENGTH(dpram) - 1) >> 16),
       "the dpram region crosses a segment boundary")
ASSERT((ORIGIN(psram) >> 16) == ((ORIGIN(psram) + LENGTH(psram) - 1) >> 16),
       "the psram region crosses a segment boundary")

/* On a part with no extension RAM the dsram and dpram regions name the same
 * memory, so neither region's own overflow check sees the whole of what is in
 * it; this is what does.  How much room the ABI stack then has left is not a
 * thing a link can know, but running past where the stack starts is. */
ASSERT(__dprambss_end <= __user_stack_top,
       "the __dpram data does not fit under the ABI stack; the dual-port RAM holds both")
ASSERT(__dpramdata_start >= 0x0000F600,
       "the __dpram data starts below the dual-port RAM")

SECTIONS
{
  /* Trap 0 is reset, so the first four bytes of segment 0 are the jump the
   * part executes.  Anything else in the vector table goes here too. */
  .reset : {
    KEEP(*(.reset))

    /* The interrupt vector table.  A slot is one double word at 4 * the trap
     * number, and what goes in it is a jump rather than an address, because
     * TRAP and a hardware interrupt both branch to the slot instead of
     * reading through it.  __attribute__((interrupt(n))) emits one, and so do
     * the VECTOR macros in xc164cm-vectors.inc; either way the section is
     * named .vectors.NNN after the trap number in decimal, padded to three
     * digits.  Trap 0 is reset and is the KEEP above.
     *
     * The whole table is 512 bytes of ROM whether its slots are filled or
     * not, which a program with no handlers should not pay - so each row is
     * conditional on __c166_vector_table, a weak absolute symbol that
     * anything claiming a slot defines and nothing else does.  Without it
     * every row leaves the location counter where it found it and the table
     * takes no room at all.
     *
     * The location counter inside an output section is an absolute address
     * rather than an offset into the section, so each row names ORIGIN(rom):
     * the table is at the low end of the segment the part fetches vectors
     * from, which is where this script puts the ROM.
     *
     * This is the layout an XC164CM has at reset - CPUCON1.VECSC resets to
     * 00, which is two words between vectors, and VECSEG selects the segment
     * - and the only layout a C167 or an ST10 has, neither having those
     * registers.  A program that writes either one wants a different script.
     *
     * The three scripts here hold the same table, which
     * test/CodeGen/C166/vector-table-scripts.test checks: every row four
     * bytes above the last, all 127 present, and the three identical. */
    . = DEFINED(__c166_vector_table) ? ORIGIN(rom) + 0x004 : .; KEEP(*(.vectors.001))
    . = DEFINED(__c166_vector_table) ? ORIGIN(rom) + 0x008 : .; KEEP(*(.vectors.002))
    . = DEFINED(__c166_vector_table) ? ORIGIN(rom) + 0x00C : .; KEEP(*(.vectors.003))
    . = DEFINED(__c166_vector_table) ? ORIGIN(rom) + 0x010 : .; KEEP(*(.vectors.004))
    . = DEFINED(__c166_vector_table) ? ORIGIN(rom) + 0x014 : .; KEEP(*(.vectors.005))
    . = DEFINED(__c166_vector_table) ? ORIGIN(rom) + 0x018 : .; KEEP(*(.vectors.006))
    . = DEFINED(__c166_vector_table) ? ORIGIN(rom) + 0x01C : .; KEEP(*(.vectors.007))
    . = DEFINED(__c166_vector_table) ? ORIGIN(rom) + 0x020 : .; KEEP(*(.vectors.008))
    . = DEFINED(__c166_vector_table) ? ORIGIN(rom) + 0x024 : .; KEEP(*(.vectors.009))
    . = DEFINED(__c166_vector_table) ? ORIGIN(rom) + 0x028 : .; KEEP(*(.vectors.010))
    . = DEFINED(__c166_vector_table) ? ORIGIN(rom) + 0x02C : .; KEEP(*(.vectors.011))
    . = DEFINED(__c166_vector_table) ? ORIGIN(rom) + 0x030 : .; KEEP(*(.vectors.012))
    . = DEFINED(__c166_vector_table) ? ORIGIN(rom) + 0x034 : .; KEEP(*(.vectors.013))
    . = DEFINED(__c166_vector_table) ? ORIGIN(rom) + 0x038 : .; KEEP(*(.vectors.014))
    . = DEFINED(__c166_vector_table) ? ORIGIN(rom) + 0x03C : .; KEEP(*(.vectors.015))
    . = DEFINED(__c166_vector_table) ? ORIGIN(rom) + 0x040 : .; KEEP(*(.vectors.016))
    . = DEFINED(__c166_vector_table) ? ORIGIN(rom) + 0x044 : .; KEEP(*(.vectors.017))
    . = DEFINED(__c166_vector_table) ? ORIGIN(rom) + 0x048 : .; KEEP(*(.vectors.018))
    . = DEFINED(__c166_vector_table) ? ORIGIN(rom) + 0x04C : .; KEEP(*(.vectors.019))
    . = DEFINED(__c166_vector_table) ? ORIGIN(rom) + 0x050 : .; KEEP(*(.vectors.020))
    . = DEFINED(__c166_vector_table) ? ORIGIN(rom) + 0x054 : .; KEEP(*(.vectors.021))
    . = DEFINED(__c166_vector_table) ? ORIGIN(rom) + 0x058 : .; KEEP(*(.vectors.022))
    . = DEFINED(__c166_vector_table) ? ORIGIN(rom) + 0x05C : .; KEEP(*(.vectors.023))
    . = DEFINED(__c166_vector_table) ? ORIGIN(rom) + 0x060 : .; KEEP(*(.vectors.024))
    . = DEFINED(__c166_vector_table) ? ORIGIN(rom) + 0x064 : .; KEEP(*(.vectors.025))
    . = DEFINED(__c166_vector_table) ? ORIGIN(rom) + 0x068 : .; KEEP(*(.vectors.026))
    . = DEFINED(__c166_vector_table) ? ORIGIN(rom) + 0x06C : .; KEEP(*(.vectors.027))
    . = DEFINED(__c166_vector_table) ? ORIGIN(rom) + 0x070 : .; KEEP(*(.vectors.028))
    . = DEFINED(__c166_vector_table) ? ORIGIN(rom) + 0x074 : .; KEEP(*(.vectors.029))
    . = DEFINED(__c166_vector_table) ? ORIGIN(rom) + 0x078 : .; KEEP(*(.vectors.030))
    . = DEFINED(__c166_vector_table) ? ORIGIN(rom) + 0x07C : .; KEEP(*(.vectors.031))
    . = DEFINED(__c166_vector_table) ? ORIGIN(rom) + 0x080 : .; KEEP(*(.vectors.032))
    . = DEFINED(__c166_vector_table) ? ORIGIN(rom) + 0x084 : .; KEEP(*(.vectors.033))
    . = DEFINED(__c166_vector_table) ? ORIGIN(rom) + 0x088 : .; KEEP(*(.vectors.034))
    . = DEFINED(__c166_vector_table) ? ORIGIN(rom) + 0x08C : .; KEEP(*(.vectors.035))
    . = DEFINED(__c166_vector_table) ? ORIGIN(rom) + 0x090 : .; KEEP(*(.vectors.036))
    . = DEFINED(__c166_vector_table) ? ORIGIN(rom) + 0x094 : .; KEEP(*(.vectors.037))
    . = DEFINED(__c166_vector_table) ? ORIGIN(rom) + 0x098 : .; KEEP(*(.vectors.038))
    . = DEFINED(__c166_vector_table) ? ORIGIN(rom) + 0x09C : .; KEEP(*(.vectors.039))
    . = DEFINED(__c166_vector_table) ? ORIGIN(rom) + 0x0A0 : .; KEEP(*(.vectors.040))
    . = DEFINED(__c166_vector_table) ? ORIGIN(rom) + 0x0A4 : .; KEEP(*(.vectors.041))
    . = DEFINED(__c166_vector_table) ? ORIGIN(rom) + 0x0A8 : .; KEEP(*(.vectors.042))
    . = DEFINED(__c166_vector_table) ? ORIGIN(rom) + 0x0AC : .; KEEP(*(.vectors.043))
    . = DEFINED(__c166_vector_table) ? ORIGIN(rom) + 0x0B0 : .; KEEP(*(.vectors.044))
    . = DEFINED(__c166_vector_table) ? ORIGIN(rom) + 0x0B4 : .; KEEP(*(.vectors.045))
    . = DEFINED(__c166_vector_table) ? ORIGIN(rom) + 0x0B8 : .; KEEP(*(.vectors.046))
    . = DEFINED(__c166_vector_table) ? ORIGIN(rom) + 0x0BC : .; KEEP(*(.vectors.047))
    . = DEFINED(__c166_vector_table) ? ORIGIN(rom) + 0x0C0 : .; KEEP(*(.vectors.048))
    . = DEFINED(__c166_vector_table) ? ORIGIN(rom) + 0x0C4 : .; KEEP(*(.vectors.049))
    . = DEFINED(__c166_vector_table) ? ORIGIN(rom) + 0x0C8 : .; KEEP(*(.vectors.050))
    . = DEFINED(__c166_vector_table) ? ORIGIN(rom) + 0x0CC : .; KEEP(*(.vectors.051))
    . = DEFINED(__c166_vector_table) ? ORIGIN(rom) + 0x0D0 : .; KEEP(*(.vectors.052))
    . = DEFINED(__c166_vector_table) ? ORIGIN(rom) + 0x0D4 : .; KEEP(*(.vectors.053))
    . = DEFINED(__c166_vector_table) ? ORIGIN(rom) + 0x0D8 : .; KEEP(*(.vectors.054))
    . = DEFINED(__c166_vector_table) ? ORIGIN(rom) + 0x0DC : .; KEEP(*(.vectors.055))
    . = DEFINED(__c166_vector_table) ? ORIGIN(rom) + 0x0E0 : .; KEEP(*(.vectors.056))
    . = DEFINED(__c166_vector_table) ? ORIGIN(rom) + 0x0E4 : .; KEEP(*(.vectors.057))
    . = DEFINED(__c166_vector_table) ? ORIGIN(rom) + 0x0E8 : .; KEEP(*(.vectors.058))
    . = DEFINED(__c166_vector_table) ? ORIGIN(rom) + 0x0EC : .; KEEP(*(.vectors.059))
    . = DEFINED(__c166_vector_table) ? ORIGIN(rom) + 0x0F0 : .; KEEP(*(.vectors.060))
    . = DEFINED(__c166_vector_table) ? ORIGIN(rom) + 0x0F4 : .; KEEP(*(.vectors.061))
    . = DEFINED(__c166_vector_table) ? ORIGIN(rom) + 0x0F8 : .; KEEP(*(.vectors.062))
    . = DEFINED(__c166_vector_table) ? ORIGIN(rom) + 0x0FC : .; KEEP(*(.vectors.063))
    . = DEFINED(__c166_vector_table) ? ORIGIN(rom) + 0x100 : .; KEEP(*(.vectors.064))
    . = DEFINED(__c166_vector_table) ? ORIGIN(rom) + 0x104 : .; KEEP(*(.vectors.065))
    . = DEFINED(__c166_vector_table) ? ORIGIN(rom) + 0x108 : .; KEEP(*(.vectors.066))
    . = DEFINED(__c166_vector_table) ? ORIGIN(rom) + 0x10C : .; KEEP(*(.vectors.067))
    . = DEFINED(__c166_vector_table) ? ORIGIN(rom) + 0x110 : .; KEEP(*(.vectors.068))
    . = DEFINED(__c166_vector_table) ? ORIGIN(rom) + 0x114 : .; KEEP(*(.vectors.069))
    . = DEFINED(__c166_vector_table) ? ORIGIN(rom) + 0x118 : .; KEEP(*(.vectors.070))
    . = DEFINED(__c166_vector_table) ? ORIGIN(rom) + 0x11C : .; KEEP(*(.vectors.071))
    . = DEFINED(__c166_vector_table) ? ORIGIN(rom) + 0x120 : .; KEEP(*(.vectors.072))
    . = DEFINED(__c166_vector_table) ? ORIGIN(rom) + 0x124 : .; KEEP(*(.vectors.073))
    . = DEFINED(__c166_vector_table) ? ORIGIN(rom) + 0x128 : .; KEEP(*(.vectors.074))
    . = DEFINED(__c166_vector_table) ? ORIGIN(rom) + 0x12C : .; KEEP(*(.vectors.075))
    . = DEFINED(__c166_vector_table) ? ORIGIN(rom) + 0x130 : .; KEEP(*(.vectors.076))
    . = DEFINED(__c166_vector_table) ? ORIGIN(rom) + 0x134 : .; KEEP(*(.vectors.077))
    . = DEFINED(__c166_vector_table) ? ORIGIN(rom) + 0x138 : .; KEEP(*(.vectors.078))
    . = DEFINED(__c166_vector_table) ? ORIGIN(rom) + 0x13C : .; KEEP(*(.vectors.079))
    . = DEFINED(__c166_vector_table) ? ORIGIN(rom) + 0x140 : .; KEEP(*(.vectors.080))
    . = DEFINED(__c166_vector_table) ? ORIGIN(rom) + 0x144 : .; KEEP(*(.vectors.081))
    . = DEFINED(__c166_vector_table) ? ORIGIN(rom) + 0x148 : .; KEEP(*(.vectors.082))
    . = DEFINED(__c166_vector_table) ? ORIGIN(rom) + 0x14C : .; KEEP(*(.vectors.083))
    . = DEFINED(__c166_vector_table) ? ORIGIN(rom) + 0x150 : .; KEEP(*(.vectors.084))
    . = DEFINED(__c166_vector_table) ? ORIGIN(rom) + 0x154 : .; KEEP(*(.vectors.085))
    . = DEFINED(__c166_vector_table) ? ORIGIN(rom) + 0x158 : .; KEEP(*(.vectors.086))
    . = DEFINED(__c166_vector_table) ? ORIGIN(rom) + 0x15C : .; KEEP(*(.vectors.087))
    . = DEFINED(__c166_vector_table) ? ORIGIN(rom) + 0x160 : .; KEEP(*(.vectors.088))
    . = DEFINED(__c166_vector_table) ? ORIGIN(rom) + 0x164 : .; KEEP(*(.vectors.089))
    . = DEFINED(__c166_vector_table) ? ORIGIN(rom) + 0x168 : .; KEEP(*(.vectors.090))
    . = DEFINED(__c166_vector_table) ? ORIGIN(rom) + 0x16C : .; KEEP(*(.vectors.091))
    . = DEFINED(__c166_vector_table) ? ORIGIN(rom) + 0x170 : .; KEEP(*(.vectors.092))
    . = DEFINED(__c166_vector_table) ? ORIGIN(rom) + 0x174 : .; KEEP(*(.vectors.093))
    . = DEFINED(__c166_vector_table) ? ORIGIN(rom) + 0x178 : .; KEEP(*(.vectors.094))
    . = DEFINED(__c166_vector_table) ? ORIGIN(rom) + 0x17C : .; KEEP(*(.vectors.095))
    . = DEFINED(__c166_vector_table) ? ORIGIN(rom) + 0x180 : .; KEEP(*(.vectors.096))
    . = DEFINED(__c166_vector_table) ? ORIGIN(rom) + 0x184 : .; KEEP(*(.vectors.097))
    . = DEFINED(__c166_vector_table) ? ORIGIN(rom) + 0x188 : .; KEEP(*(.vectors.098))
    . = DEFINED(__c166_vector_table) ? ORIGIN(rom) + 0x18C : .; KEEP(*(.vectors.099))
    . = DEFINED(__c166_vector_table) ? ORIGIN(rom) + 0x190 : .; KEEP(*(.vectors.100))
    . = DEFINED(__c166_vector_table) ? ORIGIN(rom) + 0x194 : .; KEEP(*(.vectors.101))
    . = DEFINED(__c166_vector_table) ? ORIGIN(rom) + 0x198 : .; KEEP(*(.vectors.102))
    . = DEFINED(__c166_vector_table) ? ORIGIN(rom) + 0x19C : .; KEEP(*(.vectors.103))
    . = DEFINED(__c166_vector_table) ? ORIGIN(rom) + 0x1A0 : .; KEEP(*(.vectors.104))
    . = DEFINED(__c166_vector_table) ? ORIGIN(rom) + 0x1A4 : .; KEEP(*(.vectors.105))
    . = DEFINED(__c166_vector_table) ? ORIGIN(rom) + 0x1A8 : .; KEEP(*(.vectors.106))
    . = DEFINED(__c166_vector_table) ? ORIGIN(rom) + 0x1AC : .; KEEP(*(.vectors.107))
    . = DEFINED(__c166_vector_table) ? ORIGIN(rom) + 0x1B0 : .; KEEP(*(.vectors.108))
    . = DEFINED(__c166_vector_table) ? ORIGIN(rom) + 0x1B4 : .; KEEP(*(.vectors.109))
    . = DEFINED(__c166_vector_table) ? ORIGIN(rom) + 0x1B8 : .; KEEP(*(.vectors.110))
    . = DEFINED(__c166_vector_table) ? ORIGIN(rom) + 0x1BC : .; KEEP(*(.vectors.111))
    . = DEFINED(__c166_vector_table) ? ORIGIN(rom) + 0x1C0 : .; KEEP(*(.vectors.112))
    . = DEFINED(__c166_vector_table) ? ORIGIN(rom) + 0x1C4 : .; KEEP(*(.vectors.113))
    . = DEFINED(__c166_vector_table) ? ORIGIN(rom) + 0x1C8 : .; KEEP(*(.vectors.114))
    . = DEFINED(__c166_vector_table) ? ORIGIN(rom) + 0x1CC : .; KEEP(*(.vectors.115))
    . = DEFINED(__c166_vector_table) ? ORIGIN(rom) + 0x1D0 : .; KEEP(*(.vectors.116))
    . = DEFINED(__c166_vector_table) ? ORIGIN(rom) + 0x1D4 : .; KEEP(*(.vectors.117))
    . = DEFINED(__c166_vector_table) ? ORIGIN(rom) + 0x1D8 : .; KEEP(*(.vectors.118))
    . = DEFINED(__c166_vector_table) ? ORIGIN(rom) + 0x1DC : .; KEEP(*(.vectors.119))
    . = DEFINED(__c166_vector_table) ? ORIGIN(rom) + 0x1E0 : .; KEEP(*(.vectors.120))
    . = DEFINED(__c166_vector_table) ? ORIGIN(rom) + 0x1E4 : .; KEEP(*(.vectors.121))
    . = DEFINED(__c166_vector_table) ? ORIGIN(rom) + 0x1E8 : .; KEEP(*(.vectors.122))
    . = DEFINED(__c166_vector_table) ? ORIGIN(rom) + 0x1EC : .; KEEP(*(.vectors.123))
    . = DEFINED(__c166_vector_table) ? ORIGIN(rom) + 0x1F0 : .; KEEP(*(.vectors.124))
    . = DEFINED(__c166_vector_table) ? ORIGIN(rom) + 0x1F4 : .; KEEP(*(.vectors.125))
    . = DEFINED(__c166_vector_table) ? ORIGIN(rom) + 0x1F8 : .; KEEP(*(.vectors.126))
    . = DEFINED(__c166_vector_table) ? ORIGIN(rom) + 0x1FC : .; KEEP(*(.vectors.127))
    . = DEFINED(__c166_vector_table) ? ORIGIN(rom) + 0x200 : .;
  } > rom

  /* A vector goes in the slot named for its trap number.  ".vectors" with no
   * number was the spelling before the table above existed and has no slot of
   * its own now, so it is caught here and named rather than left to LLD's
   * orphan placement, which would put it somewhere plausible and wrong. */
  .vectors.unnumbered : {
    __c166_unnumbered_vectors_start = .;
    KEEP(*(.vectors))
    __c166_unnumbered_vectors_end = .;
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

  /* The second segment's share, for a part that has one. */
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
  } > dsram AT > rom
  __data_load = LOADADDR(.data);

  /* Sixteen words apiece, in internal RAM, for the handlers that asked for a
   * bank of their own.  Nothing initialises them - a handler's first use of a
   * register is a write, because the allocator had a whole empty bank - so
   * they are NOBITS and take no room in the image. */
  .c166_banks (NOLOAD) : ALIGN(2) {
    *(.c166_banks .c166_banks.*)
  } > banks

  .bss (NOLOAD) : ALIGN(2) {
    __bss_start = .;
    *(.bss .bss.*)
    *(COMMON)
    . = ALIGN(2);
    __bss_end = .;
  } > dsram

  /* What __dpram places, at the bottom of the dual-port RAM with the ABI stack
   * coming down towards it.  This memory is the only memory the coprocessor's
   * IDX0 and IDX1 can address - PM0036 section 2.1 - so an array a CoMAC walks
   * through [idx0+] has to be here and ordinary static data will not do.
   *
   * The address is MAX of the location counter and F600H rather than simply
   * the region's start, because which memory these belong in depends on the
   * part.  On a derivative with no extension RAM the dsram region above is
   * already this same dual-port RAM, so .bss has ended inside it and these
   * have to follow on; on a part with one, .bss ended at C800H and these start
   * at the bottom of the dual-port RAM instead.  A region cannot say "after
   * the one above, or at the start of this one" - and starting at F600H
   * regardless would put these under the static data on the first kind of
   * part, silently, since two regions over one memory is not an error.
   * Whichever address is higher is the first free word of this memory. */
  .dpramdata MAX(., 0x0000F600) : ALIGN(2) {
    __dpramdata_start = .;
    *(.dpramdata .dpramdata.*)
    . = ALIGN(2);
    __dpramdata_end = .;
  } > dpram AT > rom
  __dpramdata_load = LOADADDR(.dpramdata);

  .dprambss (NOLOAD) : ALIGN(2) {
    __dprambss_start = .;
    *(.dprambss .dprambss.*)
    . = ALIGN(2);
    __dprambss_end = .;
  } > dpram

  /* What __bitaddr places: the bit-addressable RAM, which is the 128 words
   * from FD00H to FDFEH and nothing else.  A bit instruction takes an 8 bit
   * word number of that space rather than an address, so an object anywhere
   * else has no bit address for one to name - the linker says so rather than
   * writing a byte that names some other word.
   *
   * It is above the register bank at FC00H and below the top of this memory,
   * which is the part of the dual-port RAM nothing else here uses.
   */
  .bitdata 0x0000FD00 : ALIGN(2) {
    __bitdata_start = .;
    *(.bitdata .bitdata.*)
    . = ALIGN(2);
    __bitdata_end = .;
  } > bitram AT > rom
  __bitdata_load = LOADADDR(.bitdata);

  .bitbss (NOLOAD) : ALIGN(2) {
    __bitbss_start = .;
    *(.bitbss .bitbss.*)
    . = ALIGN(2);
    __bitbss_end = .;
  } > bitram

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

  /* The ABI stack comes down from just under the system stack, whether what
   * is below it is an empty DPRAM, the __dpram data, or the static data of a
   * part with no DSRAM. */
  __user_stack_top = __system_stack_limit;
  /* Nothing here implements a heap; these say where one could go.  It starts
   * after the last thing placed rather than after .bss, which is what it used
   * to say: on a part with an extension RAM .bss ends at C800H and the memory
   * from there to the dual-port RAM is not RAM at all, so a heap that started
   * there began in nothing.  Now it is the room between what is placed in the
   * dual-port RAM and the ABI stack coming down. */
  /* Where the ABI stack must not go below.  There are two stacks on this part
   * and only one of them is guarded: STKOV and STKUN compare the hardware
   * stack pointer SP, which holds return addresses, and take a trap when it
   * leaves its window.  R0 is the other stack - frames, spills, locals, arrays
   * - and no register in the part watches it, so a program that recurses too
   * deep or puts a large array on the stack walks R0 down through whatever is
   * under it and corrupts that silently.
   *
   * This says where "under it" begins, so that something can check.  Two
   * things do: -mstack-check makes the compiler compare R0 against this symbol
   * in the prologue of any function whose frame is large enough to be worth
   * it, and llvm/utils/C166Sim reads this symbol out of the executable and
   * stops when R0 goes below it, whether or not the program was built with
   * that flag. */
  __user_stack_limit = __dprambss_end;
  __heap_start = __dprambss_end;
  __heap_end = __user_stack_top;

  /DISCARD/ : { *(.comment) *(.note.*) }
}

ASSERT(__user_stack_limit < __user_stack_top,
       "there is no room left for the ABI stack: what is placed below it reaches __user_stack_top")

/* The call frame information has to fit in a sixteen bit offset from its own
 * start, because that is how startup/unwind.c stores where an expression is -
 * see the note on Rule there, which is about the two Rows a walk holds and the
 * 1 KByte of ABI stack they have to fit in.  Nothing on any part in the table
 * comes close: the whole of the near program memory is 48 KByte. */
ASSERT(__eh_frame_end - __eh_frame_start <= 0xFFFF,
       "the call frame information is larger than the unwinder can index")

ASSERT(__c166_unnumbered_vectors_start == __c166_unnumbered_vectors_end,
       "a vector goes in .vectors.NNN, named for its trap number in decimal padded to three digits; plain .vectors has no slot")
