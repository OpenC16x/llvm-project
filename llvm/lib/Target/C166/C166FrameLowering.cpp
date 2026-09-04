//===-- C166FrameLowering.cpp - C166 Frame Information --------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains the C166 implementation of TargetFrameLowering.
//
//===----------------------------------------------------------------------===//

#include "C166FrameLowering.h"
#include "C166.h"
#include "C166InstrInfo.h"
#include "C166Subtarget.h"
#include "MCTargetDesc/C166MCTargetDesc.h"
#include "MCTargetDesc/C166InstPrinter.h"
#include "MCTargetDesc/C166UnwindRules.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/TargetInstrInfo.h"
#include "llvm/IR/DiagnosticInfo.h"
#include "llvm/IR/Function.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCDwarf.h"
#include "llvm/MC/MCRegisterInfo.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Target/TargetOptions.h"

using namespace llvm;

/// How large a frame has to be before -mstack-check puts the comparison in
/// front of it.  Eight bytes of code against a frame of this many bytes or
/// more.
///
/// Six, and the six is arithmetic rather than taste.  This part has two
/// stacks and the hardware watches the other one: every call pushes two bytes
/// of return address onto the system stack, which the stock linker scripts
/// give 512 bytes, so a runaway recursion trips STKOV on its 256th call
/// whatever its frames look like.  The ABI stack in the same scripts is the
/// 1024 bytes between F600H and FA00H, so a recursion whose frame is F bytes
/// exhausts it on call 1024/F.  The ABI stack goes first exactly when
/// 1024/F < 256, which is F > 4.  A frame of four bytes or less is the
/// hardware's to catch; six is the first one that is not, and checking from
/// there up is what makes the two checks between them cover every straight
/// recursion under those scripts.  A board with a different map wants a
/// different number, which is why this is an option.
///
/// The cost of being complete rather than cheap is small.  Over the four
/// programs in llvm/utils/C166Sim/corpus built for this part at -O2, against
/// 44050 bytes of text and 17,089,775 states:
///
///   threshold  2   text +1.02%   states +0.115%
///   threshold  6   text +0.94%   states +0.113%
///   threshold  8   text +0.91%   states +0.113%
///   threshold 16   text +0.74%   states +0.096%
///   threshold 32   text +0.27%   states +0.019%
///   threshold 64   text +0.15%   states +0.000%
///
/// The distribution behind that: 61% of the functions in those programs
/// allocate anything at all, 53% allocate eight bytes or more, 43% sixteen or
/// more, 16% thirty two or more, and 6% sixty four or more, with one 402 byte
/// frame at the end of a long tail.  Raising the threshold to 32 buys three
/// quarters of the code size back and gives up the completeness above, which
/// is a trade a program that has measured its own stack can make and a
/// default should not.
///
/// The states column is small at every setting because the functions with the
/// large frames are not the ones in the inner loops.  It is not zero: the
/// check is a CMP and a JMPA, four states, in every prologue that takes it.
static cl::opt<unsigned> StackCheckThreshold(
    "c166-stack-check-threshold", cl::Hidden, cl::init(6),
    cl::desc("Smallest frame -mstack-check checks, in bytes"));

bool C166FrameLowering::hasFPImpl(const MachineFunction &MF) const {
  const MachineFrameInfo &MFI = MF.getFrameInfo();
  return MF.getTarget().Options.DisableFramePointerElim(MF) ||
         MFI.hasVarSizedObjects() || MFI.isFrameAddressTaken() ||
         MF.getSubtarget().getRegisterInfo()->hasStackRealignment(MF);
}

bool C166FrameLowering::hasReservedCallFrame(const MachineFunction &MF) const {
  // The call frame can only be folded into the function's frame if the stack
  // pointer does not move around at run time.
  return !MF.getFrameInfo().hasVarSizedObjects();
}

void C166FrameLowering::determineCalleeSaves(MachineFunction &MF,
                                             BitVector &SavedRegs,
                                             RegScavenger *RS) const {
  TargetFrameLowering::determineCalleeSaves(MF, SavedRegs, RS);

  // The frame pointer is saved and restored by the generic callee saved
  // register machinery; the prologue only has to set it up.
  if (hasFP(MF))
    SavedRegs.set(C166::R1);
}

