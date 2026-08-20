//===-- C166InstrInfo.cpp - C166 Instruction Information ------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains the C166 implementation of the TargetInstrInfo class.
//
//===----------------------------------------------------------------------===//

#include "C166InstrInfo.h"
#include "C166.h"
#include "C166Subtarget.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineInstrBundle.h"
#include "llvm/CodeGen/MachineMemOperand.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/Support/ErrorHandling.h"

using namespace llvm;

#define GET_INSTRINFO_CTOR_DTOR
#include "C166GenInstrInfo.inc"

void C166InstrInfo::anchor() {}

C166InstrInfo::C166InstrInfo(const C166Subtarget &STI)
    : C166GenInstrInfo(STI, RI, C166::ADJCALLSTACKDOWN, C166::ADJCALLSTACKUP),
      RI() {}

void C166InstrInfo::copyPhysReg(MachineBasicBlock &MBB,
                                MachineBasicBlock::iterator I,
                                const DebugLoc &DL, Register DestReg,
                                Register SrcReg, bool KillSrc,
                                bool RenamableDest, bool RenamableSrc) const {
  unsigned Opc;
  if (C166::GR16RegClass.contains(DestReg, SrcReg))
    Opc = C166::MOV16rr;
  else if (C166::GR8RegClass.contains(DestReg, SrcReg))
    Opc = C166::MOVB8rr;
  else
    llvm_unreachable("Impossible register-to-register copy");

  BuildMI(MBB, I, DL, get(Opc), DestReg)
      .addReg(SrcReg, getKillRegState(KillSrc));
}

void C166InstrInfo::storeRegToStackSlot(MachineBasicBlock &MBB,
                                        MachineBasicBlock::iterator MI,
                                        Register SrcReg, bool IsKill,
                                        int FrameIndex,
                                        const TargetRegisterClass *RC,
                                        Register VReg,
                                        MachineInstr::MIFlag Flags) const {
  DebugLoc DL;
  if (MI != MBB.end())
    DL = MI->getDebugLoc();

  MachineFunction &MF = *MBB.getParent();
  MachineFrameInfo &MFI = MF.getFrameInfo();

  MachineMemOperand *MMO = MF.getMachineMemOperand(
      MachinePointerInfo::getFixedStack(MF, FrameIndex),
      MachineMemOperand::MOStore, MFI.getObjectSize(FrameIndex),
      MFI.getObjectAlign(FrameIndex));

  unsigned Opc;
  if (C166::GR16RegClass.hasSubClassEq(RC))
    Opc = C166::MOV16mr;
  else if (C166::GR8RegClass.hasSubClassEq(RC))
    Opc = C166::MOVB8mr;
  else
    llvm_unreachable("Cannot store this register to a stack slot!");

  BuildMI(MBB, MI, DL, get(Opc))
      .addFrameIndex(FrameIndex)
      .addImm(0)
      .addReg(SrcReg, getKillRegState(IsKill))
      .addMemOperand(MMO);
}

void C166InstrInfo::loadRegFromStackSlot(MachineBasicBlock &MBB,
                                         MachineBasicBlock::iterator MI,
                                         Register DestReg, int FrameIndex,
                                         const TargetRegisterClass *RC,
                                         Register VReg, unsigned SubReg,
                                         MachineInstr::MIFlag Flags) const {
  DebugLoc DL;
  if (MI != MBB.end())
    DL = MI->getDebugLoc();

  MachineFunction &MF = *MBB.getParent();
  MachineFrameInfo &MFI = MF.getFrameInfo();

  MachineMemOperand *MMO = MF.getMachineMemOperand(
      MachinePointerInfo::getFixedStack(MF, FrameIndex),
      MachineMemOperand::MOLoad, MFI.getObjectSize(FrameIndex),
      MFI.getObjectAlign(FrameIndex));

  unsigned Opc;
  if (C166::GR16RegClass.hasSubClassEq(RC))
    Opc = C166::MOV16rm;
  else if (C166::GR8RegClass.hasSubClassEq(RC))
    Opc = C166::MOVB8rm;
  else
    llvm_unreachable("Cannot load this register from a stack slot!");

  BuildMI(MBB, MI, DL, get(Opc), DestReg)
      .addFrameIndex(FrameIndex)
      .addImm(0)
      .addMemOperand(MMO);
}

