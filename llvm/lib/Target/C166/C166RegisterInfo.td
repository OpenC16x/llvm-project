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
//
// What is here is what that part has.  A register whose address appears
// nowhere in either XC164CM manual has been taken out rather than left to be
// written to by mistake, which removed the classic CAPCOM1 block (T0, T1,
// T01CON, CC0 to CC15, CCM0 to CCM3 and their interrupt control registers),
// the PWM unit, and ports 0, 2, 4, 6, 7 and 8.  This part has CAPCOM1 and
// CAPCOM2 under CC1_ and CC2_ names at other addresses, has CAPCOM6 in the
// X-peripheral space at E8xxH where no short address reaches, and has only
// ports 1, 3, 5 and 9.  Another derivative wanting the old names back should
// add them from its own manual rather than assume these were right for it.

// CPU core: the data page pointers, the code segment pointer, the multiply/
// divide unit, the context pointer and the system stack and its bounds.
def DPP0     : C166Reg<0x00, "dpp0">, DwarfRegNum<[40]>; // FE00H
def DPP1     : C166Reg<0x01, "dpp1">, DwarfRegNum<[41]>; // FE02H
def DPP2     : C166Reg<0x02, "dpp2">, DwarfRegNum<[42]>; // FE04H
def DPP3     : C166Reg<0x03, "dpp3">, DwarfRegNum<[43]>; // FE06H
def CSP      : C166Reg<0x04, "csp">, DwarfRegNum<[44]>; // FE08H
def MDH      : C166Reg<0x06, "mdh">, DwarfRegNum<[34]>; // FE0CH
def MDL      : C166Reg<0x07, "mdl">, DwarfRegNum<[33]>; // FE0EH
def CP       : C166Reg<0x08, "cp">, DwarfRegNum<[37]>; // FE10H
def SYSSP    : C166Reg<0x09, "sp">, DwarfRegNum<[36]>; // FE12H
def STKOV    : C166Reg<0x0A, "stkov">, DwarfRegNum<[38]>; // FE14H
def STKUN    : C166Reg<0x0B, "stkun">, DwarfRegNum<[39]>; // FE16H


// The XC164CM's own.  Every one of these is at a short address that the
// older layout gives to something else, which is the whole reason the map
// has to be chosen rather than assumed; the collisions are listed above.

def CPUCON1  : C166Reg<0x0C, "cpucon1">; // FE18H
def CPUCON2  : C166Reg<0x0D, "cpucon2">; // FE1AH
def SPSEG    : C166Reg<0x86, "spseg">; // FF0CH
def VECSEG   : C166Reg<0x89, "vecseg">; // FF12H
def P9       : C166Reg<0x8B, "p9">; // FF16H
def DP9      : C166Reg<0x8C, "dp9">; // FF18H
def ODP9     : C166Reg<0x8D, "odp9">; // FF1AH

// The C167's own: the external bus, CAPCOM1, the PWM unit and the ports the
// XC164CM does not have.  Addresses are from the Keil C167CS device header,
// which cites the Siemens C167CS User Manual V1.0 of 1999-05; the 91 names
// this map shares with the one above agree with it on the short address of
// every single one, which is the check that says both were read correctly.
//
// This is the C167CS's map and is named for that part rather than for the
// family, for the same reason the one above is the XC164CM's.

