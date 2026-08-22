//===-- C166MergeExtend.cpp - Merge adjacent EXTend prefixes --------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// An EXTend instruction overrides the addressing of the instructions after it,
// and how many is written into it: one to four.  A far access is lowered as one
// EXTend covering one instruction, so two accesses to the same object produce
// two of them, and an EXTend is four bytes.
//
// This merges the second into the first where doing so cannot change what the
// program means.  Two conditions make that so:
//
//   - The two EXTends name the same segment or page.  Identical operands do,
//     and so do two offsets into the same object: "seg(g)" and "seg(g + 2)"
//     differ only if g straddles a segment boundary.  Nothing in the object
//     file says it does not, so the linker script says it instead - c166.ld
//     asserts that no section a far access can reach crosses a segment
//     boundary, which is what makes this sound.  It is also where the value is:
//     a program reaches two fields of a far structure far more often than it
//     reaches the same word twice.
//
//   - Nothing between them touches memory.  The instructions in the gap are
//     currently addressed the ordinary way, through the data page pointers, and
//     widening the first EXTend brings them under its override instead.  That
//     is only harmless if they have no data access to redirect.
//
// So this turns
//
//     exts #seg(a), #1        exts #seg(a), #3
//     mov  r2, sof(a)         mov  r2, sof(a)
//     add  r2, #1        ->   add  r2, #1
//     exts #seg(a), #1        mov  sof(a), r2
//     mov  sof(a), r2
//
// which is four bytes and a cycle less each time it fires.
//
//===----------------------------------------------------------------------===//

#include "C166.h"
#include "C166InstrInfo.h"
#include "C166Subtarget.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachineInstrBundle.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/InitializePasses.h"
#include "llvm/Support/Debug.h"

using namespace llvm;

#define DEBUG_TYPE "c166-merge-extend"

STATISTIC(NumMerged, "Number of EXTend instructions removed");

namespace {

/// The most instructions one EXTend can cover.
static constexpr unsigned MaxRange = 4;

/// Whether \p Opc is an EXTend that takes its segment or page as an immediate.
/// The forms that take a register are left alone: the register could be written
/// between the two, so telling whether they say the same thing is a different
/// question from comparing operands.
static bool isImmExtend(unsigned Opc) {
  switch (Opc) {
  case C166::EXTSi:
  case C166::EXTSRi:
  case C166::EXTPi:
  case C166::EXTPRi:
    return true;
  default:
    return false;
  }
}

/// Whether widening an EXTend over \p MI would change what \p MI does.  An
/// instruction with no data access has nothing for the override to redirect;
/// anything else is left alone, including the cases where it is not obvious
/// what the instruction touches.
static bool isSafeToCover(const MachineInstr &MI) {
  if (MI.mayLoadOrStore() || MI.isCall() || MI.isBranch() || MI.isReturn())
    return false;
  if (MI.isInlineAsm() || MI.hasUnmodeledSideEffects())
    return false;
  // An EXTend in the gap would be saying something of its own.
  return !isImmExtend(MI.getOpcode());
}

class C166MergeExtend : public MachineFunctionPass {
public:
  static char ID;

  C166MergeExtend() : MachineFunctionPass(ID) {}

  bool runOnMachineFunction(MachineFunction &MF) override;

  StringRef getPassName() const override {
    return "C166 merge EXTend prefixes";
  }

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.setPreservesCFG();
    MachineFunctionPass::getAnalysisUsage(AU);
  }

private:
  bool tryMergeAt(MachineBasicBlock &MBB, MachineBasicBlock::iterator I);
};

} // end anonymous namespace

char C166MergeExtend::ID = 0;

/// The range operand is written the way it is spelled, 1 to 4.
static unsigned getRange(const MachineInstr &MI) {
  return MI.getOperand(1).getImm();
}

/// Whether two EXTend operands name the same segment or page.
///
/// Identical operands do, obviously.  So do two offsets into the same object:
/// "seg(g)" and "seg(g + 2)" are the same segment unless g straddles a segment
/// boundary, and it is the linker script's job to see that none does - c166.ld
/// asserts it of every section a far access can reach.  That is worth the rule
/// because the interesting case is exactly this one: a program reaches two
/// fields of a far structure or two halves of a far long far more often than it
/// reaches the same word twice.
static bool saysTheSameThing(const MachineOperand &A, const MachineOperand &B) {
  if (A.isGlobal() && B.isGlobal())
    return A.getGlobal() == B.getGlobal() &&
           A.getTargetFlags() == B.getTargetFlags();
  return A.isIdenticalTo(B);
}

