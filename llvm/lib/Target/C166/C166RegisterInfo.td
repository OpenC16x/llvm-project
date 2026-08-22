//===-- C166RegisterInfo.td - C166 Register defs -----------*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Declarations that describe the C166 register file.
//
// The C166 general purpose registers R0-R15 are not "real" registers: they are
// a window into the internal RAM selected by the context pointer (CP).  The
// first eight of them can additionally be accessed as pairs of byte registers
// RLn/RHn; R8-R15 have no byte aliases.
//
//===----------------------------------------------------------------------===//

class C166Reg<bits<16> enc, string n> : Register<n> {
  let Namespace = "C166";
  let HWEncoding = enc;
}

class C166RegWithSubregs<bits<16> enc, string n, list<Register> subregs>
    : RegisterWithSubRegs<n, subregs> {
  let Namespace = "C166";
  let HWEncoding = enc;
}

let Namespace = "C166" in {
  def subreg_lo : SubRegIndex<8, 0>;
  def subreg_hi : SubRegIndex<8, 8>;
}

//===----------------------------------------------------------------------===//
// Byte registers (low/high halves of R0-R7)
//===----------------------------------------------------------------------===//

def RL0 : C166Reg< 0, "rl0">, DwarfRegNum<[16]>;
def RH0 : C166Reg< 1, "rh0">, DwarfRegNum<[17]>;
def RL1 : C166Reg< 2, "rl1">, DwarfRegNum<[18]>;
def RH1 : C166Reg< 3, "rh1">, DwarfRegNum<[19]>;
def RL2 : C166Reg< 4, "rl2">, DwarfRegNum<[20]>;
def RH2 : C166Reg< 5, "rh2">, DwarfRegNum<[21]>;
def RL3 : C166Reg< 6, "rl3">, DwarfRegNum<[22]>;
def RH3 : C166Reg< 7, "rh3">, DwarfRegNum<[23]>;
def RL4 : C166Reg< 8, "rl4">, DwarfRegNum<[24]>;
def RH4 : C166Reg< 9, "rh4">, DwarfRegNum<[25]>;
def RL5 : C166Reg<10, "rl5">, DwarfRegNum<[26]>;
def RH5 : C166Reg<11, "rh5">, DwarfRegNum<[27]>;
def RL6 : C166Reg<12, "rl6">, DwarfRegNum<[28]>;
def RH6 : C166Reg<13, "rh6">, DwarfRegNum<[29]>;
def RL7 : C166Reg<14, "rl7">, DwarfRegNum<[30]>;
def RH7 : C166Reg<15, "rh7">, DwarfRegNum<[31]>;

//===----------------------------------------------------------------------===//
// Word registers
//===----------------------------------------------------------------------===//

let SubRegIndices = [subreg_lo, subreg_hi] in {
def R0 : C166RegWithSubregs<0, "r0", [RL0, RH0]>, DwarfRegNum<[0]>;
def R1 : C166RegWithSubregs<1, "r1", [RL1, RH1]>, DwarfRegNum<[1]>;
def R2 : C166RegWithSubregs<2, "r2", [RL2, RH2]>, DwarfRegNum<[2]>;
def R3 : C166RegWithSubregs<3, "r3", [RL3, RH3]>, DwarfRegNum<[3]>;
def R4 : C166RegWithSubregs<4, "r4", [RL4, RH4]>, DwarfRegNum<[4]>;
def R5 : C166RegWithSubregs<5, "r5", [RL5, RH5]>, DwarfRegNum<[5]>;
def R6 : C166RegWithSubregs<6, "r6", [RL6, RH6]>, DwarfRegNum<[6]>;
def R7 : C166RegWithSubregs<7, "r7", [RL7, RH7]>, DwarfRegNum<[7]>;
}

def R8  : C166Reg< 8, "r8">,  DwarfRegNum<[8]>;
def R9  : C166Reg< 9, "r9">,  DwarfRegNum<[9]>;
def R10 : C166Reg<10, "r10">, DwarfRegNum<[10]>;
def R11 : C166Reg<11, "r11">, DwarfRegNum<[11]>;
def R12 : C166Reg<12, "r12">, DwarfRegNum<[12]>;
def R13 : C166Reg<13, "r13">, DwarfRegNum<[13]>;
def R14 : C166Reg<14, "r14">, DwarfRegNum<[14]>;
def R15 : C166Reg<15, "r15">, DwarfRegNum<[15]>;

//===----------------------------------------------------------------------===//
// Special function registers
//===----------------------------------------------------------------------===//