def ADDRSEL1 : C166Reg<0x0C, "addrsel1">; // FE18H
def ADDRSEL2 : C166Reg<0x0D, "addrsel2">; // FE1AH
def ADDRSEL3 : C166Reg<0x0E, "addrsel3">; // FE1CH
def ADDRSEL4 : C166Reg<0x0F, "addrsel4">; // FE1EH
def PW0      : C166Reg<0x18, "pw0">; // FE30H
def PW1      : C166Reg<0x19, "pw1">; // FE32H
def PW2      : C166Reg<0x1A, "pw2">; // FE34H
def PW3      : C166Reg<0x1B, "pw3">; // FE36H
def T0       : C166Reg<0x28, "t0">; // FE50H
def T1       : C166Reg<0x29, "t1">; // FE52H
def T0REL    : C166Reg<0x2A, "t0rel">; // FE54H
def T1REL    : C166Reg<0x2B, "t1rel">; // FE56H
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
def P1DIDIS  : C166Reg<0x52, "p1didis">; // FEA4H
def WDT      : C166Reg<0x57, "wdt">; // FEAEH
def P0L      : C166Reg<0x80, "p0l">; // FF00H
def P0H      : C166Reg<0x81, "p0h">; // FF02H
def BUSCON0  : C166Reg<0x86, "buscon0">; // FF0CH
def SYSCON   : C166Reg<0x89, "syscon">; // FF12H
def BUSCON1  : C166Reg<0x8A, "buscon1">; // FF14H
def BUSCON2  : C166Reg<0x8B, "buscon2">; // FF16H
def BUSCON3  : C166Reg<0x8C, "buscon3">; // FF18H
def BUSCON4  : C166Reg<0x8D, "buscon4">; // FF1AH
def PWMCON0  : C166Reg<0x98, "pwmcon0">; // FF30H
def PWMCON1  : C166Reg<0x99, "pwmcon1">; // FF32H
def T01CON   : C166Reg<0xA8, "t01con">; // FF50H
def CCM0     : C166Reg<0xA9, "ccm0">; // FF52H
def CCM1     : C166Reg<0xAA, "ccm1">; // FF54H
def CCM2     : C166Reg<0xAB, "ccm2">; // FF56H
def CCM3     : C166Reg<0xAC, "ccm3">; // FF58H
def CC0IC    : C166Reg<0xBC, "cc0ic">; // FF78H
def CC1IC    : C166Reg<0xBD, "cc1ic">; // FF7AH
def CC2IC    : C166Reg<0xBE, "cc2ic">; // FF7CH
def CC3IC    : C166Reg<0xBF, "cc3ic">; // FF7EH
def CC4IC    : C166Reg<0xC0, "cc4ic">; // FF80H
def CC5IC    : C166Reg<0xC1, "cc5ic">; // FF82H
def CC6IC    : C166Reg<0xC2, "cc6ic">; // FF84H
def CC7IC    : C166Reg<0xC3, "cc7ic">; // FF86H
def CC14IC   : C166Reg<0xCA, "cc14ic">; // FF94H
def CC15IC   : C166Reg<0xCB, "cc15ic">; // FF96H
def T0IC     : C166Reg<0xCE, "t0ic">; // FF9CH
def T1IC     : C166Reg<0xCF, "t1ic">; // FF9EH
def P2       : C166Reg<0xE0, "p2">; // FFC0H
def DP2      : C166Reg<0xE1, "dp2">; // FFC2H
def P4       : C166Reg<0xE4, "p4">; // FFC8H
def DP4      : C166Reg<0xE5, "dp4">; // FFCAH
def P6       : C166Reg<0xE6, "p6">; // FFCCH
def DP6      : C166Reg<0xE7, "dp6">; // FFCEH
def P7       : C166Reg<0xE8, "p7">; // FFD0H
def DP7      : C166Reg<0xE9, "dp7">; // FFD2H
def P8       : C166Reg<0xEA, "p8">; // FFD4H
def DP8      : C166Reg<0xEB, "dp8">; // FFD6H

// The instruction pointer, which is not a register a program can name: it is
// here because the debug information has to.  DWARF describes where a frame's
// return address is by giving a rule for one numbered column, and that column
// has to be something; on a C166 the return address is not in a register at
// all, it is on the hardware stack, so the column stands for the 24 bit
// CSP:IP pair rather than for anything the instruction set exposes.  See
// C166FrameLowering.cpp for the rule that computes it.
//
// It has no assembly name on purpose.  Every register name here is a word the
// assembler reserves, and this one would be reserving "pc" for something no
// instruction can write.
def PC       : C166Reg<0x00, "">, DwarfRegNum<[45]>;

// The five ALU condition flags, which are the low bits of the program status
// word.  They are registers of their own because the instruction set touches
// them separately and a single register cannot say so: the tables in the
// manual give MOV as "E Z V C N = * * - - *", writing the zero and negative
// flags from what it moved and leaving the carry and overflow alone.  That is
// what lets a carry survive the register shuffling in a wide addition, and it
// is also why a move destroys the answer a compare was about to reuse.
//
// Only "not affected" leaves a flag alone.  A flag the tables show as set,
// cleared, sticky, or taken from a bit is written, so it is a def here.
//
// They have no assembly names, for the reason PC has none: every register name
// is a word the assembler reserves, and a C object called "psw_n" would then
// stop assembling.
let Namespace = "C166" in {
  def subreg_n : SubRegIndex<1, 0>;
  def subreg_c : SubRegIndex<1, 1>;
  def subreg_v : SubRegIndex<1, 2>;
  def subreg_z : SubRegIndex<1, 3>;
  def subreg_e : SubRegIndex<1, 4>;
}

def PSW_N : C166Reg<0x00, "">;
def PSW_C : C166Reg<0x00, "">;
def PSW_V : C166Reg<0x00, "">;
def PSW_Z : C166Reg<0x00, "">;
def PSW_E : C166Reg<0x00, "">;

// Program status word, and the two constant registers.
let SubRegIndices = [subreg_n, subreg_c, subreg_v, subreg_z, subreg_e] in
def PSW      : C166RegWithSubregs<0x88, "psw",
                                  [PSW_N, PSW_C, PSW_V, PSW_Z, PSW_E]>,
               DwarfRegNum<[32]>; // FF10H
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
def T78CON   : C166Reg<0x90, "t78con">; // FF20H
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
def CCM4     : C166Reg<0x91, "ccm4">; // FF22H
def CCM5     : C166Reg<0x92, "ccm5">; // FF24H
def CCM6     : C166Reg<0x93, "ccm6">; // FF26H
def CCM7     : C166Reg<0x94, "ccm7">; // FF28H

// The PWM module.

// The A/D converter and the watchdog.
def ADDAT    : C166Reg<0x50, "addat">; // FEA0H
def ADCON    : C166Reg<0xD0, "adcon">; // FFA0H
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
def CC8IC    : C166Reg<0xC4, "cc8ic">; // FF88H
def CC9IC    : C166Reg<0xC5, "cc9ic">; // FF8AH
def CC10IC   : C166Reg<0xC6, "cc10ic">; // FF8CH
def CC11IC   : C166Reg<0xC7, "cc11ic">; // FF8EH
def CC12IC   : C166Reg<0xC8, "cc12ic">; // FF90H
def CC13IC   : C166Reg<0xC9, "cc13ic">; // FF92H
def ADCIC    : C166Reg<0xCC, "adcic">; // FF98H
def ADEIC    : C166Reg<0xCD, "adeic">; // FF9AH

