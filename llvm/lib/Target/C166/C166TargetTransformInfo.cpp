//===-- C166TargetTransformInfo.cpp - C166 specific TTI -------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "C166TargetTransformInfo.h"
#include "C166.h"
#include "llvm/IR/Instructions.h"

using namespace llvm;

void C166TTIImpl::getUnrollingPreferences(Loop *L, ScalarEvolution &SE,
                                          TTI::UnrollingPreferences &UP,
                                          OptimizationRemarkEmitter *ORE) const {
  BaseT::getUnrollingPreferences(L, SE, UP, ORE);

  if (!ST->hasMAC() || !C166::isRepeatedCoMACLoop(L, SE))
    return;

  // Every way a loop can be unrolled, refused.  A trip count above
  // FullUnrollMaxCount is what stops the full decision - that one is a count
  // and not a cost, so a threshold alone would not settle it - and the rest
  // stop the partial and runtime ones.
  UP.MaxCount = 1;
  UP.MaxUpperBound = 1;
  UP.FullUnrollMaxCount = 1;
  UP.Threshold = 0;
  UP.OptSizeThreshold = 0;
  UP.PartialThreshold = 0;
  UP.PartialOptSizeThreshold = 0;
  UP.Partial = false;
  UP.Runtime = false;
  UP.UpperBound = false;
  UP.Force = false;
}

void C166TTIImpl::getPeelingPreferences(Loop *L, ScalarEvolution &SE,
                                        TTI::PeelingPreferences &PP) const {
  BaseT::getPeelingPreferences(L, SE, PP);

  // Peeling is decided before unrolling and would take the first product out
  // of the loop, which leaves a run of one fewer and a stray MAC beside it.
  if (ST->hasMAC() && C166::isRepeatedCoMACLoop(L, SE)) {
    PP.PeelCount = 0;
    PP.AllowPeeling = false;
  }
}

/// The number of 16 bit registers a value of this many bits occupies.  The
/// machine has nothing narrower than a word to compute in - a byte operation
/// that the hardware does not have is done in a word - so a byte counts as
/// one and not as a half.
static unsigned wordsFor(unsigned Bits) {
  return std::max(1u, (Bits + 15) / 16);
}

InstructionCost C166TTIImpl::getIntImmCost(const APInt &Imm, Type *Ty,
                                           TTI::TargetCostKind CostKind) const {
  assert(Ty->isIntegerTy());

  unsigned BitSize = Ty->getPrimitiveSizeInBits();
  // A constant of no width is not a constant anyone materialises.
  if (BitSize == 0)
    return TTI::TCC_Free;

  // Zero is reachable without materialising anything in the places it usually
  // turns up: a comparison against zero reads flags the previous instruction
  // already set, and the patterns fold it.
  if (Imm == 0)
    return TTI::TCC_Free;

  // Every 16 bit constant is one MOV.  Which MOV depends on the value - "MOV
  // Rw, #data4" is two bytes for 0 to 15 and "MOV Rw, #data16" is four for the
  // rest - but that is a difference in size and not in instruction count, and
  // this cost is a count.  A wider constant is one MOV per word.
  return wordsFor(BitSize) * TTI::TCC_Basic;
}