// A special function register carries its 8 bit short address as the hardware
// encoding, which is how the "reg" field of PUSH and POP names it.  That
// address is also where the register is mapped: short address N is FE00H + 2N
// below 80H and FF00H + 2(N - 80H) above it, so the memory address every other
// instruction reaches it by is worked out from the encoding rather than
// written down a second time.  The ones above 80H are the bit-addressable
// half, and their bitoff is the short address again.
//
// This is the SFR space of the XC164CM, checked against the XC164CM User's
// Manual (System Units V2.1 and Peripheral Units V1.2, 2006).  The extended
// SFR space that EXTR selects is not here at all - nothing in this backend
// emits EXTR, so an ESFR has no way to be reached - which leaves out the whole
// of SYSCON1, SYSCON3, PLLCON, RTC, the identifier registers and the port
// alternate-select registers.
//
// The map is a property of the part rather than of the family: the same short
// address names different registers on different derivatives, so a table can
// only be right about one of them.  Where the XC164CM differs from the older
// C167 layout it follows the XC164CM, and the registers of an external bus -
// ADDRSEL1-4 and BUSCON0-4 - are absent because this part has no external bus
// and those short addresses mean other things on it:
//
//   0CH  CPUCON1, not ADDRSEL1     86H  SPSEG,  not BUSCON0
//   0DH  CPUCON2, not ADDRSEL2     89H  VECSEG, not SYSCON
//   8BH  P9,      not BUSCON2      8CH  DP9,    not BUSCON3
//   8DH  ODP9,    not BUSCON4
//
// The peripheral names here are the classic C166 ones.  The XC164CM manual
// prefixes them with the module - GPT12E_T2CON for T2CON, ADC_CON for ADCON,
// CC2_M4 for CCM4 - but names the same addresses, so the two spellings differ
// only in what a datasheet calls them.

// CPU core: the data page pointers, the code segment pointer, the multiply/
// divide unit, the context pointer and the system stack and its bounds.
def DPP0     : C166Reg<0x00, "dpp0">, DwarfRegNum<[40]>; // FE00H
def DPP1     : C166Reg<0x01, "dpp1">, DwarfRegNum<[41]>; // FE02H
def DPP2     : C166Reg<0x02, "dpp2">, DwarfRegNum<[42]>; // FE04H
def DPP3     : C166Reg<0x03, "dpp3">, DwarfRegNum<[43]>; // FE06H
def CSP      : C166Reg<0x04, "csp">; // FE08H
def MDH      : C166Reg<0x06, "mdh">, DwarfRegNum<[34]>; // FE0CH
def MDL      : C166Reg<0x07, "mdl">, DwarfRegNum<[33]>; // FE0EH
def CP       : C166Reg<0x08, "cp">, DwarfRegNum<[37]>; // FE10H
def SYSSP    : C166Reg<0x09, "sp">, DwarfRegNum<[36]>; // FE12H
def STKOV    : C166Reg<0x0A, "stkov">, DwarfRegNum<[38]>; // FE14H
def STKUN    : C166Reg<0x0B, "stkun">, DwarfRegNum<[39]>; // FE16H

// External bus: the address windows and their bus configurations.
def CPUCON1  : C166Reg<0x0C, "cpucon1">; // FE18H
def CPUCON2  : C166Reg<0x0D, "cpucon2">; // FE1AH
def SPSEG    : C166Reg<0x86, "spseg">; // FF0CH
def VECSEG   : C166Reg<0x89, "vecseg">; // FF12H

// Program status word, and the two constant registers.
def PSW      : C166Reg<0x88, "psw">, DwarfRegNum<[32]>; // FF10H
def MDC      : C166Reg<0x87, "mdc">, DwarfRegNum<[35]>; // FF0EH
def ZEROS    : C166Reg<0x8E, "zeros">; // FF1CH
def ONES     : C166Reg<0x8F, "ones">; // FF1EH
def TFR      : C166Reg<0xD6, "tfr">; // FFACH

// General purpose timers GPT1 and GPT2.
def T2       : C166Reg<0x20, "t2">; // FE40H
def T3       : C166Reg<0x21, "t3">; // FE42H
def T4       : C166Reg<0x22, "t4">; // FE44H
def T5       : C166Reg<0x23, "t5">; // FE46H
def T6       : C166Reg<0x24, "t6">; // FE48H
def CAPREL   : C166Reg<0x25, "caprel">; // FE4AH
def T2CON    : C166Reg<0xA0, "t2con">; // FF40H
def T3CON    : C166Reg<0xA1, "t3con">; // FF42H
def T4CON    : C166Reg<0xA2, "t4con">; // FF44H
def T5CON    : C166Reg<0xA3, "t5con">; // FF46H
def T6CON    : C166Reg<0xA4, "t6con">; // FF48H

