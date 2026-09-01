//===-- Machine.cpp - C166 memory, registers and addressing ---------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "Machine.h"
#include "llvm/Support/Format.h"

using namespace c166sim;
using namespace llvm;

Machine::Machine() : Mem(AddressSpaceSize, 0) {}

// The SFRs that this simulator models are the CPU's own; a peripheral register
// reads back what was written to it and does nothing else, which is enough to
// run code that configures peripherals it then never waits on.
static constexpr uint32_t SFR_DPP0 = 0xFE00, SFR_DPP1 = 0xFE02;
static constexpr uint32_t SFR_DPP2 = 0xFE04, SFR_DPP3 = 0xFE06;
static constexpr uint32_t SFR_CSP = 0xFE08;
static constexpr uint32_t SFR_MDH = 0xFE0C, SFR_MDL = 0xFE0E;
static constexpr uint32_t SFR_CP = 0xFE10, SFR_SP = 0xFE12;
static constexpr uint32_t SFR_STKOV = 0xFE14, SFR_STKUN = 0xFE16;
static constexpr uint32_t SFR_MDC = 0xFF0E, SFR_PSW = 0xFF10;
static constexpr uint32_t SFR_CPUCON1 = 0xFE18, SFR_VECSEG = 0xFF12;

// The coprocessor's accumulator, reachable by address as well as through
// CoSTORE.  An interrupt handler that uses the MAC unit saves and restores it
// this way, so these have to be the accumulator itself and not storage that
// happens to sit at the same addresses.  MRW is not here: nothing modelled
// reads or writes it, so it behaves as the storage everything unmodelled does.
static constexpr uint32_t SFR_MAL = 0xFE5C, SFR_MAH = 0xFE5E;
static constexpr uint32_t SFR_MCW = 0xFFDC, SFR_MSW = 0xFFDE;

// The clock, which is modelled only as far as startup code needs: PLLCON reads
// back what was written, and SYSSTAT reports the PLL locked once the VCO is
// running.  That is the one thing a peripheral register reading back its own
// value would get wrong, because startup code does wait on it - crt0.S spins on
// PLLLOCK before switching the CPU over, and against storage that spin would
// never end.  These two are in the extended space, which is why their addresses
// are below the SFR window rather than in it.
//
// Locking means the VCO has something to lock to, which is PLLCTRL = 01B or
// 11B; both have bit 13 set, and 10B, where the VCO free-runs with the
// oscillator input off, does not.  So a program that waits for lock without
// starting the VCO waits forever here, which is what the part does.
//
// And it takes time, which is why the spin loop exists at all, so PLLLOCK comes
// up clear and reads set a fixed number of instructions after the PLL was told
// to run.  The number is arbitrary - this simulator has no clock to convert a
// lock time into instructions - and is only large enough that a wait loop goes
// round rather than falling straight through.
static constexpr uint32_t ESFR_PLLCON = 0xF1D0, ESFR_SYSSTAT = 0xF1E4;
static constexpr uint16_t PLLCON_VCO_LOCKS = 1u << 13;
static constexpr uint16_t SYSSTAT_PLLLOCK = 1u << 14;

/// True when Phys names one of the CPU registers rather than storage.
static bool isCPUSFR(uint32_t Phys) {
  switch (Phys) {
  case SFR_DPP0:
  case SFR_DPP1:
  case SFR_DPP2:
  case SFR_DPP3:
  case SFR_CSP:
  case SFR_MDH:
  case SFR_MDL:
  case SFR_MAL:
  case SFR_MAH:
  case SFR_MCW:
  case SFR_MSW:
  case SFR_CP:
  case SFR_SP:
  case SFR_STKOV:
  case SFR_STKUN:
  case SFR_MDC:
  case SFR_PSW:
  case ESFR_PLLCON:
  case ESFR_SYSSTAT:
    return true;
  // Only on a part that has them.  On one that does not, these two addresses
  // are a different register - FF12H is a C167's SYSCON - and modelling them
  // here would be reading whatever that part's startup code wrote there as a
  // vector table location.
  case SFR_CPUCON1:
  case SFR_VECSEG:
    return simCPUHasVectorRegs();
  default:
    return false;
  }
}