InstructionCost C166TTIImpl::getArithmeticInstrCost(
    unsigned Opcode, Type *Ty, TTI::TargetCostKind CostKind,
    TTI::OperandValueInfo Op1Info, TTI::OperandValueInfo Op2Info,
    ArrayRef<const Value *> Args, const Instruction *CxtI) const {
  // There are no vector registers, so a vector type here is something the
  // caller will have to scalarise.  The base class knows how to price that.
  if (!Ty->isIntegerTy())
    return BaseT::getArithmeticInstrCost(Opcode, Ty, CostKind, Op1Info, Op2Info,
                                         Args, CxtI);

  const unsigned Bits = Ty->getPrimitiveSizeInBits();
  const unsigned N = wordsFor(Bits);

  // Size and time part company here, so the cost kind has to be honoured
  // rather than answered with one number.  A 32 bit divide runs a bit loop
  // of about 140 instructions, but it emits a call: the loop is in a builtin
  // and is shared, not copied to the call site.  Answering the running cost
  // to a caller asking about size makes a loop body look far bigger than the
  // code it becomes, and the loop unroller - which budgets in TCK_CodeSize -
  // then declines to unroll loops it should.  Measured, that was four loops
  // in language.c that stopped being unrolled and +17% code for it.
  const bool WantSize =
      CostKind == TTI::TCK_CodeSize || CostKind == TTI::TCK_SizeAndLatency;

  // The costs below are for the whole operation, so they are returned rather
  // than handed to the base class, which would price the operation at its
  // legalised type and multiply.  That is exactly what goes wrong without
  // this: a 32 bit divide legalises to two 16 bit ones, SDIV is legal at 16
  // bits, and so the default answer is two - the same as a 32 bit add, for
  // something that is a call to a bit loop.
  //
  // Whether the second operand is a constant changes the answer completely
  // for both of the operations below, so it is read rather than assumed.  A
  // wide divide by a variable is a call to a bit loop; a wide divide by a
  // constant is not a call at all - the combiner turns it into a multiply and
  // a shift - and pricing the second as the first is the largest single error
  // this file could make.
  const bool IsSigned =
      Opcode == Instruction::SDiv || Opcode == Instruction::SRem;
  const bool ConstOp2 = Op2Info.isConstant();
  const bool Pow2Op2 = ConstOp2 && Op2Info.isPowerOf2();

  switch (Opcode) {
  default:
    break;

  case Instruction::Mul:
    // One word is the hardware MUL and a MOV to read MDL back.  Wider is a
    // schoolbook product of the words, quadratic in them; against a constant
    // the partial products that are zero drop out, and a power of two leaves
    // a shift.  Expanded in place, so size and time agree - except at one
    // word, where they do not: MUL is ten states where an ordinary
    // instruction is two, so those two instructions are worth six.
    //
    // Measured at 32 bits: 10 against a variable, 7 against 13, 5 against 16.
    if (N == 1)
      return WantSize ? 2 : 6;
    if (Pow2Op2)
      return 2 * N + 1;
    if (ConstOp2)
      return 2 * N * N - 1;
    return 2 * N * N + 2;

  case Instruction::Shl:
  case Instruction::LShr:
  case Instruction::AShr:
    // One word is a single shift.  Wider is a carry chain across the words,
    // about four instructions each, plus the count arithmetic.
    return N == 1 ? 1 : 4 * N + 1;

  case Instruction::UDiv:
  case Instruction::URem:
  case Instruction::SDiv:
  case Instruction::SRem: {
    // One word is the hardware DIV: set up MDL, divide, read the answer back.
    // Three instructions of code and twenty four states of time, because DIV
    // is twenty of them - twelve ordinary instructions' worth.  This used to
    // answer three to both questions and say so as the one number here that
    // understated; the scheduling model is where the twenty now comes from.
    if (N == 1)
      return WantSize ? 3 : 12;

    // Wider than a word against a constant divisor never reaches a builtin:
    // it is a multiply by the reciprocal and a shift, or just a shift when
    // the divisor is a power of two.  Measured at 32 bits: 8 against 13, 5
    // against 16.
    if (Pow2Op2)
      return 2 * N + 1;
    if (ConstOp2)
      return 2 * N * N;

    // Against a variable it is a call.  What that costs depends on what is
    // being asked: at the call site it is the arguments, the CALLA and the
    // result, and that is all the code there is here.
    if (WantSize)
      return 6;

    // Asked about time, it is whatever the builtin does.  At 32 bits that is
    // this target's own helper, which sends a divisor that fits in a word to
    // the divide unit and only falls back to the loop for a wider one; see
    // compiler-rt/lib/builtins/c166/int_divmod.h.  Measured in the simulator,
    // a division by a word divisor is 120 states and a signed one is 188, so
    // the numbers below are the measurements rather than a formula.
    //
    // In instructions executed those two are 32 and 63, which is what this
    // used to answer.  The instruction counts were right and the unit was
    // not: a helper with a DIV in it does not cost two states an instruction
    // the way ordinary code does, and pricing it as though it did understated
    // it by nearly a half.
    //
    // They are the common case and not the worst one: a divisor that does not
    // fit in a word still walks the loop, at about 740 instructions.  The
    // model takes the case the helper is built for, because that is the one
    // ordinary code divides by - and because overstating a division is not
    // free either, being what stops loops containing one from unrolling.
    if (Bits == 32)
      return IsSigned ? 94 : 60;

    // Wider than that has no helper and is the generic loop, which walks the
    // dividend a bit at a time: roughly two dozen instructions per bit once
    // the 32 bit shifts inside it are counted.  There is no DIV anywhere in
    // it, so every instruction is an ordinary two states and the count is
    // already in the unit the rest of this file uses.  Note this is what it
    // executes, which is not what it occupies - __udivsi3 is 140 instructions
    // of code and runs about 740 of them.
    InstructionCost Cost = 24 * Bits;
    if (IsSigned)
      Cost += 32;
    return Cost;
  }
  }

  // Everything else - add, subtract, and the bitwise operations - is one
  // instruction per word, which is what the base class already says.
  return BaseT::getArithmeticInstrCost(Opcode, Ty, CostKind, Op1Info, Op2Info,
                                       Args, CxtI);
}
