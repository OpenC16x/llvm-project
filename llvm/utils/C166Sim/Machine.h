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

/// Name the part being simulated, before anything is decoded.
///
/// The decoder needs it: the same short address is a different special function
/// register on different derivatives, so a listing read for the wrong one names
/// the wrong registers and, where a name is gated, does not decode at all.
/// Defaults to the XC164CM, which is the part this simulator models - CPUCON1's
/// vector spacing, VECSEG, the PLL and the coprocessor are all its.
/// \p Features is what the part has on top of its core, in the compiler's
/// spelling: the coprocessor is a feature rather than a core, so an ST10 with
/// one is "+mac" here exactly as it is -mmac there.
void setSimCPU(llvm::StringRef CPU, llvm::StringRef Features);

/// Whether that part has the two registers that place the interrupt vector
/// table: VECSEG, which names the segment it lives in, and CPUCON1, whose
/// VECSC field sets the space between slots.  Only the XC16x has them.
///
/// It matters because the addresses they occupy are a different register on a
/// part that does not: FF12H is SYSCON on a C167, which its crt0.S writes on
/// the way up, and reading that back as VECSEG sent every interrupt to a
/// segment nothing was linked into.  Without them the table is four bytes a
/// slot at the bottom of segment 0, which is what the reset values of both
/// already say, so nothing else has to change.
bool simCPUHasVectorRegs();

/// The whole bus is 24 bits: 256 segments of 64 KByte.
static constexpr uint32_t AddressSpaceSize = 1u << 24;
static constexpr uint32_t AddressMask = AddressSpaceSize - 1;

/// Filling the pipeline, which the manual counts once for a whole program
/// rather than against any instruction (Instruction Set Manual, section 7.1).
static constexpr uint64_t PipelineFillStates = 6;

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

/// The CPU's own priority level, which is PSW bits 15..12.
static constexpr unsigned PSWILVLShift = 12;

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

/// An interrupt source declared from the command line.
///
/// A real source is a peripheral: it raises its request flag when something
/// happens to it, and its own control register holds the enable bit and the
/// priority.  Three of them are modelled - the GPT1 timers, in Machine.cpp -
/// and everything else in the vector table is still one of these, which fires
/// on the clock at a state count, once or over and over.  That is
/// deterministic, which is what a test needs, and it exercises everything the
/// *core* does with an interrupt without a peripheral behind it.
///
/// Consequently the source's own enable bit does not appear: an injected
/// request is by definition enabled, and its group level is zero because a
/// group level is a field of a control register it does not have.  What gates
/// it is PSW.IEN and the priority comparison, both of which are the core's.
struct InterruptSource {
  /// Never raise again, which is where a one shot goes after it has fired.
  static constexpr uint64_t Never = ~uint64_t(0);

  uint64_t At = Never; ///< the state count at which the request is raised
  uint64_t Period = 0; ///< 0 for a one shot, else raise again this often
  unsigned Vector = 0; ///< which vector table entry, 0 to 127
  unsigned Level = 0;  ///< the source's priority, 1 to 15
  bool Pending = false; ///< raised and not yet accepted
};

/// One of the three timers of the GPT1 block, as far as this models them.
///
/// The registers themselves are storage like every other peripheral register -
/// the program reads and writes T2, T3 and T4 at their own addresses - so what
/// is here is only the part the program cannot see: when the timer next counts,
/// and what it was configured as last time anything looked, which is what says
/// whether the prescaler has to start again.
struct Timer {
  uint64_t NextTick = 0;   ///< the state count at which it next counts
  uint16_t LastConfig = 0; ///< TxCON's mode, prescaler and run bit, as last set
};