// The ports and their direction registers.
def P1L      : C166Reg<0x82, "p1l">; // FF04H
def P1H      : C166Reg<0x83, "p1h">; // FF06H
def P3       : C166Reg<0xE2, "p3">; // FFC4H
def DP3      : C166Reg<0xE3, "dp3">; // FFC6H
def P5       : C166Reg<0xD1, "p5">; // FFA2H
def P5DIDIS  : C166Reg<0xD2, "p5didis">; // FFA4H
def FOCON    : C166Reg<0xD5, "focon">; // FFAAH

// The MAC unit's registers.  This part has a C166SV2 core, so it has a
// multiply-accumulate coprocessor, and these are the parts of it that have a
// short address.  MAS, the saturated view of the accumulator's high word, does
// not: CoSTORE reaches it through the CoREG encoding instead.
def IDX0     : C166Reg<0x84, "idx0">; // FF08H
def IDX1     : C166Reg<0x85, "idx1">; // FF0AH
def MAL      : C166Reg<0x2E, "mal">;  // FE5CH
def MAH      : C166Reg<0x2F, "mah">;  // FE5EH
def MRW      : C166Reg<0xED, "mrw">;  // FFDAH
def MCW      : C166Reg<0xEE, "mcw">;  // FFDCH
def MSW      : C166Reg<0xEF, "msw">;  // FFDEH
// The saturated view of the accumulator's high word.  It has no short address:
// CoSTORE names it by the five bit CoREG code instead, which is what the class
// below is for, so the encoding here is not an address and is never used as
// one.
def MAS      : C166Reg<0x00, "mas">;

//===----------------------------------------------------------------------===//
// Extended special function registers
//===----------------------------------------------------------------------===//

// The extended space holds a second set of registers at the same short
// addresses, mapped from F000H and F100H instead of FE00H and FF00H.  Reaching
// one through a "reg" field means preceding the instruction with EXTR, and the
// two are indistinguishable once encoded, which is why none of these is in a
// class a "reg" field can name.  What they are here for is the other way of
// reaching them: the address itself is an ordinary 16 bit address that the
// default DPPs already cover, so "mov syscon1, r2" is a MOV mem, reg and needs
// no EXTR at all.
//
// From the XC164CM User's Manual.  Three registers it documents are left out
// because it contradicts itself about where they are: it gives OPSEN as
// FE58H/2CH, CC2_T7IC as F17AH/BEH and CC2_T8IC as F17CH/BFH, and in each case
// the address and the short address name different words - 2CH is F058H in
// this space, BEH is F17CH and BFH is F17EH - with nothing else nearby to say
// which half is the typo.  The ST10F269 datasheet prints the identical
// contradiction for its own T7IC and T8IC and does supply the missing
// neighbour, so it is resolvable there and is resolved; see ESFRST10 below
// for why that settles nothing here.

def QX0         : C166Reg<0x00, "qx0">; // F000H
def QX1         : C166Reg<0x01, "qx1">; // F002H
def QR0         : C166Reg<0x02, "qr0">; // F004H
def QR1         : C166Reg<0x03, "qr1">; // F006H
def CC2_IOC     : C166Reg<0x33, "cc2_ioc">; // F066H
def IDPROG      : C166Reg<0x3C, "idprog">; // F078H
def IDMEM       : C166Reg<0x3D, "idmem">; // F07AH
def IDCHIP      : C166Reg<0x3E, "idchip">; // F07CH
def IDMANUF     : C166Reg<0x3F, "idmanuf">; // F07EH
def ADC_CTR2    : C166Reg<0x4E, "adc_ctr2">; // F09CH
def ADC_CTR2IN  : C166Reg<0x4F, "adc_ctr2in">; // F09EH
def ADC_DAT2    : C166Reg<0x50, "adc_dat2">; // F0A0H
def SCUSLC      : C166Reg<0x60, "scuslc">; // F0C0H
def RTC_RELL    : C166Reg<0x66, "rtc_rell">; // F0CCH
def RTC_RELH    : C166Reg<0x67, "rtc_relh">; // F0CEH
def RTC_RTCL    : C166Reg<0x6A, "rtc_rtcl">; // F0D4H
def RTC_RTCH    : C166Reg<0x6B, "rtc_rtch">; // F0D6H
def IMBCTR      : C166Reg<0x7F, "imbctr">; // F0FEH
def DP1L        : C166Reg<0x82, "dp1l">; // F104H
def DP1H        : C166Reg<0x83, "dp1h">; // F106H
def RTC_ISNC    : C166Reg<0x86, "rtc_isnc">; // F10CH
def RTC_CON     : C166Reg<0x88, "rtc_con">; // F110H
def ALTSEL0P1H  : C166Reg<0x90, "altsel0p1h">; // F120H
def ALTSEL0P3   : C166Reg<0x93, "altsel0p3">; // F126H
def ALTSEL0P1L  : C166Reg<0x98, "altsel0p1l">; // F130H
def ALTSEL0P9   : C166Reg<0x9C, "altsel0p9">; // F138H
def ALTSEL1P9   : C166Reg<0x9D, "altsel1p9">; // F13AH
def EOPIC       : C166Reg<0xC0, "eopic">; // F180H
def PLL_IC      : C166Reg<0xCF, "pll_ic">; // F19EH
def RTC_IC      : C166Reg<0xD0, "rtc_ic">; // F1A0H
def EXICON      : C166Reg<0xE0, "exicon">; // F1C0H
def ODP3        : C166Reg<0xE3, "odp3">; // F1C6H
def PLLCON      : C166Reg<0xE8, "pllcon">; // F1D0H
def SYSCON3     : C166Reg<0xEA, "syscon3">; // F1D4H
def EXISEL1     : C166Reg<0xEC, "exisel1">; // F1D8H
def EXISEL0     : C166Reg<0xED, "exisel0">; // F1DAH
def SYSCON1     : C166Reg<0xEE, "syscon1">; // F1DCH

