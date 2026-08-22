/* The XC164CM's special function registers, for C.
 *
 * Every name here is one C166RegisterInfo.td also defines, at the address that
 * file gives it, so what the assembler accepts and what C sees cannot drift
 * apart.  See the comment at the top of that file for where the addresses come
 * from and which three the manual is self-contradictory about.
 *
 * These are near addresses, which is what makes them cheap: both register
 * spaces are inside the page DPP3 selects, so reaching one is an ordinary
 * 16 bit access and needs no EXTR and no far pointer.  That holds as long as
 * DPP3 is left at page 3, which is what crt0.S sets it to.
 *
 * Which trap number a peripheral raises is in xc164cm-vectors.inc, because a
 * vector is claimed from assembly rather than from C.
 *
 * The CAN module's registers are not here.  That part has a manual of its own
 * and this file was not built from it.
 *
 * Part of the LLVM Project, under the Apache License v2.0 with LLVM
 * Exceptions.  See https://llvm.org/LICENSE.txt for license information.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */

#ifndef C166_XC164CM_H
#define C166_XC164CM_H

/* A register is a word, and reading or writing one has to actually happen. */
typedef volatile unsigned int c166_sfr;

#define C166_SFR(addr) (*(c166_sfr *)(addr))

/* Special function registers, FE00H to FFDEH. */
#define DPP0        C166_SFR(0xFE00U)
#define DPP1        C166_SFR(0xFE02U)
#define DPP2        C166_SFR(0xFE04U)
#define DPP3        C166_SFR(0xFE06U)
#define CSP         C166_SFR(0xFE08U)
#define MDH         C166_SFR(0xFE0CU)
#define MDL         C166_SFR(0xFE0EU)
#define CP          C166_SFR(0xFE10U)
#define SP          C166_SFR(0xFE12U)
#define STKOV       C166_SFR(0xFE14U)
#define STKUN       C166_SFR(0xFE16U)
#define CPUCON1     C166_SFR(0xFE18U)
#define CPUCON2     C166_SFR(0xFE1AU)
#define T2          C166_SFR(0xFE40U)
#define T3          C166_SFR(0xFE42U)
#define T4          C166_SFR(0xFE44U)
#define T5          C166_SFR(0xFE46U)
#define T6          C166_SFR(0xFE48U)
#define CAPREL      C166_SFR(0xFE4AU)
#define CC16        C166_SFR(0xFE60U)
#define CC17        C166_SFR(0xFE62U)
#define CC18        C166_SFR(0xFE64U)
#define CC19        C166_SFR(0xFE66U)
#define CC20        C166_SFR(0xFE68U)
#define CC21        C166_SFR(0xFE6AU)
#define CC22        C166_SFR(0xFE6CU)
#define CC23        C166_SFR(0xFE6EU)
#define CC24        C166_SFR(0xFE70U)
#define CC25        C166_SFR(0xFE72U)
#define CC26        C166_SFR(0xFE74U)
#define CC27        C166_SFR(0xFE76U)
#define CC28        C166_SFR(0xFE78U)
#define CC29        C166_SFR(0xFE7AU)
#define CC30        C166_SFR(0xFE7CU)
#define CC31        C166_SFR(0xFE7EU)
#define ADDAT       C166_SFR(0xFEA0U)
#define S0TBUF      C166_SFR(0xFEB0U)
#define S0RBUF      C166_SFR(0xFEB2U)
#define S0BG        C166_SFR(0xFEB4U)
#define PECC0       C166_SFR(0xFEC0U)
#define PECC1       C166_SFR(0xFEC2U)
#define PECC2       C166_SFR(0xFEC4U)
#define PECC3       C166_SFR(0xFEC6U)
#define PECC4       C166_SFR(0xFEC8U)
#define PECC5       C166_SFR(0xFECAU)
#define PECC6       C166_SFR(0xFECCU)
#define PECC7       C166_SFR(0xFECEU)
#define P1L         C166_SFR(0xFF04U)
#define P1H         C166_SFR(0xFF06U)
#define SPSEG       C166_SFR(0xFF0CU)
#define MDC         C166_SFR(0xFF0EU)
#define PSW         C166_SFR(0xFF10U)
#define VECSEG      C166_SFR(0xFF12U)
#define P9          C166_SFR(0xFF16U)
#define DP9         C166_SFR(0xFF18U)
#define ODP9        C166_SFR(0xFF1AU)
#define ZEROS       C166_SFR(0xFF1CU)
#define ONES        C166_SFR(0xFF1EU)
#define T78CON      C166_SFR(0xFF20U)
#define CCM4        C166_SFR(0xFF22U)
#define CCM5        C166_SFR(0xFF24U)
#define CCM6        C166_SFR(0xFF26U)
#define CCM7        C166_SFR(0xFF28U)
#define T2CON       C166_SFR(0xFF40U)
#define T3CON       C166_SFR(0xFF42U)
#define T4CON       C166_SFR(0xFF44U)
#define T5CON       C166_SFR(0xFF46U)
#define T6CON       C166_SFR(0xFF48U)
#define T2IC        C166_SFR(0xFF60U)
#define T3IC        C166_SFR(0xFF62U)
#define T4IC        C166_SFR(0xFF64U)
#define T5IC        C166_SFR(0xFF66U)
#define T6IC        C166_SFR(0xFF68U)
#define CRIC        C166_SFR(0xFF6AU)
#define S0TIC       C166_SFR(0xFF6CU)
#define S0RIC       C166_SFR(0xFF6EU)
#define S0EIC       C166_SFR(0xFF70U)
#define SSCTIC      C166_SFR(0xFF72U)
#define SSCRIC      C166_SFR(0xFF74U)
#define SSCEIC      C166_SFR(0xFF76U)
#define CC8IC       C166_SFR(0xFF88U)
#define CC9IC       C166_SFR(0xFF8AU)
#define CC10IC      C166_SFR(0xFF8CU)
#define CC11IC      C166_SFR(0xFF8EU)
#define CC12IC      C166_SFR(0xFF90U)
#define CC13IC      C166_SFR(0xFF92U)
#define ADCIC       C166_SFR(0xFF98U)
#define ADEIC       C166_SFR(0xFF9AU)
#define ADCON       C166_SFR(0xFFA0U)
#define P5          C166_SFR(0xFFA2U)
#define P5DIDIS     C166_SFR(0xFFA4U)
#define FOCON       C166_SFR(0xFFAAU)
#define TFR         C166_SFR(0xFFACU)
#define WDTCON      C166_SFR(0xFFAEU)
#define S0CON       C166_SFR(0xFFB0U)
#define SSCCON      C166_SFR(0xFFB2U)
#define P3          C166_SFR(0xFFC4U)
#define DP3         C166_SFR(0xFFC6U)