/// Expand the multiply and divide pseudos.  The C166 multiply/divide unit
/// always reads and writes the MDL/MDH register pair, so the operations are
/// modelled as pseudos that get surrounded by the required moves here.
bool C166InstrInfo::expandPostRAPseudo(MachineInstr &MI) const {
  MachineBasicBlock &MBB = *MI.getParent();
  DebugLoc DL = MI.getDebugLoc();

  auto Emit = [&](unsigned Opc) {
    return BuildMI(MBB, MI, DL, get(Opc));
  };

  switch (MI.getOpcode()) {
  default:
    return false;
  case C166::BRCC16rr:
  case C166::BRCC16ri:
  case C166::BRCC8rr:
  case C166::BRCC8ri: {
    // Nothing may come between the compare and the jump that reads its flags,
    // which is exactly why the two travelled together until now.
    unsigned CmpOpc;
    switch (MI.getOpcode()) {
    case C166::BRCC16rr:
      CmpOpc = C166::CMP16rr;
      break;
    case C166::BRCC16ri:
      CmpOpc = C166::CMP16ri;
      break;
    case C166::BRCC8rr:
      CmpOpc = C166::CMPB8rr;
      break;
    default:
      CmpOpc = C166::CMPB8ri;
      break;
    }

    Emit(CmpOpc).add(MI.getOperand(1)).add(MI.getOperand(2));
    Emit(C166::JMPAcc)
        .addMBB(MI.getOperand(0).getMBB())
        .addImm(MI.getOperand(3).getImm());
    break;
  }
  case C166::FARLOAD16:
  case C166::FARLOAD8:
  case C166::FARSTORE16:
  case C166::FARSTORE8:
  case C166::FARLOAD16i:
  case C166::FARLOAD8i:
  case C166::FARSTORE16i:
  case C166::FARSTORE8i: {
    // EXTS makes the address of the instruction that follows it a plain 16 bit
    // offset into the segment held by its register operand, and only covers
    // that one instruction, so the pair must not be broken up.  The hardware
    // locks interrupts for the sequence as well, so nothing observes the
    // partly extended state either.
    bool IsStore = MI.getOpcode() == C166::FARSTORE16 ||
                   MI.getOpcode() == C166::FARSTORE8 ||
                   MI.getOpcode() == C166::FARSTORE16i ||
                   MI.getOpcode() == C166::FARSTORE8i;
    bool IsByte =
        MI.getOpcode() == C166::FARLOAD8 || MI.getOpcode() == C166::FARSTORE8 ||
        MI.getOpcode() == C166::FARLOAD8i || MI.getOpcode() == C166::FARSTORE8i;
    unsigned Opc = IsStore ? (IsByte ? C166::MOVB8mr : C166::MOV16mr)
                           : (IsByte ? C166::MOVB8rm : C166::MOV16rm);

    // The operands are (value, offset, segment), with the value defined
    // rather than used for a load.
    const MachineOperand &Value = MI.getOperand(0);
    const MachineOperand &Offset = MI.getOperand(1);
    const MachineOperand &Segment = MI.getOperand(2);

    // A segment that is a symbol goes straight into the EXTS as an immediate;
    // one that had to be computed is in a register.
    MachineInstrBuilder Exts;
    if (Segment.isReg())
      Exts = Emit(C166::EXTSr)
                 .addReg(Segment.getReg(), getKillRegState(Segment.isKill()))
                 .addImm(1);
    else
      Exts = Emit(C166::EXTSi).add(Segment).addImm(1);

    MachineInstrBuilder Access = Emit(Opc);
    if (IsStore)
      Access.addReg(Offset.getReg(), getKillRegState(Offset.isKill()))
          .addImm(0)
          .add(Value);
    else
      Access.add(Value)
          .addReg(Offset.getReg(), getKillRegState(Offset.isKill()))
          .addImm(0);
    Access.cloneMemRefs(MI);

    // The post-RA scheduler runs straight after this pass and would happily
    // drop an unrelated instruction into the middle of the pair, so tie the
    // two together where nothing can get at them.
    finalizeBundle(MBB, Exts->getIterator(), std::next(Access->getIterator()));
    break;
  }
  case C166::MUL16rr:
  case C166::MULHS16rr:
  case C166::MULHU16rr: {
    Register Dst = MI.getOperand(0).getReg();
    Register Src1 = MI.getOperand(1).getReg();
    Register Src2 = MI.getOperand(2).getReg();
    bool Unsigned = MI.getOpcode() == C166::MULHU16rr;
    bool High = MI.getOpcode() != C166::MUL16rr;

    Emit(Unsigned ? C166::MULUrr : C166::MULrr)
        .addReg(Src1)
        .addReg(Src2);
    Emit(High ? C166::MOVfromMDH : C166::MOVfromMDL).addDef(Dst);
    break;
  }
  case C166::SDIV16rr:
  case C166::UDIV16rr:
  case C166::SREM16rr:
  case C166::UREM16rr: {
    Register Dst = MI.getOperand(0).getReg();
    Register Src1 = MI.getOperand(1).getReg();
    Register Src2 = MI.getOperand(2).getReg();
    bool Unsigned =
        MI.getOpcode() == C166::UDIV16rr || MI.getOpcode() == C166::UREM16rr;
    bool Remainder =
        MI.getOpcode() == C166::SREM16rr || MI.getOpcode() == C166::UREM16rr;

    // The dividend goes into MDL, the divisor stays in a GPR.  DIV leaves the
    // quotient in MDL and the remainder in MDH.
    Emit(C166::MOVtoMDL).addReg(Src1);
    Emit(Unsigned ? C166::DIVUr : C166::DIVr).addReg(Src2);
    Emit(Remainder ? C166::MOVfromMDH : C166::MOVfromMDL).addDef(Dst);
    break;
  }
  }

  MI.eraseFromParent();
  return true;
}