// The ST10F269's extended registers, from its datasheet's Table 31.  It is a
// C167 derivative, so what is here is the classic C166 extended space - the
// PWM module, CAPCOM timers 7 and 8, the port output and open drain controls,
// the second A/D result register and the interrupt control registers for
// CAPCOM 16 to 31 - rather than the XC164CM's real-time clock, chip
// identification and alternate select registers.
//
// Twelve names above are this part's too, at the same short addresses, so
// they are reused rather than defined again: QX0, QX1, QR0 and QR1 (the MAC
// unit's, which is why they are not either part's), DP1L, DP1H, EXICON,
// ODP3 and the four identification registers.
//
// Three short addresses name different registers on the two parts, which is
// the whole reason this map is chosen rather than assumed - though in each
// case the two names describe the same hardware, so the difference is one of
// spelling:
//
//   50H  ADDAT2, not ADC_DAT2      EDH  EXISEL, not EXISEL0
//   CFH  XP3IC,  not PLL_IC
//
// T7IC and T8IC settle something the XC164CM comment above could not.  This
// datasheet reproduces the identical contradiction - T7IC at F17AH/BEH and
// T8IC at F17CH/BFH, where BEH is F17CH and BFH is F17EH - which says the
// pairing was copied from a document both manuals descend from rather than
// mistyped twice.  Here it is resolvable: this part has PWMIC, whose own row
// is F17EH/BFH and is self-consistent, so BFH is taken and T8IC cannot have
// it; BDH is claimed by nothing; and F176H to F17EH is an unbroken run for
// BBH to BFH.  The physical column is right and the two short addresses are
// each one slot too high, so T7IC is BDH and T8IC is BEH.
//
// That resolves it for this part only.  The XC164CM has no PWM module and so
// no PWMIC to collide with, which leaves BFH free there and its own manual
// still without a tiebreaker - so CC2_T7IC and CC2_T8IC stay out of the map
// above, on the same evidence they were left out on.

