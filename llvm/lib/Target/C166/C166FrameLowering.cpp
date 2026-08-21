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
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/TargetInstrInfo.h"
#include "llvm/IR/Function.h"
#include "llvm/MC/MCDwarf.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Target/TargetOptions.h"

using namespace llvm;

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

void C166FrameLowering::emitPrologue(MachineFunction &MF,
                                     MachineBasicBlock &MBB) const {
  MachineFrameInfo &MFI = MF.getFrameInfo();
  const auto &STI = MF.getSubtarget<C166Subtarget>();
  const C166InstrInfo &TII = *STI.getInstrInfo();

  MachineBasicBlock::iterator MBBI = MBB.begin();
  DebugLoc DL;

  uint64_t StackSize = MFI.getStackSize();

  // Save the multiply/divide unit before anything else can disturb it.  MDC
  // goes first because reading MDL - which is what pushing it does - clears
  // MDC.MDRIU, the bit that says the unit is in use.
  if (needsMulDivSave(MF))
    for (MCRegister Reg : {C166::MDC, C166::MDL, C166::MDH})
      BuildMI(MBB, MBBI, DL, TII.get(C166::PUSH))
          .addReg(Reg)
          .setMIFlag(MachineInstr::FrameSetup);

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

  if (!hasFP(MF))
    return;

  // The callee saved registers - the incoming frame pointer among them - are
  // spilled by the prolog/epilog inserter right after the stack adjustment we
  // just emitted.  Their slots are addressed through R0, so R1 may only be
  // redefined once they are all safely stored.
  std::advance(MBBI, MFI.getCalleeSavedInfo().size());

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

  // Undo the saves of the multiply/divide unit last, so that nothing between
  // here and the RETI can touch it again.  Popping in the mirror order also
  // puts MDC back after MDL and MDH, whose restoration would otherwise leave
  // MDC.MDRIU set whether or not it was.
  if (needsMulDivSave(MF))
    for (MCRegister Reg : {C166::MDH, C166::MDL, C166::MDC})
      BuildMI(MBB, MBBI, DL, TII.get(C166::POP), Reg)
          .setMIFlag(MachineInstr::FrameDestroy);
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
