//===-- crt0.S - C166 reset vector and startup ----------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// What runs between reset and main() on a C166.  See README.txt in this
// directory for what to change for a particular part; the addresses this file
// uses all come from the linker script.
//
//===----------------------------------------------------------------------===//

// Trap 0 is reset, and a trap vector is one double word at 4 * trap number, so
// the reset vector is the first four bytes of code segment 0 and has room for
// exactly one jump.  JMPS is that jump, and it is four bytes.
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
        // a 10 bit page, which is what decides where a near address lands.
        // The linker script picks the pages, because it is what knows where
        // the memory is: on a part whose ROM is not in segment 0 these are not
        // the identity and not the reset values either.
        mov     dpp0, #__dpp0_page
        mov     dpp1, #__dpp1_page
        mov     dpp2, #__dpp2_page
        mov     dpp3, #__dpp3_page

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

        // The same two again for what is declared __far.  That RAM is outside
        // every page the data page pointers select - which is the whole point
        // of it - so the destination carries only its offset within the
        // segment and an EXTS supplies the segment for the store.  The source
        // is the ROM image, which is near like everything else in the ROM.
        //
        // EXTS covers the one instruction after it, so it goes inside the
        // loop rather than around it.
        mov     r1, #__fardata_load
        mov     r2, #sof(__fardata_start)
        mov     r3, #sof(__fardata_end)
.Lfarcopy:
        cmp     r2, r3
        jmpa    cc_z, .Lfarcopy_done
        mov     r4, [r1+]
        exts    #seg(__fardata_start), #1
        mov     [r2], r4
        add     r2, #2
        jmpa    cc_UC, .Lfarcopy
.Lfarcopy_done:

        mov     r1, #sof(__farbss_start)
        mov     r2, #sof(__farbss_end)
        mov     r3, #0
.Lfarzero:
        cmp     r1, r2
        jmpa    cc_z, .Lfarzero_done
        exts    #seg(__farbss_start), #1
        mov     [r1], r3
        add     r1, #2
        jmpa    cc_UC, .Lfarzero
.Lfarzero_done:

        // End of the initialisation phase.  After this DISWDT no longer works
        // and the protected system registers are locked, so anything that has
        // to change either of them has to have happened above.
        einit

        // main() takes no arguments here: there is no one to pass any.
        calla   cc_UC, main

        // Nothing to return to.  Hanging is more use on a bare part than
        // running off the end of memory.
        .globl __c166_exit
__c166_exit:
        jmpa    cc_UC, __c166_exit
        .size _start, .-_start