def XPERCON    : C166Reg<0x12, "xpercon">; // F024H  XPER Configuration Register
def PT0        : C166Reg<0x18, "pt0">; // F030H  PWM Module Up/Down Counter 0
def PT1        : C166Reg<0x19, "pt1">; // F032H  PWM Module Up/Down Counter 1
def PT2        : C166Reg<0x1A, "pt2">; // F034H  PWM Module Up/Down Counter 2
def PT3        : C166Reg<0x1B, "pt3">; // F036H  PWM Module Up/Down Counter 3
def PP0        : C166Reg<0x1C, "pp0">; // F038H  PWM Module Period Register 0
def PP1        : C166Reg<0x1D, "pp1">; // F03AH  PWM Module Period Register 1
def PP2        : C166Reg<0x1E, "pp2">; // F03CH  PWM Module Period Register 2
def PP3        : C166Reg<0x1F, "pp3">; // F03EH  PWM Module Period Register 3
def T7         : C166Reg<0x28, "t7">; // F050H  CAPCOM Timer 7 Register
def T8         : C166Reg<0x29, "t8">; // F052H  CAPCOM Timer 8 Register
def T7REL      : C166Reg<0x2A, "t7rel">; // F054H  CAPCOM Timer 7 Reload Register
def T8REL      : C166Reg<0x2B, "t8rel">; // F056H  CAPCOM Timer 8 Reload Register
def POCON0L    : C166Reg<0x40, "pocon0l">; // F080H  PORT0 Low Output Control Register (8-bit)
def POCON0H    : C166Reg<0x41, "pocon0h">; // F082H  PORT0 High Output Control Register (8-bit)
def POCON1L    : C166Reg<0x42, "pocon1l">; // F084H  PORT1 Low Output Control Register (8-bit)
def POCON1H    : C166Reg<0x43, "pocon1h">; // F086H  PORT1 High Output Control Register (8-bit)
def POCON2     : C166Reg<0x44, "pocon2">; // F088H  Port2 Output Control Register
def POCON3     : C166Reg<0x45, "pocon3">; // F08AH  Port3 Output Control Register
def POCON4     : C166Reg<0x46, "pocon4">; // F08CH  Port4 Output Control Register (8-bit)
def POCON6     : C166Reg<0x47, "pocon6">; // F08EH  Port6 Output Control Register (8-bit)
def POCON7     : C166Reg<0x48, "pocon7">; // F090H  Port7 Output Control Register (8-bit)
def POCON8     : C166Reg<0x49, "pocon8">; // F092H  Port8 Output Control Register (8-bit)
def ADDAT2     : C166Reg<0x50, "addat2">; // F0A0H  A/D Converter 2 Result Register
def POCON20    : C166Reg<0x55, "pocon20">; // F0AAH  ALE, RD , WR Output Control Register (8-bit)
def SSCTB      : C166Reg<0x58, "ssctb">; // F0B0H  SSC Transmit Buffer (write only)
def SSCRB      : C166Reg<0x59, "sscrb">; // F0B2H  SSC Receive Buffer (read only)
def SSCBR      : C166Reg<0x5A, "sscbr">; // F0B4H  SSC Baud Rate Register
def DP0L       : C166Reg<0x80, "dp0l">; // F100H  P0L Direction Control Register
def DP0H       : C166Reg<0x81, "dp0h">; // F102H  P0h Direction Control Register
def RP0H       : C166Reg<0x84, "rp0h">; // F108H  System Start-up Configuration Register (read only)
def CC16IC     : C166Reg<0xB0, "cc16ic">; // F160H  CAPCOM Register 16 Interrupt Control Register
def CC17IC     : C166Reg<0xB1, "cc17ic">; // F162H  CAPCOM Register 17 Interrupt Control Register
def CC18IC     : C166Reg<0xB2, "cc18ic">; // F164H  CAPCOM Register 18 Interrupt Control Register
def CC19IC     : C166Reg<0xB3, "cc19ic">; // F166H  CAPCOM Register 19 Interrupt Control Register
def CC20IC     : C166Reg<0xB4, "cc20ic">; // F168H  CAPCOM Register 20 Interrupt Control Register
def CC21IC     : C166Reg<0xB5, "cc21ic">; // F16AH  CAPCOM Register 21 Interrupt Control Register
def CC22IC     : C166Reg<0xB6, "cc22ic">; // F16CH  CAPCOM Register 22 Interrupt Control Register
def CC23IC     : C166Reg<0xB7, "cc23ic">; // F16EH  CAPCOM Register 23 Interrupt Control Register
def CC24IC     : C166Reg<0xB8, "cc24ic">; // F170H  CAPCOM Register 24 Interrupt Control Register
def CC25IC     : C166Reg<0xB9, "cc25ic">; // F172H  CAPCOM Register 25 Interrupt Control Register
def CC26IC     : C166Reg<0xBA, "cc26ic">; // F174H  CAPCOM Register 26 Interrupt Control Register
def CC27IC     : C166Reg<0xBB, "cc27ic">; // F176H  CAPCOM Register 27 Interrupt Control Register
def CC28IC     : C166Reg<0xBC, "cc28ic">; // F178H  CAPCOM Register 28 Interrupt Control Register
def T7IC       : C166Reg<0xBD, "t7ic">; // F17AH  CAPCOM Timer 7 Interrupt Control Register
def T8IC       : C166Reg<0xBE, "t8ic">; // F17CH  CAPCOM Timer 8 Interrupt Control Register
def PWMIC      : C166Reg<0xBF, "pwmic">; // F17EH  PWM Module Interrupt Control Register
def CC29IC     : C166Reg<0xC2, "cc29ic">; // F184H  CAPCOM Register 29 Interrupt Control Register
def XP0IC      : C166Reg<0xC3, "xp0ic">; // F186H  CAN1 Module Interrupt Control Register
def CC30IC     : C166Reg<0xC6, "cc30ic">; // F18CH  CAPCOM Register 30 Interrupt Control Register
def XP1IC      : C166Reg<0xC7, "xp1ic">; // F18EH  CAN2 Module Interrupt Control Register
def CC31IC     : C166Reg<0xCA, "cc31ic">; // F194H  CAPCOM Register 31 Interrupt Control Register
def XP2IC      : C166Reg<0xCB, "xp2ic">; // F196H  Flash ready/busy interrupt control register
def S0TBIC     : C166Reg<0xCE, "s0tbic">; // F19CH  Serial Channel 0 Transmit Buffer Interrupt Control Register
def XP3IC      : C166Reg<0xCF, "xp3ic">; // F19EH  PLL unlock Interrupt Control Register
def ODP2       : C166Reg<0xE1, "odp2">; // F1C2H  Port 2 Open Drain Control Register
def PICON      : C166Reg<0xE2, "picon">; // F1C4H  Port Input Threshold Control Register
def ODP4       : C166Reg<0xE5, "odp4">; // F1CAH  Port 4 Open Drain Control Register
def ODP6       : C166Reg<0xE7, "odp6">; // F1CEH  Port 6 Open Drain Control Register
def ODP7       : C166Reg<0xE9, "odp7">; // F1D2H  Port 7 Open Drain Control Register
def ODP8       : C166Reg<0xEB, "odp8">; // F1D6H  Port 8 Open Drain Control Register
def EXISEL     : C166Reg<0xED, "exisel">; // F1DAH  External Interrupt Source Selection Register

