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

// The condition flags live in the PSW.
def PSW   : C166Reg<0, "psw">,   DwarfRegNum<[32]>;
// Multiply/divide unit registers.
def MDL   : C166Reg<0, "mdl">,   DwarfRegNum<[33]>;
def MDH   : C166Reg<0, "mdh">,   DwarfRegNum<[34]>;
def MDC   : C166Reg<0, "mdc">,   DwarfRegNum<[35]>;
// The hardware (system) stack pointer.  It only ever holds return addresses
// and explicitly PUSHed values; the ABI stack lives in R0.
def SYSSP : C166Reg<0, "sp">,    DwarfRegNum<[36]>;
// Context pointer.
def CP    : C166Reg<0, "cp">,    DwarfRegNum<[37]>;

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
  (add PSW, MDL, MDH, MDC, SYSSP, CP)> {
  let isAllocatable = 0;
  let CopyCost = -1;
}
