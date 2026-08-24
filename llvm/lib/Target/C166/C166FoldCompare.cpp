//===-- C166FoldCompare.cpp - Drop compares the flags already answer ------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Nearly every arithmetic and logical instruction on this core sets Z and N
// from the value it produced, so "and r6, #16" followed by "cmp r6, #0" asks
// a question the AND has already answered.  This removes the compare.
//
// It is a pass rather than the usual optimizeCompareInstr() hook because of
// when the compare exists.  Until the post register allocation expansion a
// conditional branch here is a single BRCC pseudo carrying both the comparison
// and the condition; the compare only becomes an instruction of its own when
// that pseudo is split, which is after the peephole optimiser has run and the
// hook would have been called.  So the work happens late, where the compare
// and the jump that reads it are two adjacent instructions.
//
// Three things have to hold, and the second is the one that is easy to lose:
//
//   - The compare is against zero, so the Z and N it would set are the Z and
//     N of the value itself.
//
//   - The jump reads only Z and N.  A compare against zero also sets C and V,
//     to zero both times, and the instruction that produced the value will
//     generally have set them to something else - a shift leaves the last bit
//     shifted out in C, an add leaves its carry there.  So a condition that
//     reads either of those is not answered by what is already in PSW, even
//     though Z and N are right.  cc_SGT after a MOV is the case this rules
//     out, and it does occur.
//
//   - The instruction that produced the value is the one immediately before
//     the compare.  Asking instead that nothing in between writes PSW would
//     be wrong here, and quietly: MOV is deliberately not modelled as writing
//     PSW at all, because it leaves C alone and that is what lets a carry
//     survive the register shuffling around a wide addition.  It does clobber
//     Z and N on the part.  So the machine model's idea of what touches the
//     flags is not the part's, and a gap that looks empty can contain a move
//     that has already destroyed the answer.  That is not a theory: walking
//     back over anything the model called flag-preserving removed half as
//     many compares again and miscompiled two of the differential programs
//     into infinite loops.
//
//     Adjacency is not free - it is 83 compares rather than 128 over those
//     programs, so about a third of them are out of reach.  Getting those
//     would mean a list of what really preserves Z and N, which is a claim
//     about the part that would have to be checked against it rather than
//     against the model that is already wrong here.
//
// ADDC and SUBC set Z differently from everything else: they keep it set only
// if it already was, so that a wide value tests as zero exactly when every
// word of it did.  That is the answer to a different question from the one
// "cmp Rw, #0" asks, so they are not in the list below.
//
//===----------------------------------------------------------------------===//

#include "C166.h"
#include "C166InstrInfo.h"
#include "C166Subtarget.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/TargetRegisterInfo.h"

using namespace llvm;

#define DEBUG_TYPE "c166-fold-compare"

STATISTIC(NumRemoved, "Number of compares whose flags were already set");

