/* The C167's interrupt vector numbers.
 *
 * A handler claims its slot by name rather than by number:
 *
 *   #include "c167-vectors.inc"
 *   VECTOR_S0RIC  uart_rx_isr
 *
 * which puts a jump to uart_rx_isr in the slot ASC0 raises on a received
 * character.  vectors.ld places the slot and works unchanged for this part:
 * it puts vector n at ORIGIN(rom) + 4n, and c167.ld's rom starts at 00'0000H,
 * which is where a C167's vector table is.
 *
 * Claiming a slot by number works too, and the number is the decimal one:
 *
 *   VECTOR 043, uart_rx_isr
 *
 * The point of the named form is that a handler wired to the wrong source is
 * not a failure anyone sees quickly.
 *
 * Every row agrees with itself - the vector location is four times the vector
 * number in all 56 - and the numbers run from 10H to 47H with none missing.
 *
 * The macro is named for the interrupt control register of the source, which
 * is the manual's IR and IE prefix with IC in place of them: CC0IR and CC0IE
 * belong to CC0IC, so the macro is VECTOR_CC0IC.  Not all of those registers
 * are in the register file - the CAPCOM2 ones are not - which costs nothing
 * here, because a vector number is a number and this file names no register.
 *
 * The hardware traps are at the end, and they agree with themselves the same
 * way: the location is four times the trap number in all five.  They occupy
 * every other slot rather than consecutive ones.
 *
 * Between the two the vector space accounts for itself completely: 00H to 0AH
 * are the traps, 0BH to 0FH are reserved - which is locations 2CH to 3CH, as
 * the table gives them - and 10H to 47H are the interrupts.
 *
 * One slot is not spoken for: trap number 08H, which is location 00'0020H.
 * The trap table lists it neither as a trap nor among the reserved numbers,
 * and where an XC164CM has its software break trap this part has nothing
 * named, so nothing here claims it.  A program that knows better uses
 * VECTOR 008 directly.
 *
 * Everything in this file is a trap number rather than a location, because
 * that is what the section names and the VECTOR macro take.  The two differ
 * by a factor of four and the comments give both where it matters.
 *
 * The TRAP instruction reaches any of 00H to 7FH, so a software trap can enter
 * a slot this file names or one it does not.
 *
 * Part of the LLVM Project, under the Apache License v2.0 with LLVM
 * Exceptions.  See https://llvm.org/LICENSE.txt for license information.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */

/* Claim a slot by number.  The number is decimal, padded to three digits,
   which is what vectors.ld places. */
.macro VECTOR num, handler
        .section .vectors.\num,"ax",@progbits
        jmps    #seg(\handler), sof(\handler)
.endm


/* CAPCOM Register 0, at 10H in the table. */
.macro VECTOR_CC0IC handler
        VECTOR 016, \handler
.endm

/* CAPCOM Register 1, at 11H in the table. */
.macro VECTOR_CC1IC handler
        VECTOR 017, \handler
.endm

/* CAPCOM Register 2, at 12H in the table. */
.macro VECTOR_CC2IC handler
        VECTOR 018, \handler
.endm

/* CAPCOM Register 3, at 13H in the table. */
.macro VECTOR_CC3IC handler
        VECTOR 019, \handler
.endm

/* CAPCOM Register 4, at 14H in the table. */
.macro VECTOR_CC4IC handler
        VECTOR 020, \handler
.endm

/* CAPCOM Register 5, at 15H in the table. */
.macro VECTOR_CC5IC handler
        VECTOR 021, \handler
.endm

/* CAPCOM Register 6, at 16H in the table. */
.macro VECTOR_CC6IC handler
        VECTOR 022, \handler
.endm

/* CAPCOM Register 7, at 17H in the table. */
.macro VECTOR_CC7IC handler
        VECTOR 023, \handler
.endm