/// Emit an "R0 += Amount" sequence.  A negative amount allocates stack space.
static void adjustStackPointer(MachineBasicBlock &MBB,
                               MachineBasicBlock::iterator MBBI,
                               const DebugLoc &DL, const C166InstrInfo &TII,
                               int64_t Amount, MachineInstr::MIFlag Flag) {
  if (Amount == 0)
    return;

  int64_t Size = Amount < 0 ? -Amount : Amount;

  // A frame of seven bytes or less fits the two byte #data3 form.
  unsigned Opc;
  if (Size < 8)
    Opc = Amount < 0 ? C166::SUB16ri3 : C166::ADD16ri3;
  else
    Opc = Amount < 0 ? C166::SUB16ri : C166::ADD16ri;

  BuildMI(MBB, MBBI, DL, TII.get(Opc), C166::R0)
      .addReg(C166::R0)
      .addImm(Size)
      .setMIFlag(Flag);
}

/// The C166 interrupts MUL and DIV part way through rather than making them
/// atomic, so MDL, MDH and MDC can hold live state belonging to whatever the
/// handler interrupted.  A handler that touches the multiply/divide unit at
/// all - directly, or through a call to something that might - has to put it
/// back.  PSW.MULIP, the other half of that state, rides along on the PSW the
/// hardware stacks on entry and RETI restores.
static bool needsMulDivSave(const MachineFunction &MF) {
  if (!MF.getFunction().hasFnAttribute("interrupt"))
    return false;

  const MachineRegisterInfo &MRI = MF.getRegInfo();
  return MRI.isPhysRegModified(C166::MDL) || MRI.isPhysRegModified(C166::MDH) ||
         MRI.isPhysRegModified(C166::MDC);
}

/// The MAC unit's accumulator is forty bits wide: the low byte of MSW is its
/// extension, sitting above MAH and MAL.  None of that is on the record the
/// hardware builds on entry to a handler, and the C166S V2 manual says what
/// follows from it in as many words - "all dedicated MAC registers must be
/// saved on the stack if the MAC unit is shared between different tasks and
/// interrupts".  So a handler that touches the coprocessor has to put it back
/// itself, exactly as it does the multiply/divide unit above.
///
/// The three accumulator registers go together whether or not each was written
/// on its own, because putting a word into MAH zeroes MAL and sign extends the
/// extension byte: restoring one of them restores all three, so all three have
/// to have been saved.  That is also why MAH is pushed last and so popped
/// first, with MAL and MSW landing on top of what its restoration cleared.
static bool writesRegisterOutright(const MachineFunction &MF, MCRegister Reg,
                                   const TargetRegisterInfo &TRI) {
  for (const MachineBasicBlock &MBB : MF)
    for (const MachineInstr &MI : MBB)
      for (const MachineOperand &MO : MI.operands())
        if (MO.isReg() && MO.isDef() && MO.getReg() &&
            TRI.regsOverlap(MO.getReg(), Reg))
          return true;
  return false;
}

static void getMACSaveList(const MachineFunction &MF,
                           SmallVectorImpl<MCRegister> &Regs) {
  if (!MF.getFunction().hasFnAttribute("interrupt"))
    return;
  if (!MF.getSubtarget<C166Subtarget>().hasMAC())
    return;

  const MachineRegisterInfo &MRI = MF.getRegInfo();
  const TargetRegisterInfo &TRI = *MF.getSubtarget().getRegisterInfo();

  // MCW and MRW are the unit's configuration rather than its working state.
  // Nothing generated here writes either, and a hand written routine that
  // changes the unit's mode has to put it back for its own caller whether an
  // interrupt is involved or not.  So these are saved where this handler
  // really does write them and not merely because it makes a call, which is
  // what asking about the registers themselves rather than what a call's
  // register mask covers comes to.
  for (MCRegister Reg : {C166::MRW, C166::MCW})
    if (writesRegisterOutright(MF, Reg, TRI))
      Regs.push_back(Reg);

  // The accumulator is working state, and there a call is enough on its own:
  // the callee is free to use the unit, and on a part that has one that is
  // what it does whenever it multiplies.
  if (MRI.isPhysRegModified(C166::MAL) || MRI.isPhysRegModified(C166::MAH) ||
      MRI.isPhysRegModified(C166::MSW))
    Regs.append({C166::MSW, C166::MAL, C166::MAH});

  // And the four offset registers, on the same terms MRW and MCW are on: they
  // are the unit's configuration, so a handler saves them where it writes them
  // rather than merely because it makes a call.  A repeated coprocessor
  // instruction over a circular buffer is what writes them - see
  // C166MACRepeat.cpp - and until they were pushable a handler that did that
  // left the interrupted code's buffer walking a different stride.
  //
  // They are in the extended register space, so pushing one is EXTR and then
  // PUSH, which is what EPUSH is.  A part with the unit and without the EXTend
  // instructions does not exist - the unit arrived with the second generation,
  // which is where EXTR arrived - but -mattr can still describe one, so it is
  // asked rather than assumed.
  for (MCRegister Reg : {C166::QX0, C166::QX1, C166::QR0, C166::QR1})
    if (writesRegisterOutright(MF, Reg, TRI))
      Regs.push_back(Reg);
}