//===----------------------------------------------------------------------===//
// Branch analysis
//===----------------------------------------------------------------------===//
//
// A conditional branch is represented in two different ways depending on how
// far code generation has progressed.  Up to and including the post register
// allocation pseudo expansion it is a single BRCC* instruction that carries
// both the comparison operands and the condition code; afterwards it is a
// plain JMPAcc preceded by a compare.  The condition returned by
// analyzeBranch() therefore always starts with the branch opcode, followed by
// the condition code and - for the fused form - the two compare operands.
//
//===----------------------------------------------------------------------===//

static bool isCondBranchOpcode(unsigned Opc) {
  switch (Opc) {
  case C166::BRCC16rr:
  case C166::BRCC16ri:
  case C166::BRCC8rr:
  case C166::BRCC8ri:
  case C166::JMPAcc:
    return true;
  default:
    return false;
  }
}

static void parseCondBranch(MachineInstr &MI, MachineBasicBlock *&Target,
                            SmallVectorImpl<MachineOperand> &Cond) {
  assert(isCondBranchOpcode(MI.getOpcode()) && "Not a conditional branch");

  Target = MI.getOperand(0).getMBB();
  Cond.push_back(MachineOperand::CreateImm(MI.getOpcode()));

  if (MI.getOpcode() == C166::JMPAcc) {
    Cond.push_back(MI.getOperand(1));
    return;
  }

  Cond.push_back(MI.getOperand(3));
  Cond.push_back(MI.getOperand(1));
  Cond.push_back(MI.getOperand(2));
}

bool C166InstrInfo::reverseBranchCondition(
    SmallVectorImpl<MachineOperand> &Cond) const {
  assert((Cond.size() == 2 || Cond.size() == 4) &&
         "Invalid branch condition!");

  auto CC = static_cast<C166CC::CondCode>(Cond[1].getImm());
  C166CC::CondCode Opposite = C166CC::getOppositeCondition(CC);
  if (Opposite == C166CC::COND_INVALID)
    return true;

  Cond[1].setImm(Opposite);
  return false;
}

