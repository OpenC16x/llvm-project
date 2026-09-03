//===-- Machine.cpp - C166 memory, registers and addressing ---------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "Machine.h"
#include "llvm/ADT/Twine.h"
#include "llvm/Support/Format.h"

using namespace c166sim;
using namespace llvm;

Machine::Machine() : Mem(AddressSpaceSize, 0) {}

// The SFRs that this simulator models are the CPU's own, and the GPT1 timers'
// three control registers further down.  Every other peripheral register reads
// back what was written to it and does nothing else, which is enough to run
// code that configures peripherals it then never waits on.
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
// The internal dual-port RAM, which is the only memory a register bank can be
// in and the only memory an IDX pointer reaches.  Execute.cpp says the same of
// the second; both are the XC164CM's.
static constexpr uint16_t DPRamStart = 0xF600, DPRamEnd = 0xFDFF;

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

// The GPT1 block: the core timer T3 and the two auxiliary timers T2 and T4,
// with their control and interrupt control registers.  Every address here is
// from the C167CR Derivatives User's Manual V3.1 section 10.1 and its register
// summary, and every one of the nine is in SFRCommon in
// C166RegisterInfo.td - the set the three maps this tree models agree about -
// so this block is the same block at the same addresses on every part in the
// table, and none of it is gated on which one is selected.  That was worth
// checking: modelling VECSEG unconditionally, at an address a C167 uses for
// SYSCON, is exactly the mistake this would otherwise repeat.
//
// The counters and the interrupt control registers stay storage.  A program
// reads and writes them at their own addresses, the simulator does the same,
// and nothing is gained by intercepting either.  The three control registers
// are registers, because T3OTL is a bit in one of them that this simulator
// writes rather than the program.
static constexpr uint32_t SFR_T2 = 0xFE40, SFR_T3 = 0xFE42, SFR_T4 = 0xFE44;
static constexpr uint32_t SFR_T2CON = 0xFF40, SFR_T3CON = 0xFF42;
static constexpr uint32_t SFR_T4CON = 0xFF44;
static constexpr uint32_t SFR_T2IC = 0xFF60, SFR_T3IC = 0xFF62;
static constexpr uint32_t SFR_T4IC = 0xFF64;
static constexpr uint16_t PLLCON_VCO_LOCKS = 1u << 13;
static constexpr uint16_t SYSSTAT_PLLLOCK = 1u << 14;

//===----------------------------------------------------------------------===//
// GPT1
//===----------------------------------------------------------------------===//
//
// Everything below is from the C167CR Derivatives User's Manual V3.1 chapter
// 10, "The General Purpose Timer Units".  Two of its statements are the whole
// of the arithmetic:
//
//   "In this mode, T3 is clocked with the internal system clock (CPU clock)
//    divided by a programmable prescaler, which is selected by bit field T3I.
//    ... fT3 = fCPU / (8 x 2^<T3I>)"                            (section 10.1.1)
//
//   "Upon a trigger signal T3 is loaded with the contents of the respective
//    timer register (T2 or T4) and the interrupt request flag (T2IR or T4IR)
//    is set.  Note: When a T3OTL transition is selected for the trigger
//    signal, also the interrupt request flag T3IR will be set upon a trigger"
//                                                               (section 10.1.2)
//
// A state here is one CPU clock period, which is what States counts, so the
// first of those is a period of 8 << TxI states and needs no conversion.
//
// What is modelled is timer mode, and reload mode on the two auxiliary timers.
// Everything else in the block - counter, gated, capture and incremental
// interface mode, and the external up/down control - is driven by a pin, and
// this simulator has no pins.  A configuration that needs one stops the program
// by name rather than running it and never counting, because never counting is
// exactly the failure nothing else would catch.

