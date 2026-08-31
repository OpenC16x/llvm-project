//===-- C166TargetTransformInfo.h - C166 specific TTI -----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// What the IR level passes are told about this machine.  Without it they get
// the target independent defaults, which describe something this is not: a
// machine with 8 registers of 32 bits, vector registers, and a 32 bit divide
// that costs the same as a 32 bit add.
//
// The numbers below are instruction counts, measured by compiling the
// operation and counting what came out; C166.td declares NoItineraries, so
// there is no verified cycle data in this tree to anchor anything finer, and
// an invented cycle count would be worse than a measured instruction count.
// The core is in-order with nothing worth calling a pipeline, so the count is
// a sound first order proxy for time as well as for space.  The one place it
// is known to understate is the hardware DIV, whose latency is a good deal
// longer than the three instructions it takes to set up and read back.
//
// llvm/test/Analysis/CostModel/C166 records the numbers and how to redo them.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_C166_C166TARGETTRANSFORMINFO_H
#define LLVM_LIB_TARGET_C166_C166TARGETTRANSFORMINFO_H

#include "C166Subtarget.h"
#include "C166TargetMachine.h"
#include "llvm/Analysis/TargetTransformInfo.h"
#include "llvm/CodeGen/BasicTTIImpl.h"
#include "llvm/IR/Function.h"

namespace llvm {

class C166TTIImpl final : public BasicTTIImplBase<C166TTIImpl> {
  using BaseT = BasicTTIImplBase<C166TTIImpl>;
  using TTI = TargetTransformInfo;

  friend BaseT;

  const C166Subtarget *ST;
  const C166TargetLowering *TLI;

  const C166Subtarget *getST() const { return ST; }
  const C166TargetLowering *getTLI() const { return TLI; }

public:
  explicit C166TTIImpl(const C166TargetMachine *TM, const Function &F)
      : BaseT(TM, F.getDataLayout()), ST(TM->getSubtargetImpl(F)),
        TLI(ST->getTargetLowering()) {}

  /// R2 to R15 are allocatable.  R0 is the ABI stack pointer and is always
  /// reserved; R1 is the frame pointer in a function that needs one, and this
  /// answer has to hold for every function, so it counts fourteen rather than
  /// fifteen.  Understating is the safe direction: the number is a pressure
  /// threshold, and a pass that thinks it has fewer registers spills sooner
  /// rather than producing something the allocator cannot place.
  ///
  /// There are no vector registers, and saying so is not decoration: the loop
  /// vectoriser, VectorCombine and the SLP vectoriser each return early when
  /// this is zero for the vector class, so it is what stops them running at
  /// all on a target that could never use their results.
  unsigned getNumberOfRegisters(unsigned ClassID) const override {
    return ClassID == 0 ? 14 : 0;
  }

  TypeSize
  getRegisterBitWidth(TargetTransformInfo::RegisterKind K) const override {
    if (K == TargetTransformInfo::RGK_Scalar)
      return TypeSize::getFixed(16);
    // No vectors of any kind.
    return TypeSize::getFixed(0);
  }

  /// Nothing here is worth interleaving: there is one of everything and no
  /// pipeline to fill.  This is also the second half of the vectoriser's
  /// early exit, which only fires when the interleave factor is below two.
  unsigned getMaxInterleaveFactor(ElementCount VF,
                                  bool HasUnorderedReductions) const override {
    return 1;
  }

  /// What it costs to materialise a constant into a register.
  ///
  /// getIntImmCostInst and getIntImmCostIntrin, which say what a constant
  /// costs in the place it is used, are deliberately not overridden: the base
  /// class answers TCC_Free and on this machine that is right.  Every ALU
  /// instruction here has a #data16 form, a compare has one, a shift count is
  /// #data4, and a load or store encodes its address - so a constant operand
  /// is part of the instruction rather than something put in a register.  It
  /// costs two bytes of code and never an instruction or a register.
  ///
  /// Saying otherwise is not a harmless overestimate.  Constant hoisting is
  /// inert while the in-place cost is free, and answering anything else wakes
  /// it up: measured, it then hoists far pointer addresses that a store
  /// already encodes and shift counts that are #data4, for +36% code at -O2.
  /// See the commit that added this file.
  InstructionCost getIntImmCost(const APInt &Imm, Type *Ty,
                                TTI::TargetCostKind CostKind) const override;

  InstructionCost getArithmeticInstrCost(
      unsigned Opcode, Type *Ty, TTI::TargetCostKind CostKind,
      TTI::OperandValueInfo Op1Info = {TTI::OK_AnyValue, TTI::OP_None},
      TTI::OperandValueInfo Op2Info = {TTI::OK_AnyValue, TTI::OP_None},
      ArrayRef<const Value *> Args = {},
      const Instruction *CxtI = nullptr) const override;
};

} // end namespace llvm

#endif // LLVM_LIB_TARGET_C166_C166TARGETTRANSFORMINFO_H
