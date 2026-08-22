/* The XC164CM's interrupt vector numbers.
 *
 * A handler claims its slot by name rather than by number:
 *
 *   #include "xc164cm-vectors.inc"
 *   VECTOR_ASC0_RIC  uart_rx_isr
 *
 * which puts a jump to uart_rx_isr in the slot ASC0 raises on a received
 * character.  vectors.ld places the slot; see README.txt in this directory.
 *
 * Claiming a slot by number works too, and the number is the decimal one:
 *
 *   VECTOR 043, uart_rx_isr
 *
 * The point of the named form is that a handler wired to the wrong source is
 * not a failure anyone sees quickly.
 *
 * From Table 5-2 of the XC164CM User's Manual.  Every row of it agrees with
 * itself - the vector location is four times the vector number in all 80 - and
 * the numbers run from 10H to 5FH with none missing.
 *
 * The hardware traps below are Table 5-3 of the same manual, and agree with
 * themselves the same way.  They occupy every other slot rather than
 * consecutive ones.
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


/* EX0IN, at 18H in the table. */
.macro VECTOR_CC1_CC8IC handler
        VECTOR 024, \handler
.endm

/* EX1IN, at 19H in the table. */
.macro VECTOR_CC1_CC9IC handler
        VECTOR 025, \handler
.endm

/* EX2IN, at 1AH in the table. */
.macro VECTOR_CC1_CC10IC handler
        VECTOR 026, \handler
.endm

/* EX3IN, at 1BH in the table. */
.macro VECTOR_CC1_CC11IC handler
        VECTOR 027, \handler
.endm

/* EX4IN, at 1CH in the table. */
.macro VECTOR_CC1_CC12IC handler
        VECTOR 028, \handler
.endm

/* EX5IN, at 1DH in the table. */
.macro VECTOR_CC1_CC13IC handler
        VECTOR 029, \handler
.endm

/* CAPCOM Register 16, at 30H in the table. */
.macro VECTOR_CC2_CC16IC handler
        VECTOR 048, \handler
.endm

/* CAPCOM Register 17, at 31H in the table. */
.macro VECTOR_CC2_CC17IC handler
        VECTOR 049, \handler
.endm

/* CAPCOM Register 18, at 32H in the table. */
.macro VECTOR_CC2_CC18IC handler
        VECTOR 050, \handler
.endm

/* CAPCOM Register 19, at 33H in the table. */
.macro VECTOR_CC2_CC19IC handler
        VECTOR 051, \handler
.endm

/* CAPCOM Register 20, at 34H in the table. */
.macro VECTOR_CC2_CC20IC handler
        VECTOR 052, \handler
.endm

/* CAPCOM Register 21, at 35H in the table. */
.macro VECTOR_CC2_CC21IC handler
        VECTOR 053, \handler
.endm

/* CAPCOM Register 22, at 36H in the table. */
.macro VECTOR_CC2_CC22IC handler
        VECTOR 054, \handler
.endm

/* CAPCOM Register 23, at 37H in the table. */
.macro VECTOR_CC2_CC23IC handler
        VECTOR 055, \handler
.endm

/* CAPCOM Register 24, at 38H in the table. */
.macro VECTOR_CC2_CC24IC handler
        VECTOR 056, \handler
.endm

/* CAPCOM Register 25, at 39H in the table. */
.macro VECTOR_CC2_CC25IC handler
        VECTOR 057, \handler
.endm

/* CAPCOM Register 26, at 3AH in the table. */
.macro VECTOR_CC2_CC26IC handler
        VECTOR 058, \handler
.endm

/* CAPCOM Register 27, at 3BH in the table. */
.macro VECTOR_CC2_CC27IC handler
        VECTOR 059, \handler
.endm

/* CAPCOM Register 28, at 3CH in the table. */
.macro VECTOR_CC2_CC28IC handler
        VECTOR 060, \handler
.endm

/* CAPCOM Register 29, at 44H in the table. */
.macro VECTOR_CC2_CC29IC handler
        VECTOR 068, \handler
.endm

/* CAPCOM Register 30, at 45H in the table. */
.macro VECTOR_CC2_CC30IC handler
        VECTOR 069, \handler
.endm

/* CAPCOM Register 31, at 46H in the table. */
.macro VECTOR_CC2_CC31IC handler
        VECTOR 070, \handler
.endm

/* CAPCOM Timer 7, at 3DH in the table. */
.macro VECTOR_CC2_T7IC handler
        VECTOR 061, \handler
.endm

/* CAPCOM Timer 8, at 3EH in the table. */
.macro VECTOR_CC2_T8IC handler
        VECTOR 062, \handler
.endm

/* GPT1 Timer 2, at 22H in the table. */
.macro VECTOR_GPT12E_T2IC handler
        VECTOR 034, \handler
.endm

/* GPT1 Timer 3, at 23H in the table. */
.macro VECTOR_GPT12E_T3IC handler
        VECTOR 035, \handler
.endm

/* GPT1 Timer 4, at 24H in the table. */
.macro VECTOR_GPT12E_T4IC handler
        VECTOR 036, \handler
.endm

/* GPT2 Timer 5, at 25H in the table. */
.macro VECTOR_GPT12E_T5IC handler
        VECTOR 037, \handler
.endm

/* GPT2 Timer 6, at 26H in the table. */
.macro VECTOR_GPT12E_T6IC handler
        VECTOR 038, \handler
.endm

/* GPT2 CAPREL Reg., at 27H in the table. */
.macro VECTOR_GPT12E_CRIC handler
        VECTOR 039, \handler