/// Whether \p Reg is one the extended space holds, which decides whether it is
/// pushed with PUSH or with EPUSH.
static bool isExtendedSFR(MCRegister Reg) {
  return C166::ESFRAnyRegClass.contains(Reg);
}

/// How this function was entered, which is what decides the shape of the record
/// the hardware stack holds.
static C166::EntryKind getEntryKind(const MachineFunction &MF) {
  const Function &F = MF.getFunction();
  if (F.hasFnAttribute("interrupt"))
    return C166::EntryKind::Interrupt;
  if (F.hasFnAttribute("far"))
    return C166::EntryKind::Far;
  return C166::EntryKind::Near;
}

/// Say where the callee saved registers went.  These are on the ABI stack, so
/// unlike everything else above they are at an ordinary offset from the
/// canonical frame address and take an ordinary rule.  The offsets are already
/// relative to that address: the CFA here is R0 as the function was entered,
/// which is where a frame index of zero is, because the return address that
/// would otherwise sit between them is on the other stack.
void C166FrameLowering::emitCalleeSavedFrameMoves(
    MachineBasicBlock &MBB, MachineBasicBlock::iterator MBBI,
    const DebugLoc &DL) const {
  MachineFunction &MF = *MBB.getParent();
  if (!MF.needsFrameMoves())
    return;

  const MachineFrameInfo &MFI = MF.getFrameInfo();
  const auto &STI = MF.getSubtarget<C166Subtarget>();
  const C166InstrInfo &TII = *STI.getInstrInfo();
  const TargetRegisterInfo &TRI = *STI.getRegisterInfo();

  for (const CalleeSavedInfo &CS : MFI.getCalleeSavedInfo()) {
    unsigned Index = MF.addFrameInst(MCCFIInstruction::createOffset(
        nullptr, TRI.getDwarfRegNum(CS.getReg(), /*IsEH=*/false),
        MFI.getObjectOffset(CS.getFrameIdx())));
    BuildMI(MBB, MBBI, DL, TII.get(TargetOpcode::CFI_INSTRUCTION))
        .addCFIIndex(Index)
        .setMIFlag(MachineInstr::FrameSetup);
  }
}

/// Say where the return address is, and how to get back to the caller's
/// hardware stack pointer, given that \p Depth bytes have been pushed onto that
/// stack since the function was entered.
void C166FrameLowering::emitUnwindRules(MachineBasicBlock &MBB,
                                        MachineBasicBlock::iterator MBBI,
                                        const DebugLoc &DL, unsigned Depth,
                                        MachineInstr::MIFlag Flag) const {
  MachineFunction &MF = *MBB.getParent();
  if (!MF.needsFrameMoves())
    return;

  // The CIE already says what a near function at depth zero looks like, which
  // is nearly all of them, so those have nothing to add.
  C166::EntryKind Kind = getEntryKind(MF);
  if (C166::isDefaultUnwind(Kind, Depth))
    return;

  const auto &STI = MF.getSubtarget<C166Subtarget>();
  const C166InstrInfo &TII = *STI.getInstrInfo();

  SmallVector<MCCFIInstruction, 4> Rules;
  C166::getUnwindRules(Rules, *MF.getContext().getRegisterInfo(), Kind, Depth);
  for (const MCCFIInstruction &Inst : Rules)
    BuildMI(MBB, MBBI, DL, TII.get(TargetOpcode::CFI_INSTRUCTION))
        .addCFIIndex(MF.addFrameInst(Inst))
        .setMIFlag(Flag);
}

