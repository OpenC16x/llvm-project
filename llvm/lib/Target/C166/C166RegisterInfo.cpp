//===-- C166RegisterInfo.cpp - C166 Register Information ------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains the C166 implementation of the TargetRegisterInfo class.
//
//===----------------------------------------------------------------------===//

#include "C166RegisterInfo.h"
#include "C166FrameLowering.h"
#include "C166InstrInfo.h"
#include "C166Subtarget.h"
#include "llvm/ADT/BitVector.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/IR/Function.h"
#include "llvm/Target/TargetMachine.h"

using namespace llvm;

#define DEBUG_TYPE "c166-reg-info"

#define GET_REGINFO_TARGET_DESC
#include "C166GenRegisterInfo.inc"

C166RegisterInfo::C166RegisterInfo() : C166GenRegisterInfo(C166::R0) {}

const MCPhysReg *
C166RegisterInfo::getCalleeSavedRegs(const MachineFunction *MF) const {
  if (MF->getFunction().hasFnAttribute("interrupt")) {
    // With a bank of its own the handler saves nothing: the registers it uses
    // are not the ones the interrupted code was using.
    if (MF->getFunction().hasFnAttribute("c166-bank"))
      return CSR_C166_Bank_SaveList;
    return CSR_C166_Interrupt_SaveList;
  }
  return CSR_C166_SaveList;
}

const uint32_t *
C166RegisterInfo::getCallPreservedMask(const MachineFunction &MF,
                                       CallingConv::ID CC) const {
  return CSR_C166_RegMask;
}

BitVector C166RegisterInfo::getReservedRegs(const MachineFunction &MF) const {
  BitVector Reserved(getNumRegs());
  const C166FrameLowering *TFI = getFrameLowering(MF);

  // R0 is the ABI stack pointer.
  Reserved.set(C166::R0);
  Reserved.set(C166::RL0);
  Reserved.set(C166::RH0);

  // The special function registers are never allocatable.  Reserving PSW
  // covers the five condition flags, which are its sub-registers.
  for (MCPhysReg Sub : subregs_inclusive(C166::PSW))
    Reserved.set(Sub);
  Reserved.set(C166::MDL);
  Reserved.set(C166::MDH);
  Reserved.set(C166::MDC);

  // So are the coprocessor's, for the same reason: nothing allocates them, and
  // what is in them is state the compiler places by hand rather than a value
  // the allocator is free to move.
  Reserved.set(C166::MAL);
  Reserved.set(C166::MAH);
  Reserved.set(C166::MAS);
  Reserved.set(C166::MSW);
  Reserved.set(C166::MCW);
  Reserved.set(C166::MRW);
  // IDX0 and IDX1 are the same kind of thing: the unit's two pointers, placed
  // by whatever emits a form that walks them and read by that form alone.
  Reserved.set(C166::IDX0);
  Reserved.set(C166::IDX1);
  // And the four offset registers, which are the strides those pointers walk
  // by.  They are in the extended register space rather than beside the rest,
  // which changes how they are reached and not what they are.
  Reserved.set(C166::QX0);
  Reserved.set(C166::QX1);
  Reserved.set(C166::QR0);
  Reserved.set(C166::QR1);

  Reserved.set(C166::SYSSP);
  Reserved.set(C166::CP);

  // R1 is the frame pointer when the function needs one.
  if (TFI->hasFP(MF)) {
    Reserved.set(C166::R1);
    Reserved.set(C166::RL1);
    Reserved.set(C166::RH1);
  }

  return Reserved;
}

const TargetRegisterClass *
C166RegisterInfo::getPointerRegClass(unsigned Kind) const {
  return &C166::GR16RegClass;
}

/// Return true if \p FrameIndex names one of the slots that the prolog/epilog
/// inserter allocated for the callee saved registers.
static bool isCalleeSavedFrameIndex(const MachineFrameInfo &MFI,
                                    int FrameIndex) {
  return MFI.isCalleeSavedObjectIndex(FrameIndex);
}