/// The EXTend a far access bundle begins with, or null if \p MI is not one.
/// A far access reaches here already tied to its EXTend, because the pass that
/// expanded it bundled the two so that the post-RA scheduler could not put
/// anything between them.
static MachineInstr *getBundledExtend(MachineInstr &MI) {
  if (!MI.isBundle())
    return nullptr;
  auto First = std::next(MI.getIterator());
  if (First == MI.getParent()->instr_end() || !First->isInsideBundle())
    return nullptr;
  return isImmExtend(First->getOpcode()) ? &*First : nullptr;
}

/// How many instructions a bundle holds after its EXTend, which is what the
/// EXTend has to cover.
static unsigned countCovered(MachineInstr &Bundle) {
  unsigned N = 0;
  auto I = std::next(std::next(Bundle.getIterator()));
  for (; I != Bundle.getParent()->instr_end() && I->isInsideBundle(); ++I)
    if (!I->isDebugInstr())
      ++N;
  return N;
}

/// Take a bundle apart, leaving its instructions in place as ordinary ones.
static void dissolve(MachineInstr &Bundle) {
  MachineBasicBlock &MBB = *Bundle.getParent();
  auto I = std::next(Bundle.getIterator());
  while (I != MBB.instr_end() && I->isInsideBundle()) {
    MachineInstr &Inner = *I;
    ++I;
    Inner.unbundleFromPred();
  }
  Bundle.eraseFromParent();
}

bool C166MergeExtend::tryMergeAt(MachineBasicBlock &MBB,
                                 MachineBasicBlock::iterator I) {
  MachineInstr *ExtA = getBundledExtend(*I);
  if (!ExtA)
    return false;

  unsigned Total = countCovered(*I);
  if (Total >= MaxRange)
    return false;

  // Walk forward over instructions that have nothing for the override to
  // redirect, looking for the next far access.
  SmallVector<MachineInstr *, 4> Gap;
  auto J = std::next(I);
  while (J != MBB.end() && !getBundledExtend(*J)) {
    if (J->isDebugInstr()) {
      ++J;
      continue;
    }
    if (!isSafeToCover(*J) || Total + Gap.size() + 1 >= MaxRange)
      return false;
    Gap.push_back(&*J);
    ++J;
  }
  if (J == MBB.end())
    return false;

  MachineInstr *ExtB = getBundledExtend(*J);
  if (ExtB->getOpcode() != ExtA->getOpcode() ||
      !saysTheSameThing(ExtA->getOperand(0), ExtB->getOperand(0)))
    return false;

  Total += Gap.size() + countCovered(*J);
  if (Total > MaxRange)
    return false;

  LLVM_DEBUG(dbgs() << "merging " << *ExtB << "  into " << *ExtA);

  // Widen the first, drop the second, and tie the whole run back together so
  // that nothing can be scheduled into it any more than before.
  MachineInstr &BundleA = *I;
  MachineInstr &BundleB = *J;
  auto Last = std::prev(BundleB.getParent()->instr_end());
  for (auto K = std::next(BundleB.getIterator());
       K != MBB.instr_end() && K->isInsideBundle(); ++K)
    Last = K;

  ExtA->getOperand(1).setImm(Total);
  dissolve(BundleA);
  dissolve(BundleB);
  ExtB->eraseFromParent();

  finalizeBundle(MBB, ExtA->getIterator(), std::next(Last));
  ++NumMerged;
  return true;
}

bool C166MergeExtend::runOnMachineFunction(MachineFunction &MF) {
  bool Changed = false;
  for (MachineBasicBlock &MBB : MF)
    for (auto I = MBB.begin(); I != MBB.end();) {
      // On a merge the bundle at I has been replaced, so find it again rather
      // than holding an iterator across the surgery.
      if (tryMergeAt(MBB, I)) {
        Changed = true;
        I = MBB.begin();
        continue;
      }
      ++I;
    }
  return Changed;
}

INITIALIZE_PASS(C166MergeExtend, DEBUG_TYPE, "C166 merge EXTend prefixes", false,
                false)

FunctionPass *llvm::createC166MergeExtendPass() { return new C166MergeExtend(); }