/// Whether this handler gets sixteen registers of its own rather than saving
/// the ones it uses.
///
/// SCXT CP, #bank pushes the context pointer and loads a new one, which moves
/// the whole register window somewhere nobody else is using: the interrupted
/// code's R0 to R15 are left exactly as they were, so there is nothing to
/// spill and nothing to reload.  POP CP hands them back.  That is one
/// instruction in and one out against up to fifteen of each, and it is what
/// __attribute__((c166_bank)) asks for.
static bool usesOwnRegisterBank(const MachineFunction &MF) {
  return MF.getFunction().hasFnAttribute("c166-bank");
}

/// The symbol naming this handler's bank.  The AsmPrinter reserves the
/// thirty-two bytes it stands for; the linker script decides where they land,
/// which has to be internal RAM because that is the only memory a context
/// pointer may name.
static const char *bankSymbolFor(MachineFunction &MF) {
  return MF.createExternalSymbolName("__c166_bank_" + MF.getName().str());
}

/// Whether a handler with a bank of its own still needs R0.
///
/// R0 is the ABI stack pointer and belongs to the interrupted code, not to the
/// register window; the new bank's copy of it holds whatever was there last
/// time.  A handler with no frame and no calls never looks at it, and then the
/// three instructions below are three instructions of nothing.  Anything that
/// spills, takes a local's address or calls something does look at it.
static bool bankedHandlerNeedsStackPointer(const MachineFunction &MF) {
  const MachineFrameInfo &MFI = MF.getFrameInfo();
  return MFI.hasCalls() || MFI.getStackSize() != 0 ||
         MFI.hasVarSizedObjects() || MFI.isFrameAddressTaken();
}

/// Emit the two instructions that say the frame about to be allocated fits.
///
/// The part guards one of its two stacks.  STKOV and STKUN compare SP, which
/// holds return addresses, and take a trap when it leaves the window they
/// describe; R0, which holds everything else, has nothing watching it - there
/// is no second pair of registers and no way to point the first pair at it,
/// because what STKOV compares is the hardware stack pointer and not a general
/// purpose register.  So this is code, and it is only in the prologues where
/// it earns its eight bytes.
///
/// It goes before the allocation and compares against the limit plus the frame
/// size rather than after it against the limit itself.  Both are one CMP - the
/// addition is the linker's, in the addend of the relocation - and this way a
/// checked program never puts R0 below the memory it owns at all, not even for
/// the two instructions it would take to notice.  That matters for the
/// handler, which then runs with a stack pointer that is still inside the
/// stack, and it is what lets the simulator's own check (which sees every
/// value R0 takes, not only the ones a prologue asks about) agree with this
/// one instead of firing first.
///
/// CMP leaves the flags for the JMPA and nothing else in the prologue reads
/// them, and the jump does not return, so no register is disturbed and the
/// unwind rules already emitted stay true.
void C166FrameLowering::emitStackCheck(MachineBasicBlock &MBB,
                                       MachineBasicBlock::iterator MBBI,
                                       const DebugLoc &DL,
                                       const C166InstrInfo &TII,
                                       uint64_t StackSize) const {
  const MachineFunction &MF = *MBB.getParent();
  if (!MF.getSubtarget<C166Subtarget>().hasStackCheck())
    return;

  // A function that allocates nothing cannot be the one that runs out, and the
  // handler is such a function - which is what stops the check from finding
  // itself.
  if (StackSize < std::max<uint64_t>(StackCheckThreshold, 1))
    return;

  // "__user_stack_limit + StackSize", which the linker works out: the addend
  // rides in the relocation and costs the program nothing.
  MachineOperand Limit = MachineOperand::CreateES("__user_stack_limit");
  Limit.setOffset(StackSize);
  BuildMI(MBB, MBBI, DL, TII.get(C166::CMP16ri))
      .addReg(C166::R0)
      .add(Limit)
      .setMIFlag(MachineInstr::FrameSetup);
  BuildMI(MBB, MBBI, DL, TII.get(C166::JMPAcc))
      .addExternalSymbol("__c166_stack_overflow")
      .addImm(C166CC::COND_ULT)
      .setMIFlag(MachineInstr::FrameSetup);
}