uint16_t Machine::read16(uint32_t Phys) {
  Phys &= AddressMask;
  if (isCPUSFR(Phys)) {
    switch (Phys) {
    case SFR_DPP0:
      return DPP[0];
    case SFR_DPP1:
      return DPP[1];
    case SFR_DPP2:
      return DPP[2];
    case SFR_DPP3:
      return DPP[3];
    case SFR_CSP:
      return CSP;
    case SFR_MDH:
      return MDH;
    case SFR_MDL:
      return MDL;
    case SFR_MAL:
      return uint16_t(uint64_t(ACC) & 0xFFFF);
    case SFR_MAH:
      return uint16_t((uint64_t(ACC) >> 16) & 0xFFFF);
    case SFR_MCW:
      return MCW;
    case SFR_MSW:
      // The low byte is MAE, the accumulator's top eight bits; the rest is
      // carried rather than generated.
      return MSWFlags | uint16_t((uint64_t(ACC) >> 32) & 0xFF);
    case SFR_CP:
      return CP;
    case SFR_SP:
      return SP;
    case SFR_STKOV:
      return STKOV;
    case SFR_STKUN:
      return STKUN;
    case SFR_MDC:
      return MDC;
    case SFR_CPUCON1:
      return CPUCON1;
    case SFR_VECSEG:
      return VECSEG;
    case SFR_PSW:
      return PSW;
    case ESFR_PLLCON:
      return PLLCON;
    case ESFR_SYSSTAT:
      // Only PLLLOCK is modelled; the oscillator watchdog and the clock loss
      // detectors that share this register read back as nothing reported.
      return Steps >= PLLLockStep ? SYSSTAT_PLLLOCK : 0;
    }
  }
  return uint16_t(Mem[Phys]) | (uint16_t(Mem[(Phys + 1) & AddressMask]) << 8);
}

void Machine::write16(uint32_t Phys, uint16_t V) {
  Phys &= AddressMask;
  if (isCPUSFR(Phys)) {
    switch (Phys) {
    case SFR_DPP0:
      DPP[0] = V & 0x3FF;
      return;
    case SFR_DPP1:
      DPP[1] = V & 0x3FF;
      return;
    case SFR_DPP2:
      DPP[2] = V & 0x3FF;
      return;
    case SFR_DPP3:
      DPP[3] = V & 0x3FF;
      return;
    case SFR_CSP:
      CSP = V;
      return;
    case SFR_MDH:
      MDH = V;
      return;
    case SFR_MDL:
      MDL = V;
      return;
    case SFR_MAL:
      setACC((ACC & ~INT64_C(0xFFFF)) | V);
      return;
    case SFR_MAH:
      // Writing a word to MAH zeroes MAL and sign extends the extension byte,
      // which is why a handler putting the accumulator back has to restore MAH
      // first and MAL and MSW on top of what this cleared.
      setACC(int64_t(int16_t(V)) << 16);
      return;
    case SFR_MCW:
      MCW = V;
      return;
    case SFR_MSW:
      MSWFlags = V & 0xFF00;
      setACC((ACC & INT64_C(0xFFFFFFFF)) | (int64_t(V & 0xFF) << 32));
      return;
    case SFR_CP:
      CP = V;
      return;
    case SFR_SP:
      SP = V;
      return;
    case SFR_STKOV:
      STKOV = V;
      return;
    case SFR_STKUN:
      STKUN = V;
      return;
    case SFR_CPUCON1:
      CPUCON1 = V;
      return;
    case SFR_VECSEG:
      VECSEG = V & 0xFF;
      return;
    case SFR_MDC:
      MDC = V;
      return;
    case SFR_PSW:
      PSW = V;
      return;
    case ESFR_PLLCON:
      PLLCON = V;
      PLLLockStep = (V & PLLCON_VCO_LOCKS) ? Steps + PLLLockDelay : NeverLocks;
      return;
    case ESFR_SYSSTAT:
      // Read only, and writing it is not an error.
      return;
    }
  }
  if (Phys == ExitPort) {
    Stop = StopReason::Exited;
    ExitCode = V;
    return;
  }
  Mem[Phys] = V & 0xFF;
  Mem[(Phys + 1) & AddressMask] = V >> 8;
}

