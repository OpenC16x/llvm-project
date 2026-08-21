/* A default linker script for a C166 part.
 *
 * The memory map below is the one an SAB 83C166 comes up with: 8 KByte of
 * internal ROM at the bottom of code segment 0, and 1 KByte of internal RAM
 * at 00'FA00H.  Almost every board has more than that; see README.txt in this
 * directory for what to change.
 *
 * Part of the LLVM Project, under the Apache License v2.0 with LLVM
 * Exceptions.  See https://llvm.org/LICENSE.txt for license information.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */

OUTPUT_FORMAT(elf32-c166)
ENTRY(__reset_vector)

/* The internal RAM runs from 00'FA00H to 00'FDFFH and is shared between the
 * system stack, the register bank and everything else.  These three are the
 * values SP, STKUN, STKOV and CP hold after reset, so the layout below is the
 * one the hardware already assumes: the system stack grows down from FC00H
 * into FA00H, and R0 to R15 sit at FC00H just above it.
 */
__system_stack_top   = 0xFC00;
__system_stack_limit = 0xFA00;
__register_bank      = 0xFC00;

MEMORY
{
  rom (rx) : ORIGIN = 0x00000000, LENGTH = 8K
  /* What is left of the internal RAM once the stack and the register bank
   * have had their share. */
  ram (rw) : ORIGIN = 0x0000FC20, LENGTH = 480
}

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