void C166FrameLowering::emitPrologue(MachineFunction &MF,
                                     MachineBasicBlock &MBB) const {
  MachineFrameInfo &MFI = MF.getFrameInfo();
  const auto &STI = MF.getSubtarget<C166Subtarget>();
  const C166InstrInfo &TII = *STI.getInstrInfo();

  MachineBasicBlock::iterator MBBI = MBB.begin();
  DebugLoc DL;

  uint64_t StackSize = MFI.getStackSize();

  // Where the return address is, before anything moves the hardware stack.
  emitUnwindRules(MBB, MBBI, DL, /*Depth=*/0, MachineInstr::FrameSetup);

  unsigned SavedWords = 0;

  // The register bank first, because everything after it is written in the new
  // window rather than the old one.
  if (usesOwnRegisterBank(MF)) {
    BuildMI(MBB, MBBI, DL, TII.get(C166::SCXTregi))
        .addReg(C166::CP)
        .addExternalSymbol(bankSymbolFor(MF))
        .setMIFlag(MachineInstr::FrameSetup);
    ++SavedWords;

    // The instruction after one that writes CP does not see the new register
    // window.  "An instruction, which calculates a physical GPR operand
    // address via the CP register, is mostly not capable of using a new CP
    // value, which is to be updated by an immediately preceding instruction.
    // Thus, to make sure that the new CP value is used, at least one
    // instruction must be inserted between a CP-changing and a subsequent
    // GPR-using instruction" - C167CR Derivatives User's Manual V3.1, section
    // 4.2, Context Pointer Updating, whose example is this very instruction:
    //
    //   In    : SCXT CP,#0FC00h  ;select a new context
    //   In + 1: ...              ;must not be an instruction using a GPR
    //   In + 2: MOV R0,#dataX    ;write to GPR 0 in the new context
    //
    // Without it this handler's first register write would land in the
    // interrupted code's bank.  That used to be unfindable here - the
    // simulator applied the CP write at once - and is not any more: taking
    // this NOP out stops llvm/utils/C166Sim/differential/interrupts.c with the
    // sentence above, which is what llvm/test/tools/c166-sim/pipeline.s
    // checks.
    BuildMI(MBB, MBBI, DL, TII.get(C166::NOP))
        .setMIFlag(MachineInstr::FrameSetup);

    // R0 is the ABI stack pointer, which belongs to the interrupted code and
    // not to the register window - so the new bank's copy of it is whatever
    // was left there.  Bring the real one across: SCXT has just pushed the old
    // context pointer, so SP names the word holding it, and R0 is the first
    // word of the bank that names.  R1 is scratch here because nothing in this
    // bank is live yet; the frame pointer, if there is one, is set up further
    // down from the R0 this leaves.
    if (bankedHandlerNeedsStackPointer(MF)) {
      const MCRegisterInfo &MRI = *MF.getContext().getRegisterInfo();
      BuildMI(MBB, MBBI, DL, TII.get(C166::MOV16ra), C166::R1)
          .addImm(C166::getSFRAddressForReg(MRI, C166::SYSSP))
          .setMIFlag(MachineInstr::FrameSetup);
      BuildMI(MBB, MBBI, DL, TII.get(C166::MOV16rp), C166::R1)
          .addReg(C166::R1)
          .setMIFlag(MachineInstr::FrameSetup);
      BuildMI(MBB, MBBI, DL, TII.get(C166::MOV16rp), C166::R0)
          .addReg(C166::R1)
          .setMIFlag(MachineInstr::FrameSetup);
    }
  }

  // Save the multiply/divide unit before anything else can disturb it.  MDC
  // goes first because reading MDL - which is what pushing it does - clears
  // MDC.MDRIU, the bit that says the unit is in use.
  if (needsMulDivSave(MF)) {
    for (MCRegister Reg : {C166::MDC, C166::MDL, C166::MDH})
      BuildMI(MBB, MBBI, DL, TII.get(C166::PUSH))
          .addReg(Reg)
          .setMIFlag(MachineInstr::FrameSetup);
    SavedWords += 3;
  }

  // Then the coprocessor, if this handler disturbs it.
  SmallVector<MCRegister, 9> MACSaves;
  getMACSaveList(MF, MACSaves);
  for (MCRegister Reg : MACSaves) {
    if (isExtendedSFR(Reg) && !STI.hasExtInstr()) {
      const Function &F = MF.getFunction();
      F.getContext().diagnose(DiagnosticInfoUnsupported(
          F,
          "an interrupt handler writes " +
              Twine(C166InstPrinter::getRegisterName(Reg)) +
              ", which is in the extended register space and can only be saved "
              "with EXTR; select a processor that has the EXTend instructions",
          DL));
      continue;
    }
    BuildMI(MBB, MBBI, DL,
            TII.get(isExtendedSFR(Reg) ? C166::EPUSH : C166::PUSH))
        .addReg(Reg)
        .setMIFlag(MachineInstr::FrameSetup);
  }
  SavedWords += MACSaves.size();

  // All of those went on the hardware stack, so everything the rules above
  // measured from SP has moved down by two bytes apiece.
  if (SavedWords)
    emitUnwindRules(MBB, MBBI, DL, /*Depth=*/2 * SavedWords,
                    MachineInstr::FrameSetup);

  emitStackCheck(MBB, MBBI, DL, TII, StackSize);

  // Allocate the frame before the callee saved registers are spilled: their
  // slots are addressed relative to the already adjusted stack pointer.
  adjustStackPointer(MBB, MBBI, DL, TII, -static_cast<int64_t>(StackSize),
                     MachineInstr::FrameSetup);

  if (StackSize) {
    unsigned CFIIndex = MF.addFrameInst(
        MCCFIInstruction::cfiDefCfaOffset(nullptr, StackSize));
    BuildMI(MBB, MBBI, DL, TII.get(TargetOpcode::CFI_INSTRUCTION))
        .addCFIIndex(CFIIndex)
        .setMIFlag(MachineInstr::FrameSetup);
  }

  // The callee saved registers - the incoming frame pointer among them - are
  // spilled by the prolog/epilog inserter right after the stack adjustment we
  // just emitted.  Step over them, both to say where they went and because R1
  // may only be redefined once they are all safely stored.
  std::advance(MBBI, MFI.getCalleeSavedInfo().size());
  emitCalleeSavedFrameMoves(MBB, MBBI, DL);

  if (!hasFP(MF))
    return;

  BuildMI(MBB, MBBI, DL, TII.get(C166::MOV16rr), C166::R1)
      .addReg(C166::R0)
      .setMIFlag(MachineInstr::FrameSetup);

  unsigned CFIIndex = MF.addFrameInst(MCCFIInstruction::createDefCfaRegister(
      nullptr, STI.getRegisterInfo()->getDwarfRegNum(C166::R1, true)));
  BuildMI(MBB, MBBI, DL, TII.get(TargetOpcode::CFI_INSTRUCTION))
      .addCFIIndex(CFIIndex)
      .setMIFlag(MachineInstr::FrameSetup);
}