.endm

/* A/D Conversion Compl., at 28H in the table. */
.macro VECTOR_ADC_CIC handler
        VECTOR 040, \handler
.endm

/* A/D Overrun Error, at 29H in the table. */
.macro VECTOR_ADC_EIC handler
        VECTOR 041, \handler
.endm

/* ASC0 Transmit, at 2AH in the table. */
.macro VECTOR_ASC0_TIC handler
        VECTOR 042, \handler
.endm

/* ASC0 Transmit Buffer, at 47H in the table. */
.macro VECTOR_ASC0_TBIC handler
        VECTOR 071, \handler
.endm

/* ASC0 Receive, at 2BH in the table. */
.macro VECTOR_ASC0_RIC handler
        VECTOR 043, \handler
.endm

/* ASC0 Error, at 2CH in the table. */
.macro VECTOR_ASC0_EIC handler
        VECTOR 044, \handler
.endm

/* ASC0 Autobaud, at 5FH in the table. */
.macro VECTOR_ASC0_ABIC handler
        VECTOR 095, \handler
.endm

/* SSC0 Transmit, at 2DH in the table. */
.macro VECTOR_SSC0_TIC handler
        VECTOR 045, \handler
.endm

/* SSC0 Receive, at 2EH in the table. */
.macro VECTOR_SSC0_RIC handler
        VECTOR 046, \handler
.endm

/* SSC0 Error, at 2FH in the table. */
.macro VECTOR_SSC0_EIC handler
        VECTOR 047, \handler
.endm

/* PLL/OWD, at 43H in the table. */
.macro VECTOR_PLL_IC handler
        VECTOR 067, \handler
.endm

/* ASC1 Transmit, at 48H in the table. */
.macro VECTOR_ASC1_TIC handler
        VECTOR 072, \handler
.endm

/* ASC1 Transmit Buffer, at 5EH in the table. */
.macro VECTOR_ASC1_TBIC handler
        VECTOR 094, \handler
.endm

/* ASC1 Receive, at 49H in the table. */
.macro VECTOR_ASC1_RIC handler
        VECTOR 073, \handler
.endm

/* ASC1 Error, at 4AH in the table. */
.macro VECTOR_ASC1_EIC handler
        VECTOR 074, \handler
.endm

/* ASC1 Autobaud, at 42H in the table. */
.macro VECTOR_ASC1_ABIC handler
        VECTOR 066, \handler
.endm

/* End of PEC Subchannel, at 4CH in the table. */
.macro VECTOR_EOPIC handler
        VECTOR 076, \handler
.endm

/* CAPCOM6 Timer T12, at 4DH in the table. */
.macro VECTOR_CCU6_T12IC handler
        VECTOR 077, \handler
.endm

/* CAPCOM6 Timer T13, at 4EH in the table. */
.macro VECTOR_CCU6_T13IC handler
        VECTOR 078, \handler
.endm

/* CAPCOM6 Emergency, at 4FH in the table. */
.macro VECTOR_CCU6_EIC handler
        VECTOR 079, \handler
.endm

/* CAPCOM6, at 50H in the table. */
.macro VECTOR_CCU6_IC handler
        VECTOR 080, \handler
.endm

/* SSC1 Transmit, at 51H in the table. */
.macro VECTOR_SSC1_TIC handler
        VECTOR 081, \handler
.endm

/* SSC1 Receive, at 52H in the table. */
.macro VECTOR_SSC1_RIC handler
        VECTOR 082, \handler
.endm

/* SSC1 Error, at 53H in the table. */
.macro VECTOR_SSC1_EIC handler
        VECTOR 083, \handler
.endm

/* CAN0, at 54H in the table. */
.macro VECTOR_CAN_0IC handler
        VECTOR 084, \handler
.endm

/* CAN1, at 55H in the table. */
.macro VECTOR_CAN_1IC handler
        VECTOR 085, \handler
.endm

/* CAN2, at 56H in the table. */
.macro VECTOR_CAN_2IC handler
        VECTOR 086, \handler
.endm

/* CAN3, at 57H in the table. */
.macro VECTOR_CAN_3IC handler
        VECTOR 087, \handler
.endm

/* CAN4, at 59H in the table. */
.macro VECTOR_CAN_4IC handler
        VECTOR 089, \handler
.endm

/* CAN5, at 5AH in the table. */
.macro VECTOR_CAN_5IC handler
        VECTOR 090, \handler
.endm

/* CAN6, at 5BH in the table. */
.macro VECTOR_CAN_6IC handler
        VECTOR 091, \handler
.endm

/* CAN7, at 5CH in the table. */
.macro VECTOR_CAN_7IC handler
        VECTOR 092, \handler
.endm

/* RTC, at 5DH in the table. */
.macro VECTOR_RTC_IC handler
        VECTOR 093, \handler
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

/* Software break, at 08H in the table. */
.macro VECTOR_SBRKTRAP handler
        VECTOR 008, \handler
.endm

/* Undefined opcode, PMI access error, protected instruction
   fault or illegal word operand access.  All four raise this one vector, and
   TFR says which of them it was, at 0AH in the table. */
.macro VECTOR_BTRAP handler
        VECTOR 010, \handler
.endm

/* 0BH to 0FH are reserved, and these vector numbers are real but nothing on
 * this part raises them:
 * 10H, 11H, 12H, 13H, 14H, 15H, 16H, 17H, 1EH, 1FH, 20H, 21H, 3FH, 40H, 41H, 4BH, 58H
 */
