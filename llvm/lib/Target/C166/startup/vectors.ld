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
 * Part of the LLVM Project, under the Apache License v2.0 with LLVM
 * Exceptions.  See https://llvm.org/LICENSE.txt for license information.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */

/* Offsets here are relative to the start of the section, which is placed at 0,
 * so each one is also the address the part fetches that vector from. */
.vectors 0x0000 : {
  /* Trap 0 is reset, and crt0.S puts its jump there. */
  KEEP(*(.reset))
  . = 0x004; KEEP(*(.vectors.001))
  . = 0x008; KEEP(*(.vectors.002))
  . = 0x00C; KEEP(*(.vectors.003))
  . = 0x010; KEEP(*(.vectors.004))
  . = 0x014; KEEP(*(.vectors.005))
  . = 0x018; KEEP(*(.vectors.006))
  . = 0x01C; KEEP(*(.vectors.007))
  . = 0x020; KEEP(*(.vectors.008))
  . = 0x024; KEEP(*(.vectors.009))
  . = 0x028; KEEP(*(.vectors.010))
  . = 0x02C; KEEP(*(.vectors.011))
  . = 0x030; KEEP(*(.vectors.012))
  . = 0x034; KEEP(*(.vectors.013))
  . = 0x038; KEEP(*(.vectors.014))
  . = 0x03C; KEEP(*(.vectors.015))
  . = 0x040; KEEP(*(.vectors.016))
  . = 0x044; KEEP(*(.vectors.017))
  . = 0x048; KEEP(*(.vectors.018))
  . = 0x04C; KEEP(*(.vectors.019))
  . = 0x050; KEEP(*(.vectors.020))
  . = 0x054; KEEP(*(.vectors.021))
  . = 0x058; KEEP(*(.vectors.022))
  . = 0x05C; KEEP(*(.vectors.023))
  . = 0x060; KEEP(*(.vectors.024))
  . = 0x064; KEEP(*(.vectors.025))
  . = 0x068; KEEP(*(.vectors.026))
  . = 0x06C; KEEP(*(.vectors.027))
  . = 0x070; KEEP(*(.vectors.028))
  . = 0x074; KEEP(*(.vectors.029))
  . = 0x078; KEEP(*(.vectors.030))
  . = 0x07C; KEEP(*(.vectors.031))
  . = 0x080; KEEP(*(.vectors.032))
  . = 0x084; KEEP(*(.vectors.033))
  . = 0x088; KEEP(*(.vectors.034))
  . = 0x08C; KEEP(*(.vectors.035))
  . = 0x090; KEEP(*(.vectors.036))
  . = 0x094; KEEP(*(.vectors.037))
  . = 0x098; KEEP(*(.vectors.038))
  . = 0x09C; KEEP(*(.vectors.039))
  . = 0x0A0; KEEP(*(.vectors.040))
  . = 0x0A4; KEEP(*(.vectors.041))
  . = 0x0A8; KEEP(*(.vectors.042))
  . = 0x0AC; KEEP(*(.vectors.043))
  . = 0x0B0; KEEP(*(.vectors.044))
  . = 0x0B4; KEEP(*(.vectors.045))
  . = 0x0B8; KEEP(*(.vectors.046))
  . = 0x0BC; KEEP(*(.vectors.047))
  . = 0x0C0; KEEP(*(.vectors.048))
  . = 0x0C4; KEEP(*(.vectors.049))
  . = 0x0C8; KEEP(*(.vectors.050))
  . = 0x0CC; KEEP(*(.vectors.051))
  . = 0x0D0; KEEP(*(.vectors.052))
  . = 0x0D4; KEEP(*(.vectors.053))
  . = 0x0D8; KEEP(*(.vectors.054))
  . = 0x0DC; KEEP(*(.vectors.055))
  . = 0x0E0; KEEP(*(.vectors.056))
  . = 0x0E4; KEEP(*(.vectors.057))
  . = 0x0E8; KEEP(*(.vectors.058))
  . = 0x0EC; KEEP(*(.vectors.059))
  . = 0x0F0; KEEP(*(.vectors.060))
  . = 0x0F4; KEEP(*(.vectors.061))
  . = 0x0F8; KEEP(*(.vectors.062))
  . = 0x0FC; KEEP(*(.vectors.063))
  . = 0x100; KEEP(*(.vectors.064))
  . = 0x104; KEEP(*(.vectors.065))
  . = 0x108; KEEP(*(.vectors.066))
  . = 0x10C; KEEP(*(.vectors.067))
  . = 0x110; KEEP(*(.vectors.068))
  . = 0x114; KEEP(*(.vectors.069))
  . = 0x118; KEEP(*(.vectors.070))
  . = 0x11C; KEEP(*(.vectors.071))
  . = 0x120; KEEP(*(.vectors.072))
  . = 0x124; KEEP(*(.vectors.073))
  . = 0x128; KEEP(*(.vectors.074))
  . = 0x12C; KEEP(*(.vectors.075))
  . = 0x130; KEEP(*(.vectors.076))
  . = 0x134; KEEP(*(.vectors.077))
  . = 0x138; KEEP(*(.vectors.078))
  . = 0x13C; KEEP(*(.vectors.079))
  . = 0x140; KEEP(*(.vectors.080))
  . = 0x144; KEEP(*(.vectors.081))
  . = 0x148; KEEP(*(.vectors.082))
  . = 0x14C; KEEP(*(.vectors.083))
  . = 0x150; KEEP(*(.vectors.084))
  . = 0x154; KEEP(*(.vectors.085))
  . = 0x158; KEEP(*(.vectors.086))
  . = 0x15C; KEEP(*(.vectors.087))
  . = 0x160; KEEP(*(.vectors.088))
  . = 0x164; KEEP(*(.vectors.089))
  . = 0x168; KEEP(*(.vectors.090))
  . = 0x16C; KEEP(*(.vectors.091))
  . = 0x170; KEEP(*(.vectors.092))
  . = 0x174; KEEP(*(.vectors.093))
  . = 0x178; KEEP(*(.vectors.094))
  . = 0x17C; KEEP(*(.vectors.095))
  . = 0x180; KEEP(*(.vectors.096))
  . = 0x184; KEEP(*(.vectors.097))
  . = 0x188; KEEP(*(.vectors.098))
  . = 0x18C; KEEP(*(.vectors.099))
  . = 0x190; KEEP(*(.vectors.100))
  . = 0x194; KEEP(*(.vectors.101))
  . = 0x198; KEEP(*(.vectors.102))
  . = 0x19C; KEEP(*(.vectors.103))
  . = 0x1A0; KEEP(*(.vectors.104))
  . = 0x1A4; KEEP(*(.vectors.105))
  . = 0x1A8; KEEP(*(.vectors.106))
  . = 0x1AC; KEEP(*(.vectors.107))
  . = 0x1B0; KEEP(*(.vectors.108))
  . = 0x1B4; KEEP(*(.vectors.109))
  . = 0x1B8; KEEP(*(.vectors.110))
  . = 0x1BC; KEEP(*(.vectors.111))
  . = 0x1C0; KEEP(*(.vectors.112))
  . = 0x1C4; KEEP(*(.vectors.113))
  . = 0x1C8; KEEP(*(.vectors.114))
  . = 0x1CC; KEEP(*(.vectors.115))
  . = 0x1D0; KEEP(*(.vectors.116))
  . = 0x1D4; KEEP(*(.vectors.117))
  . = 0x1D8; KEEP(*(.vectors.118))
  . = 0x1DC; KEEP(*(.vectors.119))
  . = 0x1E0; KEEP(*(.vectors.120))
  . = 0x1E4; KEEP(*(.vectors.121))
  . = 0x1E8; KEEP(*(.vectors.122))
  . = 0x1EC; KEEP(*(.vectors.123))
  . = 0x1F0; KEEP(*(.vectors.124))
  . = 0x1F4; KEEP(*(.vectors.125))
  . = 0x1F8; KEEP(*(.vectors.126))
  . = 0x1FC; KEEP(*(.vectors.127))
  . = 0x200;
} > rom