bool C166RegisterInfo::eliminateFrameIndex(MachineBasicBlock::iterator II,
                                           int SPAdj, unsigned FIOperandNum,
                                           RegScavenger *RS) const {
  assert(SPAdj == 0 && "Unexpected non-zero SP adjustment");

  MachineInstr &MI = *II;
  MachineBasicBlock &MBB = *MI.getParent();
  MachineFunction &MF = *MBB.getParent();
  const MachineFrameInfo &MFI = MF.getFrameInfo();
  const C166FrameLowering *TFI = getFrameLowering(MF);
  const TargetInstrInfo &TII = *MF.getSubtarget().getInstrInfo();
  DebugLoc DL = MI.getDebugLoc();

  int FrameIndex = MI.getOperand(FIOperandNum).getIndex();

  // Both the frame pointer and the stack pointer point at the bottom of the
  // local frame after the prologue has run, so the same offset works for both.
  // The callee saved register slots are the exception: they are written before
  // the frame pointer is live and read after the stack pointer has been put
  // back, so they always go through R0.
  Register BaseReg = C166::R0;
  if (TFI->hasFP(MF) && !isCalleeSavedFrameIndex(MFI, FrameIndex))
    BaseReg = C166::R1;

  int64_t Offset = MFI.getObjectOffset(FrameIndex) + MFI.getStackSize();

  // Fold the displacement that is already part of the instruction in.
  Offset += MI.getOperand(FIOperandNum + 1).getImm();

  if (MI.getOpcode() == C166::ADDframe) {
    // This is really a "load effective address of a stack slot".  C166 has no
    // such instruction, so expand it into a move plus an add.
    MI.setDesc(TII.get(C166::MOV16rr));
    MI.getOperand(FIOperandNum).ChangeToRegister(BaseReg, /*isDef=*/false);
    MI.removeOperand(FIOperandNum + 1);

    if (Offset == 0)
      return false;

    // The add becomes the real definition, so a dead result has to move with
    // it - the move now feeds the add.
    MachineOperand &Dst = MI.getOperand(0);
    Register DstReg = Dst.getReg();
    bool IsDead = Dst.isDead();
    Dst.setIsDead(false);

    // Seven or less fits the two byte #data3 form, which is also what the
    // assembler picks when it reads this back.
    int64_t Size = Offset < 0 ? -Offset : Offset;
    unsigned Opc;
    if (Size < 8)
      Opc = Offset < 0 ? C166::SUB16ri3 : C166::ADD16ri3;
    else
      Opc = Offset < 0 ? C166::SUB16ri : C166::ADD16ri;

    MachineInstrBuilder MIB =
        BuildMI(MBB, std::next(II), DL, TII.get(Opc))
            .addReg(DstReg, RegState::Define | getDeadRegState(IsDead))
            .addReg(DstReg)
            .addImm(Size);
    (void)MIB;
    return false;
  }

  // A frame slot that turns out to sit at offset zero can use the two byte
  // [Rw] form instead of the four byte one with nothing added.
  if (Offset == 0) {
    unsigned Short = 0;
    switch (MI.getOpcode()) {
    case C166::MOV16rm:
      Short = C166::MOV16rp;
      break;
    case C166::MOVB8rm:
      Short = C166::MOVB8rp;
      break;
    case C166::MOV16mr:
      Short = C166::MOV16pr;
      break;
    case C166::MOVB8mr:
      Short = C166::MOVB8pr;
      break;
    default:
      break;
    }
    if (Short) {
      MI.setDesc(TII.get(Short));
      MI.getOperand(FIOperandNum).ChangeToRegister(BaseReg, /*isDef=*/false);
      MI.removeOperand(FIOperandNum + 1);
      return false;
    }
  }

  MI.getOperand(FIOperandNum).ChangeToRegister(BaseReg, /*isDef=*/false);
  MI.getOperand(FIOperandNum + 1).ChangeToImmediate(Offset);
  return false;
}

Register C166RegisterInfo::getFrameRegister(const MachineFunction &MF) const {
  const C166FrameLowering *TFI = getFrameLowering(MF);
  return TFI->hasFP(MF) ? C166::R1 : C166::R0;
}
