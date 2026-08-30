//===-- c167-crt0.S - C167 reset vector and startup -----------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// What runs between reset and main() on a C167, to go with c167.ld.  It is
// crt0.S with two things taken out and nothing put in:
//
//   - There is no PLL to program.  A C167 comes out of reset on the clock its
//     CLKCFG pins selected, which is a board's wiring rather than a register,
//     so there is nothing for software to set up and nothing to wait for.
//     The XC164CM's crt0 spends half its length on that.
//
//   - There is no RAM outside the data page pointers, so nothing has to be
//     copied or zeroed with an EXTS.  What a C167 has past the ROM is the
//     internal RAM at 00'F600H and the extension RAM at 00'E000H, and both are
//     page 3, which DPP3 reaches.
//
// What it adds is one BSET: the extension RAM is an X-peripheral and has to be
// enabled before anything is written to it.
//
// The addresses this file uses all come from the linker script.
//
//===----------------------------------------------------------------------===//

// Trap 0 is reset, and a trap vector is one double word at 4 * trap number, so
// the reset vector is the first four bytes of code segment 0 and has room for
// exactly one jump.  JMPS is that jump, and it is four bytes.
//
// On this part those four bytes are the bottom of the on-chip ROM, so a part
// with ROM comes up executing them with nothing having configured it first.
        .section .reset,"ax",@progbits
        .globl __reset_vector
__reset_vector:
        jmps    #seg(_start), sof(_start)

        .text
        .globl _start
        .type _start,@function
_start:
        // The watchdog is running out of reset and will reset the part again
        // if nothing stops it.  DISWDT only works between reset and EINIT, so
        // it has to be the first thing here.
        diswdt

        // The system stack holds return addresses and grows down.  SP, STKUN
        // and STKOV come up holding the values below on a part with the
        // internal RAM at 00'FA00H; a board with its stack somewhere else
        // changes them in the linker script and this writes them.
        mov     sp, #__system_stack_top
        mov     stkun, #__system_stack_top
        mov     stkov, #__system_stack_limit

        // The data page pointers map the top two bits of a 16 bit address onto
        // a 10 bit page.  With the program memory in segment 0 the pages the
        // linker script picks are 0, 1, 2 and 3, which is what these already
        // hold after reset - so unlike the XC164CM this writes what is there.
        // Four instructions once, against a reader having to know that the
        // reset values happen to be right.
        mov     dpp0, #__dpp0_page
        mov     dpp1, #__dpp1_page
        mov     dpp2, #__dpp2_page
        mov     dpp3, #__dpp3_page

        // The extension RAM is an X-peripheral, and SYSCON.XPEN is what turns
        // those on: XPERCON comes up selecting the 2 KByte XRAM, and this is
        // the bit that makes the selection take effect.  It has to happen
        // before the copy below, which writes static data into that RAM, and
        // it has to happen before EINIT, which locks SYSCON.
        //
        // SYSCON is at FF12H and so is bit addressable, which is why this is
        // one BSET rather than a read-modify-write of a register whose other
        // bits the reset pins set and nothing here should disturb.
        //
        // The linker script says whether to: there is nothing to enable on a
        // part with no extension RAM, and a board that knows its own answer
        // defines __c166_enable_xper itself.
        mov     r1, #__c166_enable_xper
        cmp     r1, #0
        jmpa    cc_z, .Lno_xper
        bset    syscon.2
.Lno_xper:

        // CP is left where reset put it.  It says where R0 to R15 are, so an
        // instruction that moves it changes the meaning of every register
        // operand around it; a program that wants a different bank is better
        // off doing it deliberately than having a crt0 do it silently.

        // R0 is the ABI stack pointer, which also grows down.
        mov     r0, #__user_stack_top

        // Copy the initialised data from the image the linker left in ROM.
        // Both ends are word aligned by the linker script, so this moves
        // whole words.
        mov     r1, #__data_load
        mov     r2, #__data_start
        mov     r3, #__data_end
.Lcopy:
        cmp     r2, r3
        jmpa    cc_z, .Lcopy_done
        mov     r4, [r1+]
        mov     [r2], r4
        add     r2, #2
        jmpa    cc_UC, .Lcopy
.Lcopy_done:

        // Zero the uninitialised data, word aligned the same way.
        mov     r1, #__bss_start
        mov     r2, #__bss_end
        mov     r3, #0
.Lzero:
        cmp     r1, r2
        jmpa    cc_z, .Lzero_done
        mov     [r1], r3
        add     r1, #2
        jmpa    cc_UC, .Lzero
.Lzero_done:

        // End of the initialisation phase.  After this DISWDT no longer works
        // and the protected system registers are locked, so anything that has
        // to change either of them - the bus configuration for a board with
        // external memory, above all - has to have happened above.
        einit

        // main() takes no arguments here: there is no one to pass any.
        calla   cc_UC, main

        // Nothing to return to.  Hanging is more use on a bare part than
        // running off the end of memory.
        .globl __c166_exit
__c166_exit:
        jmpa    cc_UC, __c166_exit
        .size _start, .-_start