bool C166InstrInfo::analyzeBranch(MachineBasicBlock &MBB,
                                  MachineBasicBlock *&TBB,
                                  MachineBasicBlock *&FBB,
                                  SmallVectorImpl<MachineOperand> &Cond,
                                  bool AllowModify) const {
  TBB = nullptr;
  FBB = nullptr;
  Cond.clear();

  MachineBasicBlock::iterator I = MBB.getLastNonDebugInstr();
  if (I == MBB.end() || !isUnpredicatedTerminator(*I))
    return false;

  // Count the terminators and remember the first branch that ends the block
  // unconditionally.
  MachineBasicBlock::iterator FirstUncondOrIndirectBr = MBB.end();
  int NumTerminators = 0;
  for (auto J = I.getReverse();
       J != MBB.rend() && isUnpredicatedTerminator(*J); ++J) {
    NumTerminators++;
    if (J->getDesc().isUnconditionalBranch() ||
        J->getDesc().isIndirectBranch())
      FirstUncondOrIndirectBr = J.getReverse();
  }

  // Anything after an unconditional branch is unreachable.
  if (AllowModify && FirstUncondOrIndirectBr != MBB.end()) {
    while (std::next(FirstUncondOrIndirectBr) != MBB.end()) {
      std::next(FirstUncondOrIndirectBr)->eraseFromParent();
      NumTerminators--;
    }
    I = FirstUncondOrIndirectBr;
  }

  // An indirect branch names no basic blocks we could reason about.
  if (I->getDesc().isIndirectBranch())
    return true;

  // Anything with more than a conditional plus an unconditional branch is
  // beyond what this hook can describe.
  if (NumTerminators > 2)
    return true;

  if (NumTerminators == 1) {
    if (I->getDesc().isUnconditionalBranch()) {
      TBB = I->getOperand(0).getMBB();
      return false;
    }
    if (isCondBranchOpcode(I->getOpcode())) {
      parseCondBranch(*I, TBB, Cond);
      return false;
    }
    return true;
  }

  if (NumTerminators == 2 && isCondBranchOpcode(std::prev(I)->getOpcode()) &&
      I->getDesc().isUnconditionalBranch()) {
    parseCondBranch(*std::prev(I), TBB, Cond);
    FBB = I->getOperand(0).getMBB();
    return false;
  }

  return true;
}

unsigned C166InstrInfo::removeBranch(MachineBasicBlock &MBB,
                                     int *BytesRemoved) const {
  if (BytesRemoved)
    *BytesRemoved = 0;

  MachineBasicBlock::iterator I = MBB.getLastNonDebugInstr();
  if (I == MBB.end())
    return 0;

  if (I->getOpcode() != C166::JMPA && !isCondBranchOpcode(I->getOpcode()))
    return 0;

  if (BytesRemoved)
    *BytesRemoved += getInstSizeInBytes(*I);
  I->eraseFromParent();

  I = MBB.getLastNonDebugInstr();
  if (I == MBB.end() || !isCondBranchOpcode(I->getOpcode()))
    return 1;

  if (BytesRemoved)
    *BytesRemoved += getInstSizeInBytes(*I);
  I->eraseFromParent();
  return 2;
}

unsigned C166InstrInfo::insertBranch(MachineBasicBlock &MBB,
                                     MachineBasicBlock *TBB,
                                     MachineBasicBlock *FBB,
                                     ArrayRef<MachineOperand> Cond,
                                     const DebugLoc &DL, int *BytesAdded) const {
  assert(TBB && "insertBranch must not be told to insert a fallthrough");
  assert((Cond.size() == 0 || Cond.size() == 2 || Cond.size() == 4) &&
         "Invalid branch condition!");

  if (BytesAdded)
    *BytesAdded = 0;

  if (Cond.empty()) {
    assert(!FBB && "Unconditional branch with multiple successors!");
    MachineInstr &MI = *BuildMI(&MBB, DL, get(C166::JMPA)).addMBB(TBB);
    if (BytesAdded)
      *BytesAdded += getInstSizeInBytes(MI);
    return 1;
  }

  unsigned Opc = Cond[0].getImm();
  MachineInstrBuilder MIB = BuildMI(&MBB, DL, get(Opc)).addMBB(TBB);
  if (Opc != C166::JMPAcc) {
    MIB.add(Cond[2]);
    MIB.add(Cond[3]);
  }
  MIB.add(Cond[1]);

  if (BytesAdded)
    *BytesAdded += getInstSizeInBytes(*MIB);

  if (!FBB)
    return 1;

  MachineInstr &MI = *BuildMI(&MBB, DL, get(C166::JMPA)).addMBB(FBB);
  if (BytesAdded)
    *BytesAdded += getInstSizeInBytes(MI);
  return 2;
}

std::pair<unsigned, unsigned>
C166InstrInfo::decomposeMachineOperandsTargetFlags(unsigned TF) const {
  // Every C166 target flag is a plain value; none of them are bitmasks.
  return {TF, 0u};
}

ArrayRef<std::pair<unsigned, const char *>>
C166InstrInfo::getSerializableDirectMachineOperandTargetFlags() const {
  using namespace C166II;
  static const std::pair<unsigned, const char *> Flags[] = {
      {MO_SEG, "c166-seg"},
      {MO_SOF, "c166-sof"},
  };
  return Flags;
}