/* CAPCOM Register 8, at 18H in the table. */
.macro VECTOR_CC8IC handler
        VECTOR 024, \handler
.endm

/* CAPCOM Register 9, at 19H in the table. */
.macro VECTOR_CC9IC handler
        VECTOR 025, \handler
.endm

/* CAPCOM Register 10, at 1AH in the table. */
.macro VECTOR_CC10IC handler
        VECTOR 026, \handler
.endm

/* CAPCOM Register 11, at 1BH in the table. */
.macro VECTOR_CC11IC handler
        VECTOR 027, \handler
.endm

/* CAPCOM Register 12, at 1CH in the table. */
.macro VECTOR_CC12IC handler
        VECTOR 028, \handler
.endm

/* CAPCOM Register 13, at 1DH in the table. */
.macro VECTOR_CC13IC handler
        VECTOR 029, \handler
.endm

/* CAPCOM Register 14, at 1EH in the table. */
.macro VECTOR_CC14IC handler
        VECTOR 030, \handler
.endm

/* CAPCOM Register 15, at 1FH in the table. */
.macro VECTOR_CC15IC handler
        VECTOR 031, \handler
.endm

/* CAPCOM Timer 0, at 20H in the table. */
.macro VECTOR_T0IC handler
        VECTOR 032, \handler
.endm

/* CAPCOM Timer 1, at 21H in the table. */
.macro VECTOR_T1IC handler
        VECTOR 033, \handler
.endm

/* GPT1 Timer 2, at 22H in the table. */
.macro VECTOR_T2IC handler
        VECTOR 034, \handler
.endm

/* GPT1 Timer 3, at 23H in the table. */
.macro VECTOR_T3IC handler
        VECTOR 035, \handler
.endm

/* GPT1 Timer 4, at 24H in the table. */
.macro VECTOR_T4IC handler
        VECTOR 036, \handler
.endm

/* GPT2 Timer 5, at 25H in the table. */
.macro VECTOR_T5IC handler
        VECTOR 037, \handler
.endm

/* GPT2 Timer 6, at 26H in the table. */
.macro VECTOR_T6IC handler
        VECTOR 038, \handler
.endm

/* GPT2 CAPREL Register, at 27H in the table. */
.macro VECTOR_CRIC handler
        VECTOR 039, \handler
.endm

/* A/D Conversion Complete, at 28H in the table. */
.macro VECTOR_ADCIC handler
        VECTOR 040, \handler
.endm

/* A/D Overrun Error, at 29H in the table. */
.macro VECTOR_ADEIC handler
        VECTOR 041, \handler
.endm

/* ASC0 Transmit, at 2AH in the table. */
.macro VECTOR_S0TIC handler
        VECTOR 042, \handler
.endm

/* ASC0 Receive, at 2BH in the table. */
.macro VECTOR_S0RIC handler
        VECTOR 043, \handler
.endm

/* ASC0 Error, at 2CH in the table. */
.macro VECTOR_S0EIC handler
        VECTOR 044, \handler
.endm

/* SSC Transmit, at 2DH in the table. */
.macro VECTOR_SSCTIC handler
        VECTOR 045, \handler
.endm

/* SSC Receive, at 2EH in the table. */
.macro VECTOR_SSCRIC handler
        VECTOR 046, \handler
.endm

/* SSC Error, at 2FH in the table. */
.macro VECTOR_SSCEIC handler
        VECTOR 047, \handler
.endm

/* CAPCOM Register 16, at 30H in the table. */
.macro VECTOR_CC16IC handler
        VECTOR 048, \handler
.endm

/* CAPCOM Register 17, at 31H in the table. */
.macro VECTOR_CC17IC handler
        VECTOR 049, \handler
.endm

/* CAPCOM Register 18, at 32H in the table. */
.macro VECTOR_CC18IC handler
        VECTOR 050, \handler
.endm

/* CAPCOM Register 19, at 33H in the table. */
.macro VECTOR_CC19IC handler
        VECTOR 051, \handler
