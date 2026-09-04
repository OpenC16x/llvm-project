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

/* An interrupt bank has to be in the internal RAM, because that is the only
 * memory a context pointer may name.  On a part whose internal RAM stops
 * before the banks region does, there is nowhere to put one - which is a fact
 * about the part rather than about the program, so it is said here rather than
 * left to a region that reads as RAM and is not.  A program with no banked
 * handlers is not asked. */
ASSERT(SIZEOF(.c166_banks) == 0 ||
       ORIGIN(banks) + SIZEOF(.c166_banks) <= 0x0000F600 + __c166_iram_length,
       "an interrupt bank is above the end of this part's internal RAM")
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

  /* No program SRAM on this part; the XC16x's E0'0000H window is its own. */
  __psramtext_start = .;
  __psramtext_end = .;
  __psramtext_load = .;

  /* The ABI stack comes down through the internal RAM from just under the
   * system stack.  The static data is in the extension RAM rather than below
   * it, so the whole of this is the stack's and the heap follows the data. */
  __user_stack_top = __system_stack_limit;
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
  __heap_start = __bss_end;
  __heap_end = ORIGIN(xram) + LENGTH(xram);

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
