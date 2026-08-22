//===-- Machine.h - C166 machine state --------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// The state a C166 program can see, and the addressing that gets at it.
// Everything here that is not obvious is from the C166 Family Instruction Set
// Manual (V2.0, 2001-03) or the SAB 80C166 User's Manual; the citations are on
// the declarations.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_UTILS_C166SIM_MACHINE_H
#define LLVM_UTILS_C166SIM_MACHINE_H

#include "llvm/ADT/StringRef.h"
#include "llvm/Support/raw_ostream.h"
#include <cstdint>
#include <string>
#include <vector>

namespace c166sim {

/// The whole bus is 24 bits: 256 segments of 64 KByte.
static constexpr uint32_t AddressSpaceSize = 1u << 24;
static constexpr uint32_t AddressMask = AddressSpaceSize - 1;

/// The special function registers are memory mapped at the top of segment 0.
static constexpr uint32_t SFRBase = 0x00FE00;
static constexpr uint32_t SFREnd = 0x010000;

/// PSW bit positions, from the register figure in the instruction set manual:
/// ILVL is 15..12, IEN 11, HLDEN 10, then USR0, MULIP and the five condition
/// flags at the bottom.
enum PSWBit {
  PSW_N = 0,
  PSW_C = 1,
  PSW_V = 2,
  PSW_Z = 3,
  PSW_E = 4,
  PSW_MULIP = 5,
  PSW_USR0 = 6,
  PSW_HLDEN = 10,
  PSW_IEN = 11,
};

/// Reset values, from the SAB 80C166 register table.  The register bank sits
/// at 00'FC00H with the system stack growing down into 00'FA00H below it, so
/// the two do not overlap even though CP and SP come up equal.
enum ResetValue : uint16_t {
  ResetCP = 0xFC00,
  ResetSP = 0xFC00,
  ResetSTKOV = 0xFA00,
  ResetSTKUN = 0xFC00,
};

/// What an EXTend instruction did to the addressing of the instructions that
/// follow it.
enum class ExtendKind { None, Page, Segment };

/// Why the machine stopped.
enum class StopReason {
  Running,
  Exited,      ///< reached the exit symbol
  Unsupported, ///< an instruction this simulator does not implement
  BadAccess,   ///< a fetch or access that cannot be satisfied
  StackFault,  ///< the system stack left the STKOV/STKUN window
  StepLimit,   ///< ran longer than --max-steps
  Halted,      ///< SRST, or PWRDN/IDLE with nothing to wake it
};

class Machine {
public:
  Machine();

  // -- memory ------------------------------------------------------------

  /// Physical (24 bit) access.  Anything in the SFR window is a register
  /// rather than storage, and anything in the top segment is a simulator
  /// port; see Machine.cpp.
  uint8_t read8(uint32_t Phys);
  uint16_t read16(uint32_t Phys);
  void write8(uint32_t Phys, uint8_t V);
  void write16(uint32_t Phys, uint16_t V);

  /// Plain storage, with no SFR or port behaviour.  This is what the loader
  /// writes through.
  void poke8(uint32_t Phys, uint8_t V) { Mem[Phys & AddressMask] = V; }

  /// Turn a 16 bit "long" (mem) or indirect address into a physical one.  By
  /// default the top two bits pick a DPP register whose ten bits become
  /// address bits 23..14; EXTP replaces the page outright and EXTS treats the
  /// address as a 16 bit offset into an explicit segment.  (Instruction set
  /// manual, "DPP Override Mechanism" and the EXTP/EXTS pages.)
  uint32_t mapData(uint16_t Addr) const;

  // -- registers ---------------------------------------------------------

  /// R0 to R15 are a window into internal RAM at CP, so they are storage
  /// rather than registers and code may reach them either way.
  uint16_t getWordReg(unsigned N) const;
  void setWordReg(unsigned N, uint16_t V);
  /// RL0, RH0, RL1 ... are the bytes of that same window in order.
  uint8_t getByteReg(unsigned N) const;
  void setByteReg(unsigned N, uint8_t V);