/* Extended special function registers, F000H to F1DEH.  Reaching one by
 * address needs no EXTR; only the short "reg" form of an instruction does. */
#define QX0         C166_SFR(0xF000U)
#define QX1         C166_SFR(0xF002U)
#define QR0         C166_SFR(0xF004U)
#define QR1         C166_SFR(0xF006U)
#define CC2_IOC     C166_SFR(0xF066U)
#define IDPROG      C166_SFR(0xF078U)
#define IDMEM       C166_SFR(0xF07AU)
#define IDCHIP      C166_SFR(0xF07CU)
#define IDMANUF     C166_SFR(0xF07EU)
#define ADC_CTR2    C166_SFR(0xF09CU)
#define ADC_CTR2IN  C166_SFR(0xF09EU)
#define ADC_DAT2    C166_SFR(0xF0A0U)
#define SCUSLC      C166_SFR(0xF0C0U)
#define RTC_RELL    C166_SFR(0xF0CCU)
#define RTC_RELH    C166_SFR(0xF0CEU)
#define RTC_RTCL    C166_SFR(0xF0D4U)
#define RTC_RTCH    C166_SFR(0xF0D6U)
#define IMBCTR      C166_SFR(0xF0FEU)
#define DP1L        C166_SFR(0xF104U)
#define DP1H        C166_SFR(0xF106U)
#define RTC_ISNC    C166_SFR(0xF10CU)
#define RTC_CON     C166_SFR(0xF110U)
#define ALTSEL0P1H  C166_SFR(0xF120U)
#define ALTSEL0P3   C166_SFR(0xF126U)
#define ALTSEL0P1L  C166_SFR(0xF130U)
#define ALTSEL0P9   C166_SFR(0xF138U)
#define ALTSEL1P9   C166_SFR(0xF13AU)
#define EOPIC       C166_SFR(0xF180U)
#define PLL_IC      C166_SFR(0xF19EU)
#define RTC_IC      C166_SFR(0xF1A0U)
#define EXICON      C166_SFR(0xF1C0U)
#define ODP3        C166_SFR(0xF1C6U)
#define PLLCON      C166_SFR(0xF1D0U)
#define SYSCON3     C166_SFR(0xF1D4U)
#define EXISEL1     C166_SFR(0xF1D8U)
#define EXISEL0     C166_SFR(0xF1DAU)
#define SYSCON1     C166_SFR(0xF1DCU)

/* The on-chip X-peripherals, at E800H and up.  These have no short address at
 * all, so no instruction reaches one by register name - but the addresses are
 * inside the page DPP3 selects, so from C they are ordinary near accesses like
 * the rest.  ADDRSEL7 and TCONCS7 belong to the LXBus controller, the on-chip
 * bus these hang off, which is a different thing from the external bus this
 * part does not have. */
#define CCU6_T12      C166_SFR(0xE890U)
#define CCU6_T12PR    C166_SFR(0xE892U)
#define CCU6_T12DTC   C166_SFR(0xE894U)
#define CCU6_CC60R    C166_SFR(0xE898U)
#define CCU6_CC61R    C166_SFR(0xE89AU)
#define CCU6_CC62R    C166_SFR(0xE89CU)
#define CCU6_TCTR4    C166_SFR(0xE8A6U)
#define CCU6_CMPSTAT  C166_SFR(0xE8A8U)
#define CCU6_TCTR0    C166_SFR(0xE8ACU)
#define CCU6_T13      C166_SFR(0xE8B0U)
#define CCU6_T13PR    C166_SFR(0xE8B2U)
#define CCU6_CC63R    C166_SFR(0xE8B4U)
#define CCU6_MODCTR   C166_SFR(0xE8C0U)
#define CCU6_TRPCTR   C166_SFR(0xE8C2U)
#define CCU6_T12MSEL  C166_SFR(0xE8C6U)
#define CCU6_MCMOUT   C166_SFR(0xE8CCU)
#define CCU6_IS       C166_SFR(0xE8D0U)
#define CCU6_ISS      C166_SFR(0xE8D2U)
#define CCU6_ISR      C166_SFR(0xE8D4U)
#define CCU6_INP      C166_SFR(0xE8D6U)
#define CCU6_IEN      C166_SFR(0xE8D8U)
#define FINT0CSP      C166_SFR(0xEC00U)
#define FINT0ADDR     C166_SFR(0xEC02U)
#define FINT1CSP      C166_SFR(0xEC04U)
#define FINT1ADDR     C166_SFR(0xEC06U)
#define TCONCS7       C166_SFR(0xEE48U)
#define ADDRSEL7      C166_SFR(0xEE4EU)

#endif /* C166_XC164CM_H */