/// TxCON, from the register figures in sections 10.1.1 and 10.1.2.  The core
/// timer has two bits the auxiliary ones do not, at the top.
enum TCONBit {
  TCON_I = 0,      ///< bits 2-0, the prescaler in timer mode
  TCON_M = 3,      ///< bits 5-3, the basic operating mode
  TCON_R = 6,      ///< the run bit
  TCON_UD = 7,     ///< count down when set
  TCON_UDE = 8,    ///< take the direction from the TxEUD pin instead
  TCON_OE = 9,     ///< T3 only: put T3OTL on the T3OUT pin
  TCON_OTL = 10,   ///< T3 only: toggles on every overflow and underflow
};

/// The basic operating modes TxM selects.
enum TimerMode {
  ModeTimer = 0,
  ModeCounter = 1,
  ModeGatedLow = 2,
  ModeGatedHigh = 3,
  ModeReload = 4,  ///< auxiliary timers only; reserved on T3
  ModeCapture = 5, ///< auxiliary timers only; reserved on T3
  ModeIncremental = 6,
};

/// xxIC, from section 5 of the same manual: "xxIR xxIE ILVL GLVL", with the
/// request flag at the top of the low byte.
enum ICBit {
  IC_ILVL = 0, ///< bits 3-0
  IC_GLVL = 4, ///< bits 5-4
  IC_IE = 6,
  IC_IR = 7,
};

/// The parts of TxCON that decide what the timer does, which is what has to
/// change before the prescaler starts again.
static uint16_t timerConfig(uint16_t Con) {
  return Con & ((7u << TCON_I) | (7u << TCON_M) | (1u << TCON_R) |
                (1u << TCON_UD) | (1u << TCON_UDE));
}

static unsigned timerMode(uint16_t Con) { return (Con >> TCON_M) & 7; }
static bool timerRunning(uint16_t Con) { return (Con >> TCON_R) & 1; }

/// States per count in timer mode: fCPU / (8 x 2^TxI) is a tick every
/// 8 x 2^TxI clocks, and a clock is a state.
static uint64_t timerPeriod(uint16_t Con) {
  return uint64_t(8) << ((Con >> TCON_I) & 7);
}

/// The addresses of one timer's three registers, and the vector its overflow
/// reaches.  The vector numbers are the table's own - 22H, 23H and 24H - which
/// is what startup/c167-vectors.inc calls VECTOR_T2IC, T3IC and T4IC.
struct TimerRegs {
  uint32_t Count, IC;
  unsigned Vector;
  const char *Name;
};
static constexpr TimerRegs GPT1[3] = {
    {SFR_T2, SFR_T2IC, 34, "T2"},
    {SFR_T3, SFR_T3IC, 35, "T3"},
    {SFR_T4, SFR_T4IC, 36, "T4"},
};

/// Raise a source's request flag, which is the peripheral half of an interrupt:
/// the enable and the priority beside it in the same register are the CPU's
/// business and are read during arbitration.
void Machine::raiseRequest(uint32_t IC) {
  write16(IC, read16(IC) | (uint16_t(1) << IC_IR));
}