  /// The 8 bit "reg" field: F0H + n is a GPR, anything else is an SFR short
  /// address, which is FE00H + 2N below 80H and FF00H + 2*(N-80H) at or above
  /// it.
  uint32_t regFieldAddress(unsigned Reg) const;

  bool flag(PSWBit B) const { return (PSW >> B) & 1; }
  void setFlag(PSWBit B, bool V) {
    PSW = (PSW & ~(uint16_t(1) << B)) | (uint16_t(V) << B);
  }

  // -- the system stack --------------------------------------------------
  //
  // It holds return addresses, grows down, and is checked against STKOV and
  // STKUN on every access.
  void push(uint16_t V);
  uint16_t pop();

  // -- execution ---------------------------------------------------------

  /// Run one instruction.  Returns false once Stop is no longer Running.
  bool step();

  /// Count down an EXTend sequence.  Called once per instruction, after the
  /// instruction has used the override.
  void retireExtend();

  StopReason Stop = StopReason::Running;
  std::string StopDetail;
  uint16_t ExitCode = 0;

  // Registers that are not in memory.
  uint16_t PSW = 0;
  // Where the interrupt vector table is and how far apart its entries are.
  // The table is at the low end of the segment VECSEG names, and CPUCON1's
  // VECSC field scales the space between entries; both are XC16x registers
  // that the older parts did without, which is why a trap on those always
  // reached segment 0 at four bytes a vector.  The reset value of VECSEG is a
  // property of how the part was started, so the loader sets it to the segment
  // the reset vector came from.
  uint16_t VECSEG = 0;
  uint16_t CPUCON1 = 0x0007;
  // The PLL, as far as this models it: what was written to PLLCON, and the step
  // at which the VCO is considered locked, which is all SYSSTAT's PLLLOCK is
  // derived from.  The reset value of PLLCON is the part's, which has the VCO
  // running with the CPU on the oscillator, so lock arrives on its own even for
  // a program that never writes the register.
  static constexpr uint64_t PLLLockDelay = 100;
  static constexpr uint64_t NeverLocks = ~uint64_t(0);
  uint16_t PLLCON = 0x2700;
  uint64_t PLLLockStep = PLLLockDelay;
  uint16_t CP = ResetCP;
  uint16_t SP = ResetSP;
  uint16_t STKOV = ResetSTKOV;
  uint16_t STKUN = ResetSTKUN;
  uint16_t MDH = 0, MDL = 0, MDC = 0;
  uint16_t DPP[4] = {0, 1, 2, 3};
  uint16_t CSP = 0;
  uint16_t IP = 0;

  // EXTend state.
  ExtendKind Extend = ExtendKind::None;
  uint32_t ExtendValue = 0;
  unsigned ExtendCount = 0;

  /// Where the program is considered finished, and where its result is.  The
  /// harness fills these in from the ELF symbol table.
  uint32_t ExitAddress = ~0u;
  bool HasExitAddress = false;

  uint64_t Steps = 0;
  uint64_t MaxSteps = 0; ///< 0 means no limit

  bool Trace = false;
  llvm::raw_ostream *TraceOS = nullptr;

  /// Console output written through the simulator port.
  llvm::raw_ostream *ConsoleOS = nullptr;

private:
  friend struct Executor;
  std::vector<uint8_t> Mem;
};

// Two addresses in the top segment are the simulator rather than storage.  No
// C166 part populates that segment and no linker script here places anything
// in it, and a program reaches it with a far pointer or an EXTS.
//
/// A byte written here appears on the simulator's console.
static constexpr uint32_t ConsolePort = 0xFF0000;
/// A word written here stops the program with that value as its result.  A
/// program linked against a real crt0 does not need this - reaching
/// __c166_exit is enough - but it lets a test be a handful of instructions
/// with no symbols and nothing to link.
static constexpr uint32_t ExitPort = 0xFF0002;

} // namespace c166sim

#endif
