//===-- C166TargetTransformInfo.cpp - C166 specific TTI -------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "C166TargetTransformInfo.h"
#include "llvm/IR/Instructions.h"

using namespace llvm;

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
  const bool ConstOp2 = Op2Info.isConstant();
  const bool Pow2Op2 = ConstOp2 && Op2Info.isPowerOf2();

  switch (Opcode) {
  default:
    break;

  case Instruction::Mul:
    // One word is the hardware MUL and a MOV to read MDL back.  Wider is a
    // schoolbook product of the words, quadratic in them; against a constant
    // the partial products that are zero drop out, and a power of two leaves
    // a shift.  Expanded in place, so size and time agree.
    //
    // Measured at 32 bits: 10 against a variable, 7 against 13, 5 against 16.
    if (N == 1)
      return 2;
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
    // Three instructions, and the one number here that understates - DIV is
    // much longer than three instructions' worth of time - but it is the
    // only divide this machine has, so nothing is chosen against it.
    if (N == 1)
      return 3;

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

    // Asked about time, it is the builtin's own bit loop, which walks the
    // dividend a bit at a time: about four instructions per bit, plus the
    // call and the setup.  A signed one wraps the unsigned loop in taking
    // both signs off and putting one back.
    //
    // Measured on the compiler-rt builtins this target builds: __udivsi3 is
    // 143 instructions and __divsi3 is 31 on top of it.  The formula is what
    // the shape of a restoring division predicts, and those are what it
    // predicts for 32 bits.
    InstructionCost Cost = 4 * Bits + 16;
    if (Opcode == Instruction::SDiv || Opcode == Instruction::SRem)
      Cost += 32;
    return Cost;
  }
  }

  // Everything else - add, subtract, and the bitwise operations - is one
  // instruction per word, which is what the base class already says.
  return BaseT::getArithmeticInstrCost(Opcode, Ty, CostKind, Op1Info, Op2Info,
                                       Args, CxtI);
}
