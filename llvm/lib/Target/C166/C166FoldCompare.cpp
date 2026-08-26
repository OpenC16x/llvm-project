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
//   - Nothing between that instruction and the compare writes Z or N, and
//     nothing between them writes the register either.
//
//     This used to be the stricter rule that the two be adjacent, because the
//     machine description did not model the flags a move writes and so could
//     not be asked.  It does now, which is what lets the walk ask it: a move
//     in the way stops the walk, as it must, because it leaves C alone but
//     clobbers Z and N.  Getting that wrong is not a theory - walking back
//     over anything the old model called flag-preserving miscompiled two of
//     the differential programs into infinite loops.
//
//     In practice the walk reaches little that adjacency did not, because
//     almost everything writes Z and N: on this part only the jumps, the
//     calls, the EXTend prefixes, NOP and ATOMIC leave them alone.  It is
//     here because it is the rule that is actually true, rather than a
//     syntactic accident that happened to be safe.
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
/// first operand.
///
/// This is a stronger question than the machine description answers.  The
/// description says which flags an instruction writes; this asks whether the
/// flags it wrote describe the value in its first operand, which is what the
/// compare is about.  Two kinds of near miss are left out because checking
/// found them: MOV16prd and MOVB8prd, the pre-decrement stores, do write Z and
/// N but from the value moved rather than the pointer they leave behind, and
/// ADDC and SUBC set Z only if it was already set, so that a wide value tests
/// as zero exactly when every word of it did - which is the answer to a
/// different question from the one "cmp Rw, #0" asks.
///
/// This list has to be kept up with C166InstrInfo.td.  An ALU instruction added
/// there and not here costs a fold, which is only a missed optimisation; one
/// added here that does not really set Z and N from its result removes a
/// compare that was doing something, which is a miscompile.  That asymmetry is
/// why this is a list of what does rather than of what does not.  The answer
/// for a given instruction is its Flags row in the Instruction Set Manual for
/// the C166 Family (V2.0, Mar. 2001), which is where the rest of the flag
/// behaviour in this backend comes from - only a "-" there means untouched.
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
  // A move sets Z and N from the value it moved, which for the forms that
  // write a register is the value that ends up in it - so a compare of that
  // register against zero is answered too.  It leaves C and V alone, which is
  // why the condition still has to be one that does not read them.
  case C166::MOV16rr:   case C166::MOVB8rr:
  case C166::MOV16ri:   case C166::MOV16ri4:
  case C166::MOVB8ri:   case C166::MOVB8ri4:
  case C166::MOV16ra:   case C166::MOVB8ra:
  case C166::MOV16rp:   case C166::MOVB8rp:
  case C166::MOV16rm:   case C166::MOVB8rm:
  case C166::MOV16rpi:  case C166::MOVB8rpi:
  case C166::MOV16regi: case C166::MOVB8regi:
  case C166::MOV16rega: case C166::MOVB8rega:
  // MOVBZ forces N to zero and MOVBS takes it from the byte's sign, which is
  // the sign of the word each of them leaves behind, and both set Z from the
  // byte - zero exactly when the widened word is.  So the flags describe the
  // word that was written, which is what is being compared.
  case C166::MOVBZ16r8: case C166::MOVBS16r8:
    return true;
  default:
    return false;
  }
}

/// Whether this instruction leaves Z and N as it found them.
///
/// The machine description answers this for everything except the four moves
/// that put a constant in a register.  Those write Z and N like any other move
/// and say they do not, because declaring it would stop LLVM rematerializing
/// them; C166InstrInfo.td sets out why, and this is the other end of that
/// bargain.  Leaving them out here is not a missed fold but a miscompile: the
/// walk would step over "mov Rw, #0" and reuse flags it had already destroyed,
/// which is what the differential programs caught when it did.
static bool writesZN(const MachineInstr &MI, const TargetRegisterInfo *TRI) {
  switch (MI.getOpcode()) {
  case C166::MOV16ri:
  case C166::MOV16ri4:
  case C166::MOVB8ri:
  case C166::MOVB8ri4:
    return true;
  default:
    break;
  }
  return MI.modifiesRegister(C166::PSW_Z, TRI) ||
         MI.modifiesRegister(C166::PSW_N, TRI);
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

  // Walk back to whatever wrote the register, over anything that leaves Z and
  // N alone.  The machine description is what says which those are, and it is
  // now right about it: the flags a move writes are declared, so a move in the
  // way stops this walk rather than being stepped over.
  for (auto I = MachineBasicBlock::reverse_iterator(Cmp); I != MBB.rend();
       ++I) {
    if (I->isDebugInstr())
      continue;

    if (setsZNFromResult(*I) && I->getOperand(0).isReg() &&
        I->getOperand(0).getReg() == Reg)
      return true;

    // Anything that writes either flag has destroyed the answer, and anything
    // that writes the register without setting the flags from it has changed
    // what the question is about.
    if (writesZN(*I, TRI) || I->modifiesRegister(Reg, TRI))
      return false;
  }
  return false;
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