//===----------------------------------------------------------------------===//
// Registers with no short address
//===----------------------------------------------------------------------===//

// These have no short address at all - the manual writes it as "--" - so no
// "reg" field can name one, and nothing below is in a class that a "reg" field
// uses.  They carry their 16 bit address as the hardware encoding rather than
// a short address, because that is all there is to carry.  All of them are
// inside the page DPP3 selects, so an ordinary near address reaches them.

// Where the oscillator and the PLL report what they are doing; crt0.S waits on
// its PLLLOCK, bit 14, before switching the CPU onto the PLL.  This one is not
// an X-peripheral - it is in the extended space beside PLLCON and SYSCON1 -
// and is here only because it is reachable the same way they are and no other.
def SYSSTAT       : C166Reg<0xF1E4, "sysstat">; // F1E4H, no short address

// The rest are the on-chip X-peripherals, mapped at E800H and up.  This is
// what the XC164CM Peripheral Units manual names in the per-register form: the
// CAPCOM6 unit and the interrupt jump table cache.  Three more addresses it
// annotates - E8AEH, E8C4H and E8CEH - are left out because the name that goes
// with each did not come out of the document.  The CAN module's registers are
// not here either; that part has its own manual.
//
// ADDRSEL7 and TCONCS7 are the LXBus controller's, which is the on-chip bus
// the X-peripherals hang off.  That is a different thing from the external bus
// whose ADDRSEL1 to ADDRSEL4 are absent from this part, and the reason both
// statements are true at once.
def CCU6_T12      : C166Reg<0xE890, "ccu6_t12">; // X-peripheral
def CCU6_T12PR    : C166Reg<0xE892, "ccu6_t12pr">; // X-peripheral
def CCU6_T12DTC   : C166Reg<0xE894, "ccu6_t12dtc">; // X-peripheral
def CCU6_CC60R    : C166Reg<0xE898, "ccu6_cc60r">; // X-peripheral
def CCU6_CC61R    : C166Reg<0xE89A, "ccu6_cc61r">; // X-peripheral
def CCU6_CC62R    : C166Reg<0xE89C, "ccu6_cc62r">; // X-peripheral
def CCU6_TCTR4    : C166Reg<0xE8A6, "ccu6_tctr4">; // X-peripheral
def CCU6_CMPSTAT  : C166Reg<0xE8A8, "ccu6_cmpstat">; // X-peripheral
def CCU6_TCTR0    : C166Reg<0xE8AC, "ccu6_tctr0">; // X-peripheral
def CCU6_T13      : C166Reg<0xE8B0, "ccu6_t13">; // X-peripheral
def CCU6_T13PR    : C166Reg<0xE8B2, "ccu6_t13pr">; // X-peripheral
def CCU6_CC63R    : C166Reg<0xE8B4, "ccu6_cc63r">; // X-peripheral
def CCU6_MODCTR   : C166Reg<0xE8C0, "ccu6_modctr">; // X-peripheral
def CCU6_TRPCTR   : C166Reg<0xE8C2, "ccu6_trpctr">; // X-peripheral
def CCU6_T12MSEL  : C166Reg<0xE8C6, "ccu6_t12msel">; // X-peripheral
def CCU6_MCMOUT   : C166Reg<0xE8CC, "ccu6_mcmout">; // X-peripheral
def CCU6_IS       : C166Reg<0xE8D0, "ccu6_is">; // X-peripheral
def CCU6_ISS      : C166Reg<0xE8D2, "ccu6_iss">; // X-peripheral
def CCU6_ISR      : C166Reg<0xE8D4, "ccu6_isr">; // X-peripheral
def CCU6_INP      : C166Reg<0xE8D6, "ccu6_inp">; // X-peripheral
def CCU6_IEN      : C166Reg<0xE8D8, "ccu6_ien">; // X-peripheral
def FINT0CSP      : C166Reg<0xEC00, "fint0csp">; // X-peripheral
def FINT0ADDR     : C166Reg<0xEC02, "fint0addr">; // X-peripheral
def FINT1CSP      : C166Reg<0xEC04, "fint1csp">; // X-peripheral
def FINT1ADDR     : C166Reg<0xEC06, "fint1addr">; // X-peripheral
def TCONCS7       : C166Reg<0xEE48, "tconcs7">; // X-peripheral
def ADDRSEL7      : C166Reg<0xEE4E, "addrsel7">; // X-peripheral

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

// Non-allocatable special function registers, in three groups.
//
// The short address a register carries is its encoding, and the same short
// address names different registers on different derivatives - so the map is
// a property of the part rather than of the family, and one register file
// cannot be right about all of them at once.  What it can do is say which
// group each register belongs to and let the subtarget pick: the assembler
// refuses a name outside the chosen map and the disassembler does not offer
// it, which is what stops a listing for one part being read as another.
//
// The encodings are not part-specific and never were.  Only the names are, so
// this is a naming layer over one set of instructions rather than a second
// instruction set.