void Machine::setTimerControl(unsigned N, uint16_t V) {
  uint16_t Old = TCON[N];
  TCON[N] = V;

  // Only the core timer has an output toggle latch and an output enable; on an
  // auxiliary timer those two bits are not implemented, so they read as zero
  // whatever was written.
  if (N != 1)
    TCON[N] &= ~((1u << TCON_OE) | (1u << TCON_OTL));

  // A configuration that would do something on the part and nothing here is
  // refused, at the write rather than at some later instruction, so that what
  // is reported is the line that asked for it.
  unsigned Mode = timerMode(TCON[N]);
  if (N == 1 && (Mode == ModeReload || Mode == ModeCapture)) {
    // Not a gap here but a mistake there: the manual marks both of these
    // reserved on the core timer, so the part would not do it either.
    Stop = StopReason::Unsupported;
    StopDetail =
        (Twine("T3CON asks for a mode the manual reserves on the core timer"))
            .str();
    return;
  }

  const char *Missing = nullptr;
  if ((TCON[N] >> TCON_UDE) & 1)
    Missing = "external up/down control";
  else if (Mode == ModeReload || Mode == ModeCapture) {
    // Table 10-8: the trigger is TxIN unless bit 2 of TxI selects T3OTL, and
    // TxI = 000 selects nothing at all, which is idle rather than unmodelled.
    unsigned Sel = (TCON[N] >> TCON_I) & 7;
    if (Mode == ModeCapture)
      Missing = "capture mode";
    else if (Sel != 0 && !(Sel & 4))
      Missing = "a reload triggered by the TxIN pin";
  } else if (Mode != ModeTimer && timerRunning(TCON[N])) {
    Missing = Mode == ModeCounter    ? "counter mode"
              : Mode == ModeIncremental ? "incremental interface mode"
                                        : "gated timer mode";
  }
  if (Missing) {
    Stop = StopReason::Unsupported;
    StopDetail = (Twine(GPT1[N].Name) + " was configured for " + Missing +
                  ", which is driven by a pin this simulator does not have")
                     .str();
    return;
  }

  // The prescaler starts again when what the timer is doing changes, which is
  // a choice: the manual gives the rate and says nothing about the phase, and
  // a tick that landed in the middle of the write would be as defensible.  The
  // rate is what a program can observe, and it is the same either way.
  if (timerConfig(Old) != timerConfig(TCON[N]))
    Timers[N].NextTick = States + timerPeriod(TCON[N]);
  Timers[N].LastConfig = timerConfig(TCON[N]);

  TimersOn = false;
  for (unsigned I = 0; I != 3; ++I) {
    unsigned M = timerMode(TCON[I]);
    if ((M == ModeTimer && timerRunning(TCON[I])) ||
        (I != 1 && M == ModeReload && (((TCON[I] >> TCON_I) & 7) & 4)))
      TimersOn = true;
  }
}

/// One overflow or underflow of the core timer: its own request, the toggle
/// latch, and whatever an auxiliary timer in reload mode makes of the latch.
void Machine::coreTimerWrapped() {
  raiseRequest(SFR_T3IC);

  bool Was = (TCON[1] >> TCON_OTL) & 1;
  bool Now = !Was;
  TCON[1] = (TCON[1] & ~(uint16_t(1) << TCON_OTL)) | (uint16_t(Now) << TCON_OTL);

  // Table 10-8 again: 101 is a rising edge of T3OTL, 110 a falling one and 111
  // either.  T2 is looked at before T4, so where both are set to reload - which
  // the manual does not forbid and no program should do - T4's value is the one
  // T3 keeps.
  for (unsigned I : {0u, 2u}) {
    if (timerMode(TCON[I]) != ModeReload)
      continue;
    unsigned Sel = (TCON[I] >> TCON_I) & 7;
    if (!(Sel & 4))
      continue;
    bool Wanted = (Sel & 3) == 1 ? Now : (Sel & 3) == 2 ? !Now : true;
    if (!Wanted)
      continue;
    write16(GPT1[1].Count, read16(GPT1[I].Count));
    raiseRequest(GPT1[I].IC);
  }
}

void Machine::advanceTimers() {
  for (unsigned N = 0; N != 3; ++N) {
    uint16_t Con = TCON[N];
    if (timerMode(Con) != ModeTimer || !timerRunning(Con))
      continue;
    uint64_t Period = timerPeriod(Con);
    bool Down = (Con >> TCON_UD) & 1;
    while (States >= Timers[N].NextTick) {
      Timers[N].NextTick += Period;
      uint16_t V = read16(GPT1[N].Count);
      uint16_t Next = Down ? uint16_t(V - 1) : uint16_t(V + 1);
      write16(GPT1[N].Count, Next);
      // Counting up, the wrap is FFFFH to 0000H; counting down it is 0000H to
      // FFFFH.  Either is what the manual calls an overflow or an underflow,
      // and either raises the request.
      if (Down ? V == 0 : Next == 0) {
        if (N == 1)
          coreTimerWrapped();
        else
          raiseRequest(GPT1[N].IC);
      }
      if (Stop != StopReason::Running)
        return;
    }
  }
}

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
  case SFR_T2CON:
  case SFR_T3CON:
  case SFR_T4CON:
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
    case SFR_T2CON:
      return TCON[0];
    case SFR_T3CON:
      return TCON[1];
    case SFR_T4CON:
      return TCON[2];
    }
  }
  return uint16_t(Mem[Phys]) | (uint16_t(Mem[(Phys + 1) & AddressMask]) << 8);
}