namespace {

/// Whether this is a compare of a register against the constant zero.
static bool isCompareWithZero(const MachineInstr &MI) {
  switch (MI.getOpcode()) {
  case C166::CMP16ri:
  case C166::CMP16ri3:
  case C166::CMPB8ri:
  case C166::CMPB8ri3:
    break;
  default:
    return false;
  }
  return MI.getOperand(0).isReg() && MI.getOperand(1).isImm() &&
         MI.getOperand(1).getImm() == 0;
}

/// Whether a condition is decided by Z and N alone.
static bool readsOnlyZN(C166CC::CondCode CC) {
  switch (CC) {
  case C166CC::COND_Z:
  case C166CC::COND_NZ:
  case C166CC::COND_N:
  case C166CC::COND_NN:
    return true;
  default:
    return false;
  }
}

/// Whether this instruction sets Z and N from the value it writes to its
/// first operand.  Everything here goes through the same two flag writes in
/// the manual's description; ADDC and SUBC are left out on purpose, see the
/// note at the top of the file.
static bool setsZNFromResult(const MachineInstr &MI) {
  switch (MI.getOpcode()) {
  case C166::ADD16rr:  case C166::ADD16ri:  case C166::ADD16ri3:
  case C166::ADD16ra:  case C166::ADDB8rr:  case C166::ADDB8ri:
  case C166::ADDB8ri3: case C166::ADDB8ra:
  case C166::SUB16rr:  case C166::SUB16ri:  case C166::SUB16ri3:
  case C166::SUB16ra:  case C166::SUBB8rr:  case C166::SUBB8ri:
  case C166::SUBB8ri3: case C166::SUBB8ra:
  case C166::AND16rr:  case C166::AND16ri:  case C166::AND16ri3:
  case C166::AND16ra:  case C166::ANDB8rr:  case C166::ANDB8ri:
  case C166::ANDB8ri3: case C166::ANDB8ra:
  case C166::OR16rr:   case C166::OR16ri:   case C166::OR16ri3:
  case C166::OR16ra:   case C166::ORB8rr:   case C166::ORB8ri:
  case C166::ORB8ri3:  case C166::ORB8ra:
  case C166::XOR16rr:  case C166::XOR16ri:  case C166::XOR16ri3:
  case C166::XOR16ra:  case C166::XORB8rr:  case C166::XORB8ri:
  case C166::XORB8ri3: case C166::XORB8ra:
  case C166::SHL16rr:  case C166::SHL16ri:
  case C166::SHR16rr:  case C166::SHR16ri:
  case C166::ASHR16rr: case C166::ASHR16ri:
  case C166::ROL16rr:  case C166::ROL16ri:
  case C166::ROR16rr:  case C166::ROR16ri:
    return true;
  default:
    return false;
  }
}

class C166FoldCompare : public MachineFunctionPass {
public:
  static char ID;

  C166FoldCompare() : MachineFunctionPass(ID) {}

  bool runOnMachineFunction(MachineFunction &MF) override;

  StringRef getPassName() const override {
    return "C166 drop compares the flags already answer";
  }

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.setPreservesCFG();
    MachineFunctionPass::getAnalysisUsage(AU);
  }

private:
  const TargetRegisterInfo *TRI = nullptr;

  bool isRedundant(MachineBasicBlock::iterator Cmp,
                   MachineBasicBlock &MBB) const;
};

} // end anonymous namespace

char C166FoldCompare::ID = 0;

bool C166FoldCompare::isRedundant(MachineBasicBlock::iterator Cmp,
                                  MachineBasicBlock &MBB) const {
  if (!isCompareWithZero(*Cmp))
    return false;

  // The jump that reads the flags has to be the next instruction, which is how
  // the pseudo expansion emits the pair, and it has to want only Z and N.
  auto Jmp = std::next(Cmp);
  if (Jmp == MBB.end() || Jmp->getOpcode() != C166::JMPRcc)
    return false;
  auto CC = static_cast<C166CC::CondCode>(Jmp->getOperand(1).getImm());
  if (!readsOnlyZN(CC))
    return false;

  // Nothing else may read the flags this compare sets.  It is the last
  // instruction of the block that could, since the jump above ends it, but a
  // fallthrough successor could still have PSW live in.
  for (const MachineBasicBlock *Succ : MBB.successors())
    if (Succ->isLiveIn(C166::PSW))
      return false;

  Register Reg = Cmp->getOperand(0).getReg();

  // The instruction before the compare, skipping anything that is not one.
  auto I = MachineBasicBlock::reverse_iterator(Cmp);
  while (I != MBB.rend() && I->isDebugInstr())
    ++I;
  if (I == MBB.rend())
    return false;

  // It has to be what wrote the register being compared, and it has to set Z
  // and N from what it wrote.
  return setsZNFromResult(*I) && I->getOperand(0).isReg() &&
         I->getOperand(0).getReg() == Reg;
}

bool C166FoldCompare::runOnMachineFunction(MachineFunction &MF) {
  TRI = MF.getSubtarget().getRegisterInfo();

  bool Changed = false;
  for (MachineBasicBlock &MBB : MF)
    for (auto I = MBB.begin(); I != MBB.end();) {
      if (isRedundant(I, MBB)) {
        I = MBB.erase(I);
        ++NumRemoved;
        Changed = true;
        continue;
      }
      ++I;
    }
  return Changed;
}

INITIALIZE_PASS(C166FoldCompare, DEBUG_TYPE,
                "C166 drop compares the flags already answer", false, false)

FunctionPass *llvm::createC166FoldComparePass() { return new C166FoldCompare(); }
