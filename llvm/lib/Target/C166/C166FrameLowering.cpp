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

  unsigned Opc = Amount < 0 ? C166::SUB16ri : C166::ADD16ri;
  BuildMI(MBB, MBBI, DL, TII.get(Opc), C166::R0)
      .addReg(C166::R0)
      .addImm(Amount < 0 ? -Amount : Amount)
      .setMIFlag(Flag);
}

void C166FrameLowering::emitPrologue(MachineFunction &MF,
                                     MachineBasicBlock &MBB) const {
  MachineFrameInfo &MFI = MF.getFrameInfo();
  const auto &STI = MF.getSubtarget<C166Subtarget>();
  const C166InstrInfo &TII = *STI.getInstrInfo();

  MachineBasicBlock::iterator MBBI = MBB.begin();
  DebugLoc DL;

  uint64_t StackSize = MFI.getStackSize();

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
