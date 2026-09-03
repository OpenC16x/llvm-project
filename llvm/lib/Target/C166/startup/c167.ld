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
   * is the only memory the multiply-accumulate unit's IDX pointers reach, so it
   * is where __dpram places things as well as where the ABI stack lives.  On a
   * part with no extension RAM the region above is this same memory and the
   * two names deliberately cover the same bytes; nothing is placed twice,
   * because what goes here takes its address from the location counter and so
   * follows the static data rather than starting under it. */
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
ASSERT(__dpramdata_start >= 0x0000F600,
       "the __dpram data starts below the internal RAM")

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

  /* What __dpram places, at the bottom of the internal RAM with the ABI stack
   * coming down towards it.  This memory is the only memory the coprocessor's
   * IDX0 and IDX1 can address - PM0036 section 2.1 - so an array a CoMAC walks
   * through [idx0+] has to be here and ordinary static data will not do.
   *
   * MAX of the two says where without a condition: on a part with an extension
   * RAM .bss ended at E000H and this starts at the bottom of the internal RAM,
   * and on one without .bss is already in this memory and this follows it.
   * Whichever is higher is the first free word here. */
  .dpramdata MAX(., 0x0000F600) : ALIGN(2) {
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
  /* Nothing here implements a heap; these say where one could go.  After the
   * last thing placed rather than after .bss, because on a part with an
   * extension RAM the memory between E800H and the internal RAM is not RAM. */
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
