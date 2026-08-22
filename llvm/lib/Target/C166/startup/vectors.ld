/* The interrupt vector table.
 *
 * Include this in place of the .reset section of c166.ld when a program has
 * interrupt handlers; a program without any should leave it out, because the
 * table is 512 bytes of ROM whether its slots are filled or not.
 *
 *   SECTIONS
 *   {
 *     INCLUDE vectors.ld
 *     .text : { ... }
 *   }
 *
 * A slot is one double word at 4 * the trap number, and what goes in it is a
 * jump rather than an address, because TRAP and a hardware interrupt both
 * branch to the slot instead of reading through it.  So a handler is reached
 * by putting a JMPS in its own slot:
 *
 *   .section .vectors.026,"ax",@progbits
 *   jmps    #seg(t3_isr), sof(t3_isr)
 *
 * The number in the section name is the trap number in decimal, padded to
 * three digits.  Slots nothing claims stay zero.
 *
 * This is the layout an XC164CM has at reset: CPUCON1.VECSC resets to 00,
 * which is two words between vectors, and VECSEG selects the segment the
 * table lives in.  A program that writes either of them wants a different
 * script from this one.
 *
 * The table goes at the start of the rom region, because that is the segment
 * VECSEG names: on an XC164CM the Flash is at C0'0000H and VECSEG comes out of
 * reset holding C0H, so the reset vector is the first double word of Flash.
 *
 * Part of the LLVM Project, under the Apache License v2.0 with LLVM
 * Exceptions.  See https://llvm.org/LICENSE.txt for license information.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */

/* The location counter inside an output section is an absolute address, not an
 * offset into the section, so each slot names ORIGIN(rom) explicitly.  Slot n
 * is at 4n into the segment VECSEG names, which is where the part fetches
 * vector n from. */