// CAPCOM1 and CAPCOM2: the timers, their reloads, the capture/compare
// registers and the mode control registers.
def T0       : C166Reg<0x28, "t0">; // FE50H
def T1       : C166Reg<0x29, "t1">; // FE52H
def T0REL    : C166Reg<0x2A, "t0rel">; // FE54H
def T1REL    : C166Reg<0x2B, "t1rel">; // FE56H
def T01CON   : C166Reg<0xA8, "t01con">; // FF50H
def T78CON   : C166Reg<0x90, "t78con">; // FF20H
def CC0      : C166Reg<0x40, "cc0">; // FE80H
def CC1      : C166Reg<0x41, "cc1">; // FE82H
def CC2      : C166Reg<0x42, "cc2">; // FE84H
def CC3      : C166Reg<0x43, "cc3">; // FE86H
def CC4      : C166Reg<0x44, "cc4">; // FE88H
def CC5      : C166Reg<0x45, "cc5">; // FE8AH
def CC6      : C166Reg<0x46, "cc6">; // FE8CH
def CC7      : C166Reg<0x47, "cc7">; // FE8EH
def CC8      : C166Reg<0x48, "cc8">; // FE90H
def CC9      : C166Reg<0x49, "cc9">; // FE92H
def CC10     : C166Reg<0x4A, "cc10">; // FE94H
def CC11     : C166Reg<0x4B, "cc11">; // FE96H
def CC12     : C166Reg<0x4C, "cc12">; // FE98H
def CC13     : C166Reg<0x4D, "cc13">; // FE9AH
def CC14     : C166Reg<0x4E, "cc14">; // FE9CH
def CC15     : C166Reg<0x4F, "cc15">; // FE9EH
def CC16     : C166Reg<0x30, "cc16">; // FE60H
def CC17     : C166Reg<0x31, "cc17">; // FE62H
def CC18     : C166Reg<0x32, "cc18">; // FE64H
def CC19     : C166Reg<0x33, "cc19">; // FE66H
def CC20     : C166Reg<0x34, "cc20">; // FE68H
def CC21     : C166Reg<0x35, "cc21">; // FE6AH
def CC22     : C166Reg<0x36, "cc22">; // FE6CH
def CC23     : C166Reg<0x37, "cc23">; // FE6EH
def CC24     : C166Reg<0x38, "cc24">; // FE70H
def CC25     : C166Reg<0x39, "cc25">; // FE72H
def CC26     : C166Reg<0x3A, "cc26">; // FE74H
def CC27     : C166Reg<0x3B, "cc27">; // FE76H
def CC28     : C166Reg<0x3C, "cc28">; // FE78H
def CC29     : C166Reg<0x3D, "cc29">; // FE7AH
def CC30     : C166Reg<0x3E, "cc30">; // FE7CH
def CC31     : C166Reg<0x3F, "cc31">; // FE7EH
def CCM0     : C166Reg<0xA9, "ccm0">; // FF52H
def CCM1     : C166Reg<0xAA, "ccm1">; // FF54H
def CCM2     : C166Reg<0xAB, "ccm2">; // FF56H
def CCM3     : C166Reg<0xAC, "ccm3">; // FF58H
def CCM4     : C166Reg<0x91, "ccm4">; // FF22H
def CCM5     : C166Reg<0x92, "ccm5">; // FF24H
def CCM6     : C166Reg<0x93, "ccm6">; // FF26H
def CCM7     : C166Reg<0x94, "ccm7">; // FF28H

// The PWM module.
def PW0      : C166Reg<0x18, "pw0">; // FE30H
def PW1      : C166Reg<0x19, "pw1">; // FE32H
def PW2      : C166Reg<0x1A, "pw2">; // FE34H
def PW3      : C166Reg<0x1B, "pw3">; // FE36H
def PWMCON0  : C166Reg<0x98, "pwmcon0">; // FF30H
def PWMCON1  : C166Reg<0x99, "pwmcon1">; // FF32H

// The A/D converter and the watchdog.
def ADDAT    : C166Reg<0x50, "addat">; // FEA0H
def ADCON    : C166Reg<0xD0, "adcon">; // FFA0H
def WDT      : C166Reg<0x57, "wdt">; // FEAEH
def WDTCON   : C166Reg<0xD7, "wdtcon">; // FFAEH