void C166FrameLowering::emitEpilogue(MachineFunction &MF,
                                     MachineBasicBlock &MBB) const {
  MachineFrameInfo &MFI = MF.getFrameInfo();
  const auto &STI = MF.getSubtarget<C166Subtarget>();
  const C166InstrInfo &TII = *STI.getInstrInfo();

  MachineBasicBlock::iterator MBBI = MBB.getFirstTerminator();
  DebugLoc DL = MBBI != MBB.end() ? MBBI->getDebugLoc() : DebugLoc();

  uint64_t StackSize = MFI.getStackSize();

  // Walk back over the callee saved register reloads: the stack pointer has to
  // be valid while they run.
  MachineBasicBlock::iterator FirstReload = MBBI;
  for (size_t I = 0, E = MFI.getCalleeSavedInfo().size();
       I != E && FirstReload != MBB.begin(); ++I)
    --FirstReload;

  // If the stack pointer moved at run time, put it back where the frame
  // pointer says it should be before reloading anything.
  if (hasFP(MF))
    BuildMI(MBB, FirstReload, DL, TII.get(C166::MOV16rr), C166::R0)
        .addReg(C166::R1)
        .setMIFlag(MachineInstr::FrameDestroy);

  adjustStackPointer(MBB, MBBI, DL, TII, StackSize,
                     MachineInstr::FrameDestroy);

  // The canonical frame address is the stack pointer again.
  if (StackSize || hasFP(MF)) {
    unsigned CFIIndex = MF.addFrameInst(MCCFIInstruction::cfiDefCfa(
        nullptr, STI.getRegisterInfo()->getDwarfRegNum(C166::R0, true), 0));
    BuildMI(MBB, MBBI, DL, TII.get(TargetOpcode::CFI_INSTRUCTION))
        .addCFIIndex(CFIIndex)
        .setMIFlag(MachineInstr::FrameDestroy);
  }

  // Undo the saves of the coprocessor and the multiply/divide unit last, so
  // that nothing between here and the RETI can touch either again.  Popping in
  // the mirror order puts MDC back after MDL and MDH, whose restoration would
  // otherwise leave MDC.MDRIU set whether or not it was, and puts MAH back
  // before MAL and MSW, which is the order the accumulator needs.
  SmallVector<MCRegister, 9> MACSaves;
  getMACSaveList(MF, MACSaves);
  for (MCRegister Reg : reverse(MACSaves)) {
    if (isExtendedSFR(Reg) && !STI.hasExtInstr())
      continue; // The prologue has already said why.
    BuildMI(MBB, MBBI, DL,
            TII.get(isExtendedSFR(Reg) ? C166::EPOP : C166::POP), Reg)
        .setMIFlag(MachineInstr::FrameDestroy);
  }

  if (needsMulDivSave(MF))
    for (MCRegister Reg : {C166::MDH, C166::MDL, C166::MDC})
      BuildMI(MBB, MBBI, DL, TII.get(C166::POP), Reg)
          .setMIFlag(MachineInstr::FrameDestroy);

  // The register window last, so that everything above it ran in this
  // handler's own bank.  This is the whole of putting the interrupted code's
  // registers back: they were never touched.
  if (usesOwnRegisterBank(MF))
    BuildMI(MBB, MBBI, DL, TII.get(C166::POP), C166::CP)
        .setMIFlag(MachineInstr::FrameDestroy);

  // And back up again, so that the rules are right for the RETI as well.
  if (!MACSaves.empty() || needsMulDivSave(MF) || usesOwnRegisterBank(MF))
    emitUnwindRules(MBB, MBBI, DL, /*Depth=*/0, MachineInstr::FrameDestroy);
}

