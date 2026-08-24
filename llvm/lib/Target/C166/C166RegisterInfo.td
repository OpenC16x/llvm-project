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

// External bus: the address windows and their bus configurations.
def CPUCON1  : C166Reg<0x0C, "cpucon1">; // FE18H
def CPUCON2  : C166Reg<0x0D, "cpucon2">; // FE1AH
def SPSEG    : C166Reg<0x86, "spseg">; // FF0CH
def VECSEG   : C166Reg<0x89, "vecseg">; // FF12H

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
// Port 9 sits in the block the external bus registers occupy on a part that
// has an external bus; the XC164CM has none.
def P9       : C166Reg<0x8B, "p9">; // FF16H
def DP9      : C166Reg<0x8C, "dp9">; // FF18H
def ODP9     : C166Reg<0x8D, "odp9">; // FF1AH
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
// which half is the typo.

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

// Non-allocatable special function registers.
def SFR : RegisterClass<"C166", [i16], 16,
  (add 
      
      DPP0, DPP1, DPP2, DPP3, CSP, MDH, MDL, CP, SYSSP, STKOV, STKUN,
      CPUCON1, CPUCON2, SPSEG, VECSEG, P9, DP9, ODP9, PSW, MDC, ZEROS,
      ONES, TFR, T2, T3, T4, T5, T6, CAPREL, T2CON, T3CON, T4CON, T5CON,
      T6CON, T78CON, CC16, CC17, CC18, CC19, CC20, CC21, CC22, CC23, CC24,
      CC25, CC26, CC27, CC28, CC29, CC30, CC31, CCM4, CCM5, CCM6, CCM7,
      ADDAT, ADCON, WDTCON, S0TBUF, S0RBUF, S0BG, S0CON, SSCCON, PECC0,
      PECC1, PECC2, PECC3, PECC4, PECC5, PECC6, PECC7, T2IC, T3IC, T4IC,
      T5IC, T6IC, CRIC, S0TIC, S0RIC, S0EIC, SSCTIC, SSCRIC, SSCEIC,
      CC8IC, CC9IC, CC10IC, CC11IC, CC12IC, CC13IC, ADCIC, ADEIC, P1L,
      P1H, P3, DP3, P5, P5DIDIS, FOCON, IDX0, IDX1, MAL, MAH, MRW, MCW,
      MSW
  )> {
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

// The two pointers the MAC unit's indirect forms run on.
def CoIDX : RegisterClass<"C166", [i16], 16, (add IDX0, IDX1)> {
  let isAllocatable = 0;
  let CopyCost = -1;
}

// What CoSTORE can name.  These are addressed by a five bit code of their own
// rather than by a short address, so this class exists to say which registers
// the code covers; the encoder maps each to its code.
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