// Serial channel 0 (ASC0) and the synchronous serial channel (SSC).
def S0TBUF   : C166Reg<0x58, "s0tbuf">; // FEB0H
def S0RBUF   : C166Reg<0x59, "s0rbuf">; // FEB2H
def S0BG     : C166Reg<0x5A, "s0bg">; // FEB4H
def S0CON    : C166Reg<0xD8, "s0con">; // FFB0H
def SSCCON   : C166Reg<0xD9, "ssccon">; // FFB2H

// The peripheral event controller.
def PECC0    : C166Reg<0x60, "pecc0">; // FEC0H
def PECC1    : C166Reg<0x61, "pecc1">; // FEC2H
def PECC2    : C166Reg<0x62, "pecc2">; // FEC4H
def PECC3    : C166Reg<0x63, "pecc3">; // FEC6H
def PECC4    : C166Reg<0x64, "pecc4">; // FEC8H
def PECC5    : C166Reg<0x65, "pecc5">; // FECAH
def PECC6    : C166Reg<0x66, "pecc6">; // FECCH
def PECC7    : C166Reg<0x67, "pecc7">; // FECEH

// Interrupt control registers, one per source.
def T2IC     : C166Reg<0xB0, "t2ic">; // FF60H
def T3IC     : C166Reg<0xB1, "t3ic">; // FF62H
def T4IC     : C166Reg<0xB2, "t4ic">; // FF64H
def T5IC     : C166Reg<0xB3, "t5ic">; // FF66H
def T6IC     : C166Reg<0xB4, "t6ic">; // FF68H
def CRIC     : C166Reg<0xB5, "cric">; // FF6AH
def S0TIC    : C166Reg<0xB6, "s0tic">; // FF6CH
def S0RIC    : C166Reg<0xB7, "s0ric">; // FF6EH
def S0EIC    : C166Reg<0xB8, "s0eic">; // FF70H
def SSCTIC   : C166Reg<0xB9, "ssctic">; // FF72H
def SSCRIC   : C166Reg<0xBA, "sscric">; // FF74H
def SSCEIC   : C166Reg<0xBB, "ssceic">; // FF76H
def CC0IC    : C166Reg<0xBC, "cc0ic">; // FF78H
def CC1IC    : C166Reg<0xBD, "cc1ic">; // FF7AH
def CC2IC    : C166Reg<0xBE, "cc2ic">; // FF7CH
def CC3IC    : C166Reg<0xBF, "cc3ic">; // FF7EH
def CC4IC    : C166Reg<0xC0, "cc4ic">; // FF80H
def CC5IC    : C166Reg<0xC1, "cc5ic">; // FF82H
def CC6IC    : C166Reg<0xC2, "cc6ic">; // FF84H
def CC7IC    : C166Reg<0xC3, "cc7ic">; // FF86H
def CC8IC    : C166Reg<0xC4, "cc8ic">; // FF88H
def CC9IC    : C166Reg<0xC5, "cc9ic">; // FF8AH
def CC10IC   : C166Reg<0xC6, "cc10ic">; // FF8CH
def CC11IC   : C166Reg<0xC7, "cc11ic">; // FF8EH
def CC12IC   : C166Reg<0xC8, "cc12ic">; // FF90H
def CC13IC   : C166Reg<0xC9, "cc13ic">; // FF92H
def CC14IC   : C166Reg<0xCA, "cc14ic">; // FF94H
def CC15IC   : C166Reg<0xCB, "cc15ic">; // FF96H
def ADCIC    : C166Reg<0xCC, "adcic">; // FF98H
def ADEIC    : C166Reg<0xCD, "adeic">; // FF9AH
def T0IC     : C166Reg<0xCE, "t0ic">; // FF9CH
def T1IC     : C166Reg<0xCF, "t1ic">; // FF9EH