bool C166FrameLowering::spillCalleeSavedRegisters(
    MachineBasicBlock &MBB, MachineBasicBlock::iterator MI,
    ArrayRef<CalleeSavedInfo> CSI, const TargetRegisterInfo *TRI) const {
  if (CSI.empty())
    return false;

  MachineFunction &MF = *MBB.getParent();
  const MachineRegisterInfo &MRI = MF.getRegInfo();
  const TargetInstrInfo &TII = *MF.getSubtarget().getInstrInfo();

  for (const CalleeSavedInfo &CS : CSI) {
    // An interrupt handler saves the argument registers along with everything
    // else, so a callee saved register here can still be carrying an incoming
    // value that the body goes on to use.  Spilling such a register does not
    // end its life, and saying that it does leaves a kill flag that later
    // passes are entitled to believe.
    MCRegister Reg = CS.getReg();
    TII.storeRegToStackSlot(MBB, MI, Reg, /*isKill=*/!MRI.isLiveIn(Reg),
                            CS.getFrameIdx(), TRI->getMinimalPhysRegClass(Reg),
                            Register());
  }

  return true;
}

MachineBasicBlock::iterator C166FrameLowering::eliminateCallFramePseudoInstr(
    MachineFunction &MF, MachineBasicBlock &MBB,
    MachineBasicBlock::iterator I) const {
  const auto &STI = MF.getSubtarget<C166Subtarget>();
  const C166InstrInfo &TII = *STI.getInstrInfo();

  if (!hasReservedCallFrame(MF)) {
    // The call frame is set up and torn down around every call, so turn the
    // pseudos into real stack pointer adjustments.
    MachineInstr &Old = *I;
    uint64_t Amount = TII.getFrameSize(Old);
    if (Amount != 0) {
      Amount = alignTo(Amount, getStackAlign());

      if (Old.getOpcode() == TII.getCallFrameSetupOpcode())
        adjustStackPointer(MBB, I, Old.getDebugLoc(), TII,
                           -static_cast<int64_t>(Amount),
                           MachineInstr::NoFlags);
      else
        adjustStackPointer(MBB, I, Old.getDebugLoc(), TII,
                           Amount - TII.getFramePoppedByCallee(Old),
                           MachineInstr::NoFlags);
    }
  }

  return MBB.erase(I);
}