// Registers both maps here agree about, name and short address alike.  That
// is the classic C166 layout: the timers, the A/D converter, the serial
// channels, the peripheral event controller, the interrupt control registers
// and ports 1, 3 and 5, on top of the CPU core proper.  The coprocessor's
// registers ride along here too - they belong to the MAC unit rather than to
// a derivative, and HasMAC is what says whether that unit is there.
def SFRCommon : RegisterClass<"C166", [i16], 16,
  (add
      DPP0, DPP1, DPP2, DPP3, CSP, MDH, MDL, CP, SYSSP, STKOV, STKUN, PSW,
      MDC, ZEROS, ONES, TFR, T2, T3, T4, T5, T6, CAPREL, T2CON, T3CON,
      T4CON, T5CON, T6CON, T78CON, CC16, CC17, CC18, CC19, CC20, CC21,
      CC22, CC23, CC24, CC25, CC26, CC27, CC28, CC29, CC30, CC31, CCM4,
      CCM5, CCM6, CCM7, ADDAT, ADCON, WDTCON, S0TBUF, S0RBUF, S0BG, S0CON,
      SSCCON, PECC0, PECC1, PECC2, PECC3, PECC4, PECC5, PECC6, PECC7, T2IC,
      T3IC, T4IC, T5IC, T6IC, CRIC, S0TIC, S0RIC, S0EIC, SSCTIC, SSCRIC,
      SSCEIC, CC8IC, CC9IC, CC10IC, CC11IC, CC12IC, CC13IC, ADCIC, ADEIC,
      P1L, P1H, P3, DP3, P5, P5DIDIS, IDX0, IDX1, MAL, MAH, MRW,
      MCW, MSW
  )> {
  let isAllocatable = 0;
  let CopyCost = -1;
}

// The XC164CM's, selected by FeatureSFRXC164.
def SFRXC164 : RegisterClass<"C166", [i16], 16,
  (add
      CPUCON1, CPUCON2, SPSEG, VECSEG, P9, DP9, ODP9, FOCON
  )> {
  let isAllocatable = 0;
  let CopyCost = -1;
}

// The C167CS's, selected by FeatureSFRC167.
def SFRC167 : RegisterClass<"C166", [i16], 16,
  (add
      ADDRSEL1, ADDRSEL2, ADDRSEL3, ADDRSEL4, PW0, PW1, PW2, PW3, T0, T1,
      T0REL, T1REL, CC0, CC1, CC2, CC3, CC4, CC5, CC6, CC7, CC8, CC9, CC10,
      CC11, CC12, CC13, CC14, CC15, P1DIDIS, WDT, P0L, P0H, BUSCON0,
      SYSCON, BUSCON1, BUSCON2, BUSCON3, BUSCON4, PWMCON0, PWMCON1, T01CON,
      CCM0, CCM1, CCM2, CCM3, CC0IC, CC1IC, CC2IC, CC3IC, CC4IC, CC5IC,
      CC6IC, CC7IC, CC14IC, CC15IC, T0IC, T1IC, P2, DP2, P4, DP4, P6, DP6,
      P7, DP7, P8, DP8, FOCON
  )> {
  let isAllocatable = 0;
  let CopyCost = -1;
}

// The ST10F269's, selected by FeatureSFRST10.  Every one of the 163 names its
// datasheet gives a short address to is a name the two classes above between
// them also have, at the same short address - which is what says both were
// read correctly, and is why this is the C167's map rather than a third copy
// of it.  Like its two siblings this is what to add to the shared set, not
// the whole map: naming SFRCommon here would make every register every part
// has depend on a feature only one part sets.
//
// Two names are the exception and the reason for the subtraction, and in both
// cases the C167CS has the register and the ST10F269 has nothing at that
// short address at all: P1DIDIS at 52H and FOCON at D5H.  FOCON also had to
// come out of SFRCommon, which claims to be what every map here agrees about
// - true of two maps and made false by a third that lacks it - so it now sits
// in the two maps that do have it.
def SFRST10 : RegisterClass<"C166", [i16], 16,
  (add (sub SFRC167, P1DIDIS, FOCON))> {
  let isAllocatable = 0;
  let CopyCost = -1;
}

// Everything a "reg" field could name on some part.  This is what the operand
// classes below are built from; which of it is live is the subtarget's answer,
// asked at parse time and at decode time rather than baked in here.
def SFR : RegisterClass<"C166", [i16], 16,
  (add SFRCommon, SFRXC164, SFRC167)> {
  let isAllocatable = 0;
  let CopyCost = -1;
}

// Everything the 8 bit "reg" field can name.  There are two of these because
// F0H + n is a word register in a word instruction and a byte register in a
// byte one, and the special function registers are reachable from either.
// They exist only to widen that field past the general purpose registers, so
// neither is an allocation class of its own.
// The pointers the ALU's indirect forms can use.  Their field is two bits
// wide, so only the first four general purpose registers fit in it.
def GR16P : RegisterClass<"C166", [i16], 16, (add R0, R1, R2, R3)>;

def GR16orSFR : RegisterClass<"C166", [i16], 16, (add GR16, SFR)> {
  let isAllocatable = 0;
  let CopyCost = -1;
}

def GR8orSFR : RegisterClass<"C166", [i8], 8, (add GR8, SFR)> {
  let isAllocatable = 0;
  let CopyCost = -1;
}