// The ports and their direction registers.
def P0L      : C166Reg<0x80, "p0l">; // FF00H
def P0H      : C166Reg<0x81, "p0h">; // FF02H
def P1L      : C166Reg<0x82, "p1l">; // FF04H
def P1H      : C166Reg<0x83, "p1h">; // FF06H
def P1DIDIS  : C166Reg<0x52, "p1didis">; // FEA4H
def P2       : C166Reg<0xE0, "p2">; // FFC0H
def DP2      : C166Reg<0xE1, "dp2">; // FFC2H
def P3       : C166Reg<0xE2, "p3">; // FFC4H
def DP3      : C166Reg<0xE3, "dp3">; // FFC6H
def P4       : C166Reg<0xE4, "p4">; // FFC8H
def DP4      : C166Reg<0xE5, "dp4">; // FFCAH
def P5       : C166Reg<0xD1, "p5">; // FFA2H
def P5DIDIS  : C166Reg<0xD2, "p5didis">; // FFA4H
def P6       : C166Reg<0xE6, "p6">; // FFCCH
def DP6      : C166Reg<0xE7, "dp6">; // FFCEH
def P7       : C166Reg<0xE8, "p7">; // FFD0H
def DP7      : C166Reg<0xE9, "dp7">; // FFD2H
def P8       : C166Reg<0xEA, "p8">; // FFD4H
def DP8      : C166Reg<0xEB, "dp8">; // FFD6H
// Port 9 sits in the block the external bus registers occupy on a part that
// has an external bus; the XC164CM has none.
def P9       : C166Reg<0x8B, "p9">; // FF16H
def DP9      : C166Reg<0x8C, "dp9">; // FF18H
def ODP9     : C166Reg<0x8D, "odp9">; // FF1AH
def FOCON    : C166Reg<0xD5, "focon">; // FFAAH

//===----------------------------------------------------------------------===//
// Register classes
//===----------------------------------------------------------------------===//

// Byte registers.  RL0/RH0 and RL1/RH1 overlap the ABI stack and frame
// pointers, so they are listed last and are reserved in practice.
def GR8 : RegisterClass<"C166", [i8], 8,
  (add RL2, RH2, RL3, RH3, RL4, RH4, RL5, RH5, RL6, RH6, RL7, RH7,
       RL0, RH0, RL1, RH1)>;

// Word registers.  The allocation order prefers the caller saved registers
// R2-R11 over the callee saved R12-R15.  R1 (frame pointer) and R0 (stack
// pointer) come last and are reserved when in use.
def GR16 : RegisterClass<"C166", [i16], 16,
  (add R2, R3, R4, R5, R6, R7, R8, R9, R10, R11,
       R12, R13, R14, R15, R1, R0)>;

// Non-allocatable special function registers.
def SFR : RegisterClass<"C166", [i16], 16,
  (add DPP0, DPP1, DPP2, DPP3, CSP, MDH, MDL, CP, SYSSP, STKOV, STKUN,
       CPUCON1, CPUCON2, SPSEG, VECSEG, P9, DP9, ODP9,
       PSW, MDC, ZEROS, ONES, TFR, T2, T3, T4,
       T5, T6, CAPREL, T2CON, T3CON, T4CON, T5CON, T6CON, T0, T1, T0REL,
       T1REL, T01CON, T78CON, CC0, CC1, CC2, CC3, CC4, CC5, CC6, CC7, CC8,
       CC9, CC10, CC11, CC12, CC13, CC14, CC15, CC16, CC17, CC18, CC19,
       CC20, CC21, CC22, CC23, CC24, CC25, CC26, CC27, CC28, CC29, CC30,
       CC31, CCM0, CCM1, CCM2, CCM3, CCM4, CCM5, CCM6, CCM7, PW0, PW1,
       PW2, PW3, PWMCON0, PWMCON1, ADDAT, ADCON, WDT, WDTCON, S0TBUF,
       S0RBUF, S0BG, S0CON, SSCCON, PECC0, PECC1, PECC2, PECC3, PECC4,
       PECC5, PECC6, PECC7, T2IC, T3IC, T4IC, T5IC, T6IC, CRIC, S0TIC,
       S0RIC, S0EIC, SSCTIC, SSCRIC, SSCEIC, CC0IC, CC1IC, CC2IC, CC3IC,
       CC4IC, CC5IC, CC6IC, CC7IC, CC8IC, CC9IC, CC10IC, CC11IC, CC12IC,
       CC13IC, CC14IC, CC15IC, ADCIC, ADEIC, T0IC, T1IC, P0L, P0H, P1L,
       P1H, P1DIDIS, P2, DP2, P3, DP3, P4, DP4, P5, P5DIDIS, P6, DP6, P7,
       DP7, P8, DP8, FOCON)> {
  let isAllocatable = 0;
  let CopyCost = -1;
}

// Everything the 8 bit "reg" field can name.  There are two of these because
// F0H + n is a word register in a word instruction and a byte register in a
// byte one, and the special function registers are reachable from either.
// They exist only to widen that field past the general purpose registers, so
// neither is an allocation class of its own.
def GR16orSFR : RegisterClass<"C166", [i16], 16, (add GR16, SFR)> {
  let isAllocatable = 0;
  let CopyCost = -1;
}

def GR8orSFR : RegisterClass<"C166", [i8], 8, (add GR8, SFR)> {
  let isAllocatable = 0;
  let CopyCost = -1;
}
