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
#include "llvm/Target/TargetMachine.h"
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
  case C166::ATOMADD16:
  case C166::ATOMSUB16:
  case C166::ATOMAND16:
  case C166::ATOMOR16:
  case C166::ATOMXOR16:
  case C166::ATOMADD8:
  case C166::ATOMSUB8:
  case C166::ATOMAND8:
  case C166::ATOMOR8:
  case C166::ATOMXOR8:
  case C166::ATOMSWAP16:
  case C166::ATOMSWAP8: {
    // ATOMIC #n holds interrupts and PEC transfers off for the next n
    // instructions, so what follows has to be exactly the n instructions
    // counted here: a read, the change, and the write back.  Nothing in it
    // changes the program flow and nothing in it is an EXTend, which are the
    // two things the single instruction counter cannot survive.
    bool Swap = MI.getOpcode() == C166::ATOMSWAP16 ||
                MI.getOpcode() == C166::ATOMSWAP8;
    bool Byte = MI.getOpcode() == C166::ATOMADD8 ||
                MI.getOpcode() == C166::ATOMSUB8 ||
                MI.getOpcode() == C166::ATOMAND8 ||
                MI.getOpcode() == C166::ATOMOR8 ||
                MI.getOpcode() == C166::ATOMXOR8 ||
                MI.getOpcode() == C166::ATOMSWAP8;

    Register Dst = MI.getOperand(0).getReg();
    Register Tmp = Swap ? Register() : MI.getOperand(1).getReg();
    Register Addr = MI.getOperand(Swap ? 1 : 2).getReg();
    Register Val = MI.getOperand(Swap ? 2 : 3).getReg();

    unsigned LoadOpc = Byte ? C166::MOVB8rp : C166::MOV16rp;
    unsigned StoreOpc = Byte ? C166::MOVB8pr : C166::MOV16pr;
    unsigned CopyOpc = Byte ? C166::MOVB8rr : C166::MOV16rr;

    unsigned AluOpc = 0;
    switch (MI.getOpcode()) {
    case C166::ATOMADD16:
      AluOpc = C166::ADD16rr;
      break;
    case C166::ATOMSUB16:
      AluOpc = C166::SUB16rr;
      break;
    case C166::ATOMAND16:
      AluOpc = C166::AND16rr;
      break;
    case C166::ATOMOR16:
      AluOpc = C166::OR16rr;
      break;
    case C166::ATOMXOR16:
      AluOpc = C166::XOR16rr;
      break;
    case C166::ATOMADD8:
      AluOpc = C166::ADDB8rr;
      break;
    case C166::ATOMSUB8:
      AluOpc = C166::SUBB8rr;
      break;
    case C166::ATOMAND8:
      AluOpc = C166::ANDB8rr;
      break;
    case C166::ATOMOR8:
      AluOpc = C166::ORB8rr;
      break;
    case C166::ATOMXOR8:
      AluOpc = C166::XORB8rr;
      break;
    }

    // An exchange is a read and a write; anything else copies what it read so
    // that the old value survives the change, and is four.
    Emit(C166::ATOMIC).addImm(Swap ? 2 : 4);
    Emit(LoadOpc).addReg(Dst, RegState::Define).addReg(Addr);
    if (!Swap) {
      Emit(CopyOpc).addReg(Tmp, RegState::Define).addReg(Dst);
      Emit(AluOpc).addReg(Tmp, RegState::Define).addReg(Tmp).addReg(Val);
    }
    Emit(StoreOpc).addReg(Addr).addReg(Swap ? Val : Tmp);
    MI.eraseFromParent();
    return true;
  }
  case C166::BRCC16rr:
  case C166::BRCC16ri:
  case C166::BRCC8rr:
  case C166::BRCC8ri: {
    // Nothing may come between the compare and the jump that reads its flags,
    // which is exactly why the two travelled together until now.
    // A constant of 0 to 7 fits the two byte form of the compare.
    const MachineOperand &Rhs = MI.getOperand(2);
    bool Short = Rhs.isImm() && Rhs.getImm() >= 0 && Rhs.getImm() < 8;

    unsigned CmpOpc;
    switch (MI.getOpcode()) {
    case C166::BRCC16rr:
      CmpOpc = C166::CMP16rr;
      break;
    case C166::BRCC16ri:
      CmpOpc = Short ? C166::CMP16ri3 : C166::CMP16ri;
      break;
    case C166::BRCC8rr:
      CmpOpc = C166::CMPB8rr;
      break;
    default:
      CmpOpc = Short ? C166::CMPB8ri3 : C166::CMPB8ri;
      break;
    }

    Emit(CmpOpc).add(MI.getOperand(1)).add(MI.getOperand(2));
    Emit(C166::JMPRcc)
        .addMBB(MI.getOperand(0).getMBB())
        .addImm(MI.getOperand(3).getImm());
    break;
  }
  case C166::TCRETURNs: {
    // A far tail call keeps the segment its caller expects RETS to come back
    // from, so it names the callee's segment outright.
    Emit(C166::TAILJMPs).add(MI.getOperand(0)).add(MI.getOperand(1));
    break;
  }
  case C166::TCRETURNa:
  case C166::TCRETURNi: {
    // The frame is already gone by the time this runs, so all that is left is
    // the jump that the callee will return from on our behalf.
    bool Indirect = MI.getOpcode() == C166::TCRETURNi;
    Emit(Indirect ? C166::TAILJMPi : C166::TAILJMPa).add(MI.getOperand(0));
    break;
  }
  case C166::FARLOAD16:
  case C166::FARLOAD8:
  case C166::FARSTORE16:
  case C166::FARSTORE8:
  case C166::FARLOAD16i:
  case C166::FARLOAD8i:
  case C166::FARSTORE16i:
  case C166::FARSTORE8i:
  case C166::FARLOAD16a:
  case C166::FARLOAD8a:
  case C166::FARSTORE16a:
  case C166::FARSTORE8a: {
    // EXTS makes the address of the instruction that follows it a plain 16 bit
    // offset into the segment held by its register operand, and only covers
    // that one instruction, so the pair must not be broken up.  The hardware
    // locks interrupts for the sequence as well, so nothing observes the
    // partly extended state either.
    bool IsStore = MI.mayStore();
    bool IsByte =
        MI.getOpcode() == C166::FARLOAD8 || MI.getOpcode() == C166::FARSTORE8 ||
        MI.getOpcode() == C166::FARLOAD8i ||
        MI.getOpcode() == C166::FARSTORE8i ||
        MI.getOpcode() == C166::FARLOAD8a || MI.getOpcode() == C166::FARSTORE8a;

    // The operands are (value, offset, segment), with the value defined
    // rather than used for a load.  Either half of the address may already be
    // settled at link time, in which case it is an immediate rather than a
    // register and the access names it outright.
    const MachineOperand &Value = MI.getOperand(0);
    const MachineOperand &Offset = MI.getOperand(1);
    const MachineOperand &Segment = MI.getOperand(2);

    unsigned Opc;
    if (Offset.isReg())
      // The address is the offset register on its own, so this is the two byte
      // [Rw] form rather than the four byte one with nothing added.
      Opc = IsStore ? (IsByte ? C166::MOVB8pr : C166::MOV16pr)
                    : (IsByte ? C166::MOVB8rp : C166::MOV16rp);
    else
      Opc = IsStore ? (IsByte ? C166::MOVB8ar : C166::MOV16ar)
                    : (IsByte ? C166::MOVB8ra : C166::MOV16ra);

    MachineInstrBuilder Exts;
    if (Segment.isReg())
      Exts = Emit(C166::EXTSr)
                 .addReg(Segment.getReg(), getKillRegState(Segment.isKill()))
                 .addImm(1);
    else
      Exts = Emit(C166::EXTSi).add(Segment).addImm(1);

    auto AddOffset = [&](MachineInstrBuilder &MIB) {
      if (Offset.isReg())
        MIB.addReg(Offset.getReg(), getKillRegState(Offset.isKill()));
      else
        MIB.add(Offset);
    };

    MachineInstrBuilder Access = Emit(Opc);
    if (IsStore) {
      AddOffset(Access);
      Access.add(Value);
    } else {
      Access.add(Value);
      AddOffset(Access);
    }
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
  case C166::SMUL16rrLOHI:
  case C166::UMUL16rrLOHI: {
    // One MUL, then both halves out of the register pair it left them in.
    Register Lo = MI.getOperand(0).getReg();
    Register Hi = MI.getOperand(1).getReg();
    Register Src1 = MI.getOperand(2).getReg();
    Register Src2 = MI.getOperand(3).getReg();
    bool Unsigned = MI.getOpcode() == C166::UMUL16rrLOHI;

    Emit(Unsigned ? C166::MULUrr : C166::MULrr).addReg(Src1).addReg(Src2);
    Emit(C166::MOVfromMDL).addDef(Lo);
    Emit(C166::MOVfromMDH).addDef(Hi);
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

/// The bit branches, whose condition is which bit of what rather than a
/// condition code, and which are inverted by swapping the two opcodes.
static bool isBitBranchOpcode(unsigned Opc) {
  return Opc == C166::JBr || Opc == C166::JNBr || Opc == C166::JBm ||
         Opc == C166::JNBm;
}

static unsigned getInvertedBitBranch(unsigned Opc) {
  switch (Opc) {
  case C166::JBr:
    return C166::JNBr;
  case C166::JNBr:
    return C166::JBr;
  case C166::JBm:
    return C166::JNBm;
  case C166::JNBm:
    return C166::JBm;
  }
  llvm_unreachable("not a bit branch");
}

static bool isCondBranchOpcode(unsigned Opc) {
  switch (Opc) {
  case C166::JBr:
  case C166::JNBr:
  case C166::JBm:
  case C166::JNBm:
  case C166::BRCC16rr:
  case C166::BRCC16ri:
  case C166::BRCC8rr:
  case C166::BRCC8ri:
  case C166::JMPAcc:
  case C166::JMPRcc:
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

  // A bit branch carries which word and which bit; the opcode itself says
  // whether it goes on set or on clear.
  if (isBitBranchOpcode(MI.getOpcode())) {
    Cond.push_back(MI.getOperand(1));
    Cond.push_back(MI.getOperand(2));
    return;
  }

  if (MI.getOpcode() == C166::JMPAcc || MI.getOpcode() == C166::JMPRcc) {
    Cond.push_back(MI.getOperand(1));
    return;
  }

  Cond.push_back(MI.getOperand(3));
  Cond.push_back(MI.getOperand(1));
  Cond.push_back(MI.getOperand(2));
}

bool C166InstrInfo::reverseBranchCondition(
    SmallVectorImpl<MachineOperand> &Cond) const {
  assert((Cond.size() == 2 || Cond.size() == 3 || Cond.size() == 4) &&
         "Invalid branch condition!");

  // Testing the same bit the other way round is a different instruction
  // rather than a different condition code.
  if (isBitBranchOpcode(Cond[0].getImm())) {
    Cond[0].setImm(getInvertedBitBranch(Cond[0].getImm()));
    return false;
  }

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

  // A tail call ends the block like a return does, but it names a callee
  // rather than a basic block, so there is nothing here to describe.
  if (I->getOpcode() == C166::TCRETURNa || I->getOpcode() == C166::TCRETURNi ||
      I->getOpcode() == C166::TCRETURNs)
    return true;

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

  if (I->getOpcode() != C166::JMPA && I->getOpcode() != C166::JMPR &&
      !isCondBranchOpcode(I->getOpcode()))
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
  assert((Cond.size() == 0 || Cond.size() == 2 || Cond.size() == 3 ||
          Cond.size() == 4) &&
         "Invalid branch condition!");

  if (BytesAdded)
    *BytesAdded = 0;

  if (Cond.empty()) {
    assert(!FBB && "Unconditional branch with multiple successors!");
    MachineInstr &MI = *BuildMI(&MBB, DL, get(C166::JMPR)).addMBB(TBB);
    if (BytesAdded)
      *BytesAdded += getInstSizeInBytes(MI);
    return 1;
  }

  unsigned Opc = Cond[0].getImm();
  MachineInstrBuilder MIB = BuildMI(&MBB, DL, get(Opc)).addMBB(TBB);
  if (isBitBranchOpcode(Opc)) {
    MIB.add(Cond[1]);
    MIB.add(Cond[2]);
  } else {
    if (Opc != C166::JMPAcc && Opc != C166::JMPRcc) {
      MIB.add(Cond[2]);
      MIB.add(Cond[3]);
    }
    MIB.add(Cond[1]);
  }

  if (BytesAdded)
    *BytesAdded += getInstSizeInBytes(*MIB);

  if (!FBB)
    return 1;

  MachineInstr &MI = *BuildMI(&MBB, DL, get(C166::JMPR)).addMBB(FBB);
  if (BytesAdded)
    *BytesAdded += getInstSizeInBytes(MI);
  return 2;
}

unsigned C166InstrInfo::getInstSizeInBytes(const MachineInstr &MI) const {
  // A bundle holds an EXTend and the access it covers, and carries no size of
  // its own; what it costs is what is inside it.
  if (MI.isBundle())
    return getInstBundleSize(MI);

  // Debug values, labels and the like are written down rather than assembled.
  if (MI.isMetaInstruction())
    return 0;

  // Inline assembly is however long the text turns out to be.
  if (MI.isInlineAsm()) {
    const MachineFunction &MF = *MI.getParent()->getParent();
    return getInlineAsmLength(MI.getOperand(0).getSymbolName(),
                              MF.getTarget().getMCAsmInfo());
  }

  // JMPR is two bytes, but the assembler grows it to a four byte JMPA when the
  // target turns out to be too far, and that happens after everything here has
  // measured the code.  Reporting the larger size makes every offset the
  // branch relaxation works from an upper bound, so a bit branch it judges to
  // be in range really is one.
  switch (MI.getOpcode()) {
  case C166::JMPR:
  case C166::JMPRcc:
    return 4;
  default:
    return MI.getDesc().getSize();
  }
}

MachineBasicBlock *
C166InstrInfo::getBranchDestBlock(const MachineInstr &MI) const {
  // Every branch here keeps its target in operand 0, including the bit
  // branches, which is why their operands are not in the order they print in.
  return MI.getOperand(0).getMBB();
}

bool C166InstrInfo::isBranchOffsetInRange(unsigned BranchOpc,
                                          int64_t BrOffset) const {
  // The bit branches are the only ones with a range to run out of: they take a
  // signed 8 bit count of words from the instruction after them, and the
  // instruction set has no long form to grow them into.  Everything else
  // either names an address outright or is a JMPR, which the assembler turns
  // into a JMPA when it has to.
  if (!isBitBranchOpcode(BranchOpc))
    return true;

  // BrOffset is measured from the start of the four byte branch.
  int64_t FromNext = BrOffset - 4;
  assert(!(FromNext & 1) && "instructions are an even number of bytes");
  return isInt<8>(FromNext / 2);
}

void C166InstrInfo::insertIndirectBranch(MachineBasicBlock &MBB,
                                         MachineBasicBlock &DestBB,
                                         MachineBasicBlock &RestoreBB,
                                         const DebugLoc &DL, int64_t BrOffset,
                                         RegScavenger *RS) const {
  // Reached only when a plain jump cannot get there either, and JMPA covers
  // the whole 64K segment a function lives in, so a function big enough to
  // need this has other problems.
  report_fatal_error("C166: a branch target too far away for JMPA");
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