void Machine::write16(uint32_t Phys, uint16_t V) {
  Phys &= AddressMask;
  if (isCPUSFR(Phys)) {
    switch (Phys) {
    case SFR_DPP0:
      ThisWrite = {PipelineWrite::DataPage, 0};
      DPP[0] = V & 0x3FF;
      return;
    case SFR_DPP1:
      ThisWrite = {PipelineWrite::DataPage, 1};
      DPP[1] = V & 0x3FF;
      return;
    case SFR_DPP2:
      ThisWrite = {PipelineWrite::DataPage, 2};
      DPP[2] = V & 0x3FF;
      return;
    case SFR_DPP3:
      ThisWrite = {PipelineWrite::DataPage, 3};
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
      // R0 to R15 are a window into the internal RAM at the address this
      // holds, so a context pointer that names anything else does not name
      // registers at all.  On the part the window would read whatever that
      // memory is; here it would read the simulator's own array and quietly
      // work, which is worse - a register bank placed outside the internal
      // RAM by a linker script that has no banks region is exactly the
      // mistake nothing else would catch.  The bounds are the XC164CM's
      // dual-port RAM, which is what this simulator models throughout.
      if (V < DPRamStart || uint32_t(V) + 32 > uint32_t(DPRamEnd) + 1) {
        Stop = StopReason::BadAccess;
        StopDetail = "a context pointer outside the internal RAM, where the "
                     "register bank has to be - see c166_bank";
        return;
      }
      ThisWrite = {PipelineWrite::ContextPointer, 0};
      CP = V;
      return;
    case SFR_SP:
      // An explicit write, which is the one the manual's rule is about;
      // PUSH, CALL and SCXT reach SP through push() and "are solved
      // internally by the CPU logic".
      ThisWrite = {PipelineWrite::StackPointer, 0};
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
    case SFR_T2CON:
      setTimerControl(0, V);
      return;
    case SFR_T3CON:
      setTimerControl(1, V);
      return;
    case SFR_T4CON:
      setTimerControl(2, V);
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
  // address bits 23..14.  Which one is what the write before this is checked
  // against; an EXTP or EXTS access returned above and used none of them.
  unsigned N = (Addr >> 14) & 3;
  UsedDPPMask |= 1u << N;
  return (uint32_t(DPP[N] & 0x3FF) << 14) | (Addr & 0x3FFF);
}

uint16_t Machine::getWordReg(unsigned N) const {
  uint32_t A = gprAddress(2 * N);
  return uint16_t(Mem[A]) | (uint16_t(Mem[A + 1]) << 8);
}

void Machine::setWordReg(unsigned N, uint16_t V) {
  uint32_t A = gprAddress(2 * N);
  Mem[A] = V & 0xFF;
  Mem[A + 1] = V >> 8;
}

uint8_t Machine::getByteReg(unsigned N) const {
  // RL0, RH0, RL1, RH1 ... are consecutive bytes of the same window.
  return Mem[gprAddress(N)];
}

void Machine::setByteReg(unsigned N, uint8_t V) { Mem[gprAddress(N)] = V; }

uint32_t Machine::regFieldAddress(unsigned Reg) const {
  Reg &= 0xFF;
  if (Reg >= 0xF0)
    return gprAddress(2 * (Reg - 0xF0));
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