.vectors ORIGIN(rom) : {
  /* Trap 0 is reset, and crt0.S puts its jump there. */
  KEEP(*(.reset))
  . = ORIGIN(rom) + 0x004; KEEP(*(.vectors.001))
  . = ORIGIN(rom) + 0x008; KEEP(*(.vectors.002))
  . = ORIGIN(rom) + 0x00C; KEEP(*(.vectors.003))
  . = ORIGIN(rom) + 0x010; KEEP(*(.vectors.004))
  . = ORIGIN(rom) + 0x014; KEEP(*(.vectors.005))
  . = ORIGIN(rom) + 0x018; KEEP(*(.vectors.006))
  . = ORIGIN(rom) + 0x01C; KEEP(*(.vectors.007))
  . = ORIGIN(rom) + 0x020; KEEP(*(.vectors.008))
  . = ORIGIN(rom) + 0x024; KEEP(*(.vectors.009))
  . = ORIGIN(rom) + 0x028; KEEP(*(.vectors.010))
  . = ORIGIN(rom) + 0x02C; KEEP(*(.vectors.011))
  . = ORIGIN(rom) + 0x030; KEEP(*(.vectors.012))
  . = ORIGIN(rom) + 0x034; KEEP(*(.vectors.013))
  . = ORIGIN(rom) + 0x038; KEEP(*(.vectors.014))
  . = ORIGIN(rom) + 0x03C; KEEP(*(.vectors.015))
  . = ORIGIN(rom) + 0x040; KEEP(*(.vectors.016))
  . = ORIGIN(rom) + 0x044; KEEP(*(.vectors.017))
  . = ORIGIN(rom) + 0x048; KEEP(*(.vectors.018))
  . = ORIGIN(rom) + 0x04C; KEEP(*(.vectors.019))
  . = ORIGIN(rom) + 0x050; KEEP(*(.vectors.020))
  . = ORIGIN(rom) + 0x054; KEEP(*(.vectors.021))
  . = ORIGIN(rom) + 0x058; KEEP(*(.vectors.022))
  . = ORIGIN(rom) + 0x05C; KEEP(*(.vectors.023))
  . = ORIGIN(rom) + 0x060; KEEP(*(.vectors.024))
  . = ORIGIN(rom) + 0x064; KEEP(*(.vectors.025))
  . = ORIGIN(rom) + 0x068; KEEP(*(.vectors.026))
  . = ORIGIN(rom) + 0x06C; KEEP(*(.vectors.027))
  . = ORIGIN(rom) + 0x070; KEEP(*(.vectors.028))
  . = ORIGIN(rom) + 0x074; KEEP(*(.vectors.029))
  . = ORIGIN(rom) + 0x078; KEEP(*(.vectors.030))
  . = ORIGIN(rom) + 0x07C; KEEP(*(.vectors.031))
  . = ORIGIN(rom) + 0x080; KEEP(*(.vectors.032))
  . = ORIGIN(rom) + 0x084; KEEP(*(.vectors.033))
  . = ORIGIN(rom) + 0x088; KEEP(*(.vectors.034))
  . = ORIGIN(rom) + 0x08C; KEEP(*(.vectors.035))
  . = ORIGIN(rom) + 0x090; KEEP(*(.vectors.036))
  . = ORIGIN(rom) + 0x094; KEEP(*(.vectors.037))
  . = ORIGIN(rom) + 0x098; KEEP(*(.vectors.038))
  . = ORIGIN(rom) + 0x09C; KEEP(*(.vectors.039))
  . = ORIGIN(rom) + 0x0A0; KEEP(*(.vectors.040))
  . = ORIGIN(rom) + 0x0A4; KEEP(*(.vectors.041))
  . = ORIGIN(rom) + 0x0A8; KEEP(*(.vectors.042))
  . = ORIGIN(rom) + 0x0AC; KEEP(*(.vectors.043))
  . = ORIGIN(rom) + 0x0B0; KEEP(*(.vectors.044))
  . = ORIGIN(rom) + 0x0B4; KEEP(*(.vectors.045))
  . = ORIGIN(rom) + 0x0B8; KEEP(*(.vectors.046))
  . = ORIGIN(rom) + 0x0BC; KEEP(*(.vectors.047))
  . = ORIGIN(rom) + 0x0C0; KEEP(*(.vectors.048))
  . = ORIGIN(rom) + 0x0C4; KEEP(*(.vectors.049))
  . = ORIGIN(rom) + 0x0C8; KEEP(*(.vectors.050))
  . = ORIGIN(rom) + 0x0CC; KEEP(*(.vectors.051))
  . = ORIGIN(rom) + 0x0D0; KEEP(*(.vectors.052))
  . = ORIGIN(rom) + 0x0D4; KEEP(*(.vectors.053))
  . = ORIGIN(rom) + 0x0D8; KEEP(*(.vectors.054))
  . = ORIGIN(rom) + 0x0DC; KEEP(*(.vectors.055))
  . = ORIGIN(rom) + 0x0E0; KEEP(*(.vectors.056))
  . = ORIGIN(rom) + 0x0E4; KEEP(*(.vectors.057))
  . = ORIGIN(rom) + 0x0E8; KEEP(*(.vectors.058))
  . = ORIGIN(rom) + 0x0EC; KEEP(*(.vectors.059))
  . = ORIGIN(rom) + 0x0F0; KEEP(*(.vectors.060))
  . = ORIGIN(rom) + 0x0F4; KEEP(*(.vectors.061))
  . = ORIGIN(rom) + 0x0F8; KEEP(*(.vectors.062))
  . = ORIGIN(rom) + 0x0FC; KEEP(*(.vectors.063))
  . = ORIGIN(rom) + 0x100; KEEP(*(.vectors.064))
  . = ORIGIN(rom) + 0x104; KEEP(*(.vectors.065))
  . = ORIGIN(rom) + 0x108; KEEP(*(.vectors.066))
  . = ORIGIN(rom) + 0x10C; KEEP(*(.vectors.067))
  . = ORIGIN(rom) + 0x110; KEEP(*(.vectors.068))
  . = ORIGIN(rom) + 0x114; KEEP(*(.vectors.069))
  . = ORIGIN(rom) + 0x118; KEEP(*(.vectors.070))
  . = ORIGIN(rom) + 0x11C; KEEP(*(.vectors.071))
  . = ORIGIN(rom) + 0x120; KEEP(*(.vectors.072))
  . = ORIGIN(rom) + 0x124; KEEP(*(.vectors.073))
  . = ORIGIN(rom) + 0x128; KEEP(*(.vectors.074))
  . = ORIGIN(rom) + 0x12C; KEEP(*(.vectors.075))
  . = ORIGIN(rom) + 0x130; KEEP(*(.vectors.076))
  . = ORIGIN(rom) + 0x134; KEEP(*(.vectors.077))
  . = ORIGIN(rom) + 0x138; KEEP(*(.vectors.078))
  . = ORIGIN(rom) + 0x13C; KEEP(*(.vectors.079))
  . = ORIGIN(rom) + 0x140; KEEP(*(.vectors.080))
  . = ORIGIN(rom) + 0x144; KEEP(*(.vectors.081))
  . = ORIGIN(rom) + 0x148; KEEP(*(.vectors.082))
  . = ORIGIN(rom) + 0x14C; KEEP(*(.vectors.083))
  . = ORIGIN(rom) + 0x150; KEEP(*(.vectors.084))
  . = ORIGIN(rom) + 0x154; KEEP(*(.vectors.085))
  . = ORIGIN(rom) + 0x158; KEEP(*(.vectors.086))
  . = ORIGIN(rom) + 0x15C; KEEP(*(.vectors.087))
  . = ORIGIN(rom) + 0x160; KEEP(*(.vectors.088))
  . = ORIGIN(rom) + 0x164; KEEP(*(.vectors.089))
  . = ORIGIN(rom) + 0x168; KEEP(*(.vectors.090))
  . = ORIGIN(rom) + 0x16C; KEEP(*(.vectors.091))
  . = ORIGIN(rom) + 0x170; KEEP(*(.vectors.092))
  . = ORIGIN(rom) + 0x174; KEEP(*(.vectors.093))
  . = ORIGIN(rom) + 0x178; KEEP(*(.vectors.094))
  . = ORIGIN(rom) + 0x17C; KEEP(*(.vectors.095))
  . = ORIGIN(rom) + 0x180; KEEP(*(.vectors.096))
  . = ORIGIN(rom) + 0x184; KEEP(*(.vectors.097))
  . = ORIGIN(rom) + 0x188; KEEP(*(.vectors.098))
  . = ORIGIN(rom) + 0x18C; KEEP(*(.vectors.099))
  . = ORIGIN(rom) + 0x190; KEEP(*(.vectors.100))
  . = ORIGIN(rom) + 0x194; KEEP(*(.vectors.101))
  . = ORIGIN(rom) + 0x198; KEEP(*(.vectors.102))
  . = ORIGIN(rom) + 0x19C; KEEP(*(.vectors.103))
  . = ORIGIN(rom) + 0x1A0; KEEP(*(.vectors.104))
  . = ORIGIN(rom) + 0x1A4; KEEP(*(.vectors.105))
  . = ORIGIN(rom) + 0x1A8; KEEP(*(.vectors.106))
  . = ORIGIN(rom) + 0x1AC; KEEP(*(.vectors.107))
  . = ORIGIN(rom) + 0x1B0; KEEP(*(.vectors.108))
  . = ORIGIN(rom) + 0x1B4; KEEP(*(.vectors.109))
  . = ORIGIN(rom) + 0x1B8; KEEP(*(.vectors.110))
  . = ORIGIN(rom) + 0x1BC; KEEP(*(.vectors.111))
  . = ORIGIN(rom) + 0x1C0; KEEP(*(.vectors.112))
  . = ORIGIN(rom) + 0x1C4; KEEP(*(.vectors.113))
  . = ORIGIN(rom) + 0x1C8; KEEP(*(.vectors.114))
  . = ORIGIN(rom) + 0x1CC; KEEP(*(.vectors.115))
  . = ORIGIN(rom) + 0x1D0; KEEP(*(.vectors.116))
  . = ORIGIN(rom) + 0x1D4; KEEP(*(.vectors.117))
  . = ORIGIN(rom) + 0x1D8; KEEP(*(.vectors.118))
  . = ORIGIN(rom) + 0x1DC; KEEP(*(.vectors.119))
  . = ORIGIN(rom) + 0x1E0; KEEP(*(.vectors.120))
  . = ORIGIN(rom) + 0x1E4; KEEP(*(.vectors.121))
  . = ORIGIN(rom) + 0x1E8; KEEP(*(.vectors.122))
  . = ORIGIN(rom) + 0x1EC; KEEP(*(.vectors.123))
  . = ORIGIN(rom) + 0x1F0; KEEP(*(.vectors.124))
  . = ORIGIN(rom) + 0x1F4; KEEP(*(.vectors.125))
  . = ORIGIN(rom) + 0x1F8; KEEP(*(.vectors.126))
  . = ORIGIN(rom) + 0x1FC; KEEP(*(.vectors.127))
  . = ORIGIN(rom) + 0x200;
} > rom