.endm

/* CAPCOM Register 20, at 34H in the table. */
.macro VECTOR_CC20IC handler
        VECTOR 052, \handler
.endm

/* CAPCOM Register 21, at 35H in the table. */
.macro VECTOR_CC21IC handler
        VECTOR 053, \handler
.endm

/* CAPCOM Register 22, at 36H in the table. */
.macro VECTOR_CC22IC handler
        VECTOR 054, \handler
.endm

/* CAPCOM Register 23, at 37H in the table. */
.macro VECTOR_CC23IC handler
        VECTOR 055, \handler
.endm

/* CAPCOM Register 24, at 38H in the table. */
.macro VECTOR_CC24IC handler
        VECTOR 056, \handler
.endm

/* CAPCOM Register 25, at 39H in the table. */
.macro VECTOR_CC25IC handler
        VECTOR 057, \handler
.endm

/* CAPCOM Register 26, at 3AH in the table. */
.macro VECTOR_CC26IC handler
        VECTOR 058, \handler
.endm

/* CAPCOM Register 27, at 3BH in the table. */
.macro VECTOR_CC27IC handler
        VECTOR 059, \handler
.endm

/* CAPCOM Register 28, at 3CH in the table. */
.macro VECTOR_CC28IC handler
        VECTOR 060, \handler
.endm

/* CAPCOM Timer 7, at 3DH in the table. */
.macro VECTOR_T7IC handler
        VECTOR 061, \handler
.endm

/* CAPCOM Timer 8, at 3EH in the table. */
.macro VECTOR_T8IC handler
        VECTOR 062, \handler
.endm

/* PWM Channel 0...3, at 3FH in the table. */
.macro VECTOR_PWMIC handler
        VECTOR 063, \handler
.endm

/* CAN Interface, at 40H in the table. */
.macro VECTOR_XP0IC handler
        VECTOR 064, \handler
.endm

/* X-Peripheral Node 1, at 41H in the table. */
.macro VECTOR_XP1IC handler
        VECTOR 065, \handler
.endm

/* X-Peripheral Node 2, at 42H in the table. */
.macro VECTOR_XP2IC handler
        VECTOR 066, \handler
.endm

/* PLL Unlock, at 43H in the table. */
.macro VECTOR_XP3IC handler
        VECTOR 067, \handler
.endm

/* CAPCOM Register 29, at 44H in the table. */
.macro VECTOR_CC29IC handler
        VECTOR 068, \handler
.endm

/* CAPCOM Register 30, at 45H in the table. */
.macro VECTOR_CC30IC handler
        VECTOR 069, \handler
.endm

/* CAPCOM Register 31, at 46H in the table. */
.macro VECTOR_CC31IC handler
        VECTOR 070, \handler
.endm

/* ASC0 Transmit Buffer, at 47H in the table. */
.macro VECTOR_S0TBIC handler
        VECTOR 071, \handler
.endm


/* The hardware traps.  Vector 0 is reset, which crt0.S claims with its own
   section, so there is no macro for it. */

/* Non-maskable interrupt, at 02H in the table. */
.macro VECTOR_NMITRAP handler
        VECTOR 002, \handler
.endm

/* Stack overflow, at 04H in the table. */
.macro VECTOR_STOTRAP handler
        VECTOR 004, \handler
.endm

/* Stack underflow, at 06H in the table. */
.macro VECTOR_STUTRAP handler
        VECTOR 006, \handler
.endm

/* Undefined opcode, protected instruction fault, illegal word operand access,
   illegal instruction access or illegal external bus access.  All five raise
   this one vector and TFR says which of them it was, at 0AH in the table.
   The last two are this part's own: an XC164CM has no external bus to make an
   illegal access to, and raises a PMI access error here instead. */
.macro VECTOR_BTRAP handler
        VECTOR 010, \handler
.endm