uint8_t Machine::read8(uint32_t Phys) {
  Phys &= AddressMask;
  if (isCPUSFR(Phys & ~1u))
    return (read16(Phys & ~1u) >> ((Phys & 1) * 8)) & 0xFF;
  return Mem[Phys];
}

void Machine::write8(uint32_t Phys, uint8_t V) {
  Phys &= AddressMask;
  // A byte written to the console port is output rather than storage.  It is
  // the one thing in this simulator that a program can observe from outside.
  if (Phys == ConsolePort) {
    if (ConsoleOS)
      *ConsoleOS << char(V);
    return;
  }
  // A byte written to a word wide special function register does not leave the
  // other half alone: "byte write operations to word wide SFRs via indirect or
  // direct 16-bit (mem) addressing or byte transfers via the PEC force zeros in
  // the non-addressed byte.  Byte write operations via short 8-bit (reg)
  // addressing can only access the low byte of an SFR and force zeros in the
  // high byte."  Both cases come out the same here: the half not written reads
  // back as zero.
  if (isCPUSFR(Phys & ~1u)) {
    write16(Phys & ~1u, uint16_t(V) << ((Phys & 1) * 8));
    return;
  }
  Mem[Phys] = V;
}

uint32_t Machine::mapData(uint16_t Addr) const {
  switch (Extend) {
  case ExtendKind::Page:
    // EXTP: address bits 23..14 are op1, 13..0 come from the address.
    return ((ExtendValue & 0x3FF) << 14) | (Addr & 0x3FFF);
  case ExtendKind::Segment:
    // EXTS: the address is a 16 bit offset into the named segment.
    return ((ExtendValue & 0xFF) << 16) | Addr;
  case ExtendKind::None:
    break;
  }
  // The standard scheme: the top two bits pick a DPP, whose ten bits are
  // address bits 23..14.
  return (uint32_t(DPP[(Addr >> 14) & 3] & 0x3FF) << 14) | (Addr & 0x3FFF);
}

uint16_t Machine::getWordReg(unsigned N) const {
  uint32_t A = uint16_t(CP + 2 * N);
  return uint16_t(Mem[A]) | (uint16_t(Mem[A + 1]) << 8);
}

void Machine::setWordReg(unsigned N, uint16_t V) {
  uint32_t A = uint16_t(CP + 2 * N);
  Mem[A] = V & 0xFF;
  Mem[A + 1] = V >> 8;
}

uint8_t Machine::getByteReg(unsigned N) const {
  // RL0, RH0, RL1, RH1 ... are consecutive bytes of the same window.
  return Mem[uint16_t(CP + N)];
}

void Machine::setByteReg(unsigned N, uint8_t V) { Mem[uint16_t(CP + N)] = V; }

uint32_t Machine::regFieldAddress(unsigned Reg) const {
  Reg &= 0xFF;
  if (Reg >= 0xF0)
    return uint16_t(CP + 2 * (Reg - 0xF0));
  if (Reg < 0x80)
    return 0xFE00 + 2 * Reg;
  return 0xFF00 + 2 * (Reg - 0x80);
}

void Machine::push(uint16_t V) {
  SP -= 2;
  if (SP < STKOV) {
    Stop = StopReason::StackFault;
    StopDetail = "system stack overflow: SP below STKOV";
    return;
  }
  write16(SP, V);
}

uint16_t Machine::pop() {
  if (SP >= STKUN) {
    Stop = StopReason::StackFault;
    StopDetail = "system stack underflow: SP at or above STKUN";
    return 0;
  }
  uint16_t V = read16(SP);
  SP += 2;
  return V;
}

void Machine::retireExtend() {
  if (Extend == ExtendKind::None && ExtendCount == 0)
    return;
  if (ExtendCount > 0 && --ExtendCount == 0)
    Extend = ExtendKind::None;
}