/// What the instruction that has just retired did that the pipeline will not
/// have finished with by the time the next one wants it.
///
/// Section 4.2 of the C167CR Derivatives User's Manual V3.1, "Particular
/// Pipeline Effects", names four of these and three are of this shape: the
/// program has to leave a gap, and one that does not is not told so - the
/// instruction simply uses the old value.  So they are checked here rather
/// than emulated.  Emulating them would mean inventing what "mostly not
/// capable of using a new CP value" covers, and the wrong answer a program
/// gets on the part is not a wrong answer this could reproduce; refusing the
/// sequence the manual says must not be written is exactly what can be
/// checked.
///
/// The fourth effect, the one instruction of delay before a change to PSW.IEN
/// or PSW.ILVL is arbitrated on, is not of this shape - the manual says what
/// happens rather than what not to write - so that one is modelled.  See
/// IntPSW.
struct PipelineWrite {
  enum Kind {
    Nothing,
    ContextPointer, ///< CP, so the next instruction must not use a GPR
    DataPage,       ///< a DPPn, so the next must not address through that one
    StackPointer,   ///< SP, so the next must not be a RET or a POP
  };
  Kind What = Nothing;
  unsigned Which = 0; ///< which DPP, when What is DataPage
};

/// Why the machine stopped.
enum class StopReason {
  Running,
  Exited,      ///< reached the exit symbol
  Unsupported, ///< an instruction this simulator does not implement
  BadAccess,   ///< a fetch or access that cannot be satisfied
  StackFault,  ///< the system stack left the STKOV/STKUN window
  BadSequence, ///< an ATOMIC or EXTend sequence contained what it must not
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

  /// The address of a byte of the register window, and the one place the CP
  /// register is turned into an address.
  ///
  /// Everything that reaches a GPR goes through here - the accessors below,
  /// the "reg" field's own encoding, the bit-addressable form - so that the
  /// dependency section 4.2 describes is recorded in one place rather than in
  /// each of them.  The first attempt instrumented the accessors and missed
  /// the shortest path of all, which is a register named in a reg8 field.
  uint16_t gprAddress(unsigned ByteOffset) const {
    UsedGPR = true;
    return uint16_t(CP + ByteOffset);
  }

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

  /// Keep the accumulator sign correct in forty bits after an update.
  void setACC(int64_t V) {
    ACC = (V & 0xFFFFFFFFFFll);
    if (ACC & 0x8000000000ll)
      ACC -= 0x10000000000ll;
  }

  /// The priority of the task the CPU is running, which an interrupt has to
  /// beat to be accepted.  It is zero out of reset and becomes the accepted
  /// source's priority on entry, which is what keeps a handler from being
  /// interrupted by its own level or below; RETI puts back the PSW that was
  /// pushed, and with it the level.
  unsigned cpuPriority() const { return (PSW >> PSWILVLShift) & 0xF; }
  void setCPUPriority(unsigned L) {
    PSW = (PSW & 0x0FFF) | (uint16_t(L & 0xF) << PSWILVLShift);
  }

  /// Whether the pipeline effects of section 4.2 are checked and modelled.
  /// On by default; --no-pipeline-effects turns it off, which is how a program
  /// that predates the check can still be run.
  bool PipelineEffects = true;

  /// What the previous instruction wrote, and what this one is writing.
  PipelineWrite LastWrite, ThisWrite;
  /// What this instruction reached, which is what the previous one's write is
  /// checked against.  Both are mutable because the accessors that set them
  /// are const: reading a GPR does not change the machine, but it is the thing
  /// being recorded.
  mutable bool UsedGPR = false;
  mutable unsigned UsedDPPMask = 0;

  /// PSW as arbitration sees it, which is one instruction behind.
  ///
  /// "Software modifications (implicit or explicit) of the PSW are done in the
  /// execute phase of the respective instructions.  In order to maintain fast
  /// interrupt responses, however, the current interrupt prioritization round
  /// does not consider these changes, i.e. an interrupt request may be
  /// acknowledged after the instruction that disables interrupts via IEN or
  /// ILVL or after the following instructions." (section 4.2, Controlling
  /// Interrupts.)  Only IEN and ILVL are read from this; every other field of
  /// PSW is read by instructions, which see it at once.
  ///
  /// Entering a handler is not a software modification - it is the hardware
  /// doing it - so acceptance sets this to the new value rather than letting
  /// it lag, which is what stops a second request of the same level walking
  /// straight into the handler.
  uint16_t IntPSW = 0;