// The extended special function registers.  This is deliberately not part of
// either class above: a "reg" field cannot tell one of these from the SFR with
// the same short address, so they are reachable by address only.
def ESFR : RegisterClass<"C166", [i16], 16,
  (add
      QX0, QX1, QR0, QR1, CC2_IOC, IDPROG, IDMEM, IDCHIP, IDMANUF,
      ADC_CTR2, ADC_CTR2IN, ADC_DAT2, SCUSLC, RTC_RELL, RTC_RELH,
      RTC_RTCL, RTC_RTCH, IMBCTR, DP1L, DP1H, RTC_ISNC, RTC_CON,
      ALTSEL0P1H, ALTSEL0P3, ALTSEL0P1L, ALTSEL0P9, ALTSEL1P9, EOPIC,
      PLL_IC, RTC_IC, EXICON, ODP3, PLLCON, SYSCON3, EXISEL1, EXISEL0,
      SYSCON1)> {
  let isAllocatable = 0;
  let CopyCost = -1;
}

// The ST10F269's, which are a different set at overlapping addresses - see
// the definitions for the three that collide and for what T7IC and T8IC
// settle.  Reached by address only, for the same reason as the class above.
def ESFRST10 : RegisterClass<"C166", [i16], 16,
  (add
      QX0, QX1, QR0, QR1, XPERCON, PT0, PT1, PT2, PT3, PP0, PP1, PP2, PP3,
      T7, T8, T7REL, T8REL, IDPROG, IDMEM, IDCHIP, IDMANUF, POCON0L,
      POCON0H, POCON1L, POCON1H, POCON2, POCON3, POCON4, POCON6, POCON7,
      POCON8, ADDAT2, POCON20, SSCTB, SSCRB, SSCBR, DP0L, DP0H, DP1L,
      DP1H, RP0H, CC16IC, CC17IC, CC18IC, CC19IC, CC20IC, CC21IC, CC22IC,
      CC23IC, CC24IC, CC25IC, CC26IC, CC27IC, CC28IC, T7IC, T8IC, PWMIC,
      CC29IC, XP0IC, CC30IC, XP1IC, CC31IC, XP2IC, S0TBIC, XP3IC, EXICON,
      ODP2, PICON, ODP3, ODP4, ODP6, ODP7, ODP8, EXISEL)> {
  let isAllocatable = 0;
  let CopyCost = -1;
}

// Every extended special function register, whichever map it came from.  A
// "reg" field that names one has to be one operand class, because which map a
// name belongs to is not the matcher's question - isSFRInSelectedMap is where
// that is asked, and it is asked of the name rather than of the field.  The
// two classes overlap in the coprocessor's four offset registers, which belong
// to the unit rather than to either part.
def ESFRAny : RegisterClass<"C166", [i16], 16, (add ESFR, ESFRST10)> {
  let isAllocatable = 0;
  let CopyCost = -1;
}

// The two pointers the MAC unit's indirect forms run on.
def CoIDX : RegisterClass<"C166", [i16], 16, (add IDX0, IDX1)> {
  let isAllocatable = 0;
  let CopyCost = -1;
}

// What CoSTORE can name.  These are addressed by a five bit code of their own
// rather than by a short address, so this class exists to say which registers
// the code covers; the encoder maps each to its code.
// The MAC unit's offset registers, which live in the extended space but belong
// to the coprocessor rather than to any one part's peripheral map.  They are a
// class of their own so that they can be gated on the unit being there instead
// of on whose map is selected: the ST10F269 datasheet puts QX0 at F000H, QX1 at
// F002H, QR0 at F004H and QR1 at F006H, which is where the XC164CM User's
// Manual puts them too, so an ST10 with a MAC has the same four at the same
// addresses.  IDX0 and IDX1 are the same kind of register and need no class
// because they are in the ordinary space, which every part shares.
def CoOFFS : RegisterClass<"C166", [i16], 16, (add QX0, QX1, QR0, QR1)> {
  let isAllocatable = 0;
  let CopyCost = -1;
}

def CoREG : RegisterClass<"C166", [i16], 16,
  (add MSW, MAH, MAS, MAL, MCW, MRW)> {
  let isAllocatable = 0;
  let CopyCost = -1;
}

// The registers reachable by address only.  Like the extended ones above they
// are not reachable through a "reg" field, and for a stronger reason: they have
// no short address at all, so there is nothing a "reg" field could carry.
def XSFR : RegisterClass<"C166", [i16], 16,
  (add
      CCU6_T12, CCU6_T12PR, CCU6_T12DTC, CCU6_CC60R, CCU6_CC61R,
      CCU6_CC62R, CCU6_TCTR4, CCU6_CMPSTAT, CCU6_TCTR0, CCU6_T13,
      CCU6_T13PR, CCU6_CC63R, CCU6_MODCTR, CCU6_TRPCTR, CCU6_T12MSEL,
      CCU6_MCMOUT, CCU6_IS, CCU6_ISS, CCU6_ISR, CCU6_INP, CCU6_IEN,
      FINT0CSP, FINT0ADDR, FINT1CSP, FINT1ADDR, TCONCS7, ADDRSEL7,
      SYSSTAT)> {
  let isAllocatable = 0;
  let CopyCost = -1;
}
