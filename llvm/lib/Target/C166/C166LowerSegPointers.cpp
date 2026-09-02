//===-- C166LowerSegPointers.cpp - 16 bit arithmetic on a far pointer -----===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Address space 2 is a far pointer whose arithmetic stays inside one segment.
// It has the same representation as address space 1 - a linear 24 bit address
// zero extended into 32 bits - so the two convert into each other for nothing
// and either can be stored, passed and returned; what differs is only what
// adding to one means.  Adding to a far pointer is a 32 bit add and a carry out
// of the offset lands in the segment.  Adding to one of these touches the low
// sixteen bits and leaves the segment alone.
//
// That is not a licence this pass takes: it is what the data layout already
// says.  Address space 2 is declared with an index size of sixteen, and the
// language reference is explicit about what that means - "the offsets are then
// added to the low bits of the base address up to the index type width, with
// silently-wrapping two's complement arithmetic ... the bits outside the index
// type width will not be affected".  So every getelementptr in this address
// space already has these semantics, and every pass that reasons about one -
// scalar evolution, the induction variable rewriters, the loop strength
// reducer - already reasons about it in sixteen bits.
//
// What is missing is the last step.  SelectionDAGBuilder lowers a
// getelementptr by widening the offset to the pointer's own type and adding
// there, which is the same answer whenever the add does not carry and is not
// the one the language reference asks for when it does.  More to the point
// here, it puts a 32 bit add in the loop, and a 32 bit add is two instructions
// on this machine and keeps the segment half of the pointer live as something
// that is written rather than something that is read.
//
// So this rewrites the arithmetic into what the machine should do with it,
// while it is still IR and the pointer is still one value:
//
//   %q = getelementptr T, ptr addrspace(2) %p, i16 %i
// becomes
//   %pi  = ptrtoint ptr addrspace(2) %p to i32
//   %lo  = trunc i32 %pi to i16
//   %sum = add i16 %lo, <the offset, computed in i16>
//   %hi  = and i32 %pi, -65536
//   %new = or disjoint i32 %hi, (zext i16 %sum to i32)
//   %q   = inttoptr i32 %new to ptr addrspace(2)
//
// The type does not change, so nothing downstream has to know this ran: an
// access through one of these is selected exactly the way a far access is,
// because that is what it is.  What changed is that the segment half of the
// pointer now passes through the loop untouched, so the two i16 registers an
// i32 becomes after type legalisation are one that is stepped and one that is
// only read - and a value that is only read does not need an instruction.
//
// This runs in addIRPasses rather than in the optimisation pipeline, and
// deliberately late.  A getelementptr is what alias analysis, scalar evolution
// and every loop pass are written to understand, and the form above is an
// inttoptr they would all give up on.  Running here means the optimiser sees
// the pointer the whole way through and only the code generator sees this.
//
// A constant expression is left alone.  Its offset is a constant added to an
// address the linker picks, so there is nothing to compute at run time and
// nothing to put in a loop; whether it stays inside the segment is the same
// question as whether the object does, which the linker script already asks -
// llvm/lib/Target/C166/startup/c166.ld asserts that no region it lays out
// crosses a segment boundary.
//
//===----------------------------------------------------------------------===//

#include "C166.h"
#include "llvm/Analysis/Utils/Local.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/Instructions.h"
#include "llvm/InitializePasses.h"
#include "llvm/Pass.h"

using namespace llvm;

#define DEBUG_TYPE "c166-lower-seg-pointers"

namespace {

class C166LowerSegPointers : public FunctionPass {
public:
  static char ID;

  C166LowerSegPointers() : FunctionPass(ID) {}

  bool runOnFunction(Function &F) override;

  StringRef getPassName() const override {
    return "C166 segment-confined pointer arithmetic";
  }

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.setPreservesCFG();
  }
};

} // end anonymous namespace

char C166LowerSegPointers::ID = 0;

INITIALIZE_PASS(C166LowerSegPointers, DEBUG_TYPE,
                "C166 segment-confined pointer arithmetic", false, false)

FunctionPass *llvm::createC166LowerSegPointersPass() {
  return new C166LowerSegPointers();
}

bool C166LowerSegPointers::runOnFunction(Function &F) {
  SmallVector<GetElementPtrInst *, 8> Work;
  for (Instruction &I : instructions(F))
    if (auto *GEP = dyn_cast<GetElementPtrInst>(&I))
      if (GEP->getPointerAddressSpace() == C166AS::Seg)
        Work.push_back(GEP);

  if (Work.empty())
    return false;

  const DataLayout &DL = F.getDataLayout();
  Type *I16 = Type::getInt16Ty(F.getContext());
  Type *I32 = Type::getInt32Ty(F.getContext());

  for (GetElementPtrInst *GEP : Work) {
    IRBuilder<> B(GEP);

    // The offset in the index type, which the data layout makes sixteen bits
    // wide for this address space, so this is already the arithmetic the
    // result is defined to do.
    Value *Offset = emitGEPOffset(&B, DL, GEP);

    Value *Base = B.CreatePtrToInt(GEP->getPointerOperand(), I32);
    Value *Lo = B.CreateTrunc(Base, I16);
    Value *Sum = B.CreateAdd(Lo, Offset);
    // Keep bits 31-16 and put the new offset back under them.  The two halves
    // do not overlap, which is what "disjoint" says and what lets the or be
    // folded away once the pointer is split into its halves for the access.
    Value *Hi = B.CreateAnd(Base, ConstantInt::get(I32, 0xFFFF0000u));
    Value *New = B.CreateOr(Hi, B.CreateZExt(Sum, I32), "", /*IsDisjoint=*/true);

    Value *Ptr = B.CreateIntToPtr(New, GEP->getType());
    Ptr->takeName(GEP);
    GEP->replaceAllUsesWith(Ptr);
    GEP->eraseFromParent();
  }

  return true;
}