  bool flag(PSWBit B) const { return (PSW >> B) & 1; }
  void setFlag(PSWBit B, bool V) {
    PSW = (PSW & ~(uint16_t(1) << B)) | (uint16_t(V) << B);
    WrotePSW = true;
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

  // -- interrupts --------------------------------------------------------

  /// Raise every declared source whose time has come, then hand the CPU the
  /// arbitration winner if it is allowed to take it.  Called at the top of
  /// each step, because between instructions is where the core accepts one.
  /// Returns true if a handler was entered, in which case no instruction ran
  /// this step.
  bool serviceInterrupts();

  /// The sources declared on the command line.  Empty unless the harness put
  /// something here, and then nothing below costs anything.
  std::vector<InterruptSource> Interrupts;

  /// How many requests have been accepted, which is what a test asserts on
  /// when the program itself cannot see the difference.
  uint64_t InterruptsTaken = 0;

  /// And how many were serviced by a PEC channel instead of by a handler,
  /// which is the same thing for the same reason: a program that had its
  /// buffer filled by the controller cannot tell how it got there.
  uint64_t PECTransfers = 0;

  /// Run the GPT1 timers forward to the current state count, raising the
  /// request flags of any that overflowed on the way.  Called from
  /// serviceInterrupts, which is where a request becomes an interrupt.
  void advanceTimers();

  /// Set a peripheral's interrupt request flag, which is bit 7 of its own
  /// interrupt control register.  The enable and the priority beside it there
  /// are the CPU's business and are read during arbitration.
  void raiseRequest(uint32_t IC);

  /// Write T2CON, T3CON or T4CON - 0, 1 and 2 - and take from it whatever the
  /// timer now does.  A configuration that needs a pin stops the program here,
  /// where the write is, rather than by never counting.
  void setTimerControl(unsigned N, uint16_t V);

  /// What an overflow or underflow of the core timer does: its own request,
  /// the toggle latch, and any reload an auxiliary timer is watching for.
  void coreTimerWrapped();

  /// T2CON, T3CON and T4CON, which are registers rather than storage because
  /// this simulator writes one of them: T3OTL toggles on every overflow of T3,
  /// and an auxiliary timer in reload mode watches it.
  uint16_t TCON[3] = {0, 0, 0};
  Timer Timers[3];
  /// Whether any of the three is doing anything, so that a program with no
  /// timers - which is every program in this tree until now - pays one test
  /// per instruction and not three register reads.
  bool TimersOn = false;

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

  /// The MAC unit's 40 bit signed accumulator, and its control word.
  ///
  /// ACC is held as a 64 bit signed value kept sign correct in its low 40
  /// bits; MAL is bits 15 to 0 and MAH bits 31 to 16, which is what a program
  /// reads back with CoSTORE.  MCW resets to 0000H, so the unit starts with
  /// the product shift and the saturation both off - which is plain integer
  /// multiply-accumulate, and is why nothing has to configure it first.
  ///
  /// MSW's flags and guard bits are not modelled, and neither is the limiter
  /// or the shifter; anything using them stops the simulator by name, as any
  /// unimplemented instruction does.
  int64_t ACC = 0;
  uint16_t MCW = 0;

  /// MSW's upper byte, which is the unit's flags.  Nothing here generates
  /// them, so they are only carried: a program that writes MSW reads back what
  /// it wrote, which is what an interrupt handler saving and restoring the
  /// coprocessor needs and is all that is claimed.  The register's low byte is
  /// not here at all - it is MAE, the accumulator's top eight bits, and it
  /// comes straight off ACC.
  uint16_t MSWFlags = 0;
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

  /// Execution time in states, where one state is one CPU clock period.
  ///
  /// This is what the Instruction Set Manual's chapter 7 calls Ttot: the sum
  /// of each instruction's time plus six states for the solitary filling of
  /// the pipeline.  It counts a program executing from the internal program
  /// memory, which is where the linker script here puts .text; running from
  /// RAM or through the external bus controller costs more, and by an amount
  /// that depends on the bus mode and the programmed waitstates rather than
  /// on the program, so none of that is modelled.  See stateTime() for what
  /// is and is not counted.
  uint64_t States = PipelineFillStates;

  /// Whether the instruction just retired wrote PSW, which a conditional
  /// branch immediately after it pays a state for.
  bool PrevWrotePSW = false;
  /// Set by setFlag() while the current instruction runs.
  bool WrotePSW = false;

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
