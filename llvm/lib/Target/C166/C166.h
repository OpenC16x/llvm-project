//===-- C166.h - Top-level interface for C166 representation ----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains the entry points for global functions defined in the
// LLVM C166 backend.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_C166_C166_H
#define LLVM_LIB_TARGET_C166_C166_H

#include "MCTargetDesc/C166MCTargetDesc.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/Target/TargetMachine.h"

namespace llvm {

namespace C166AS {
/// C166 address spaces.  The default one holds the 16 bit near pointers the
/// hardware addresses directly; address space 1 holds far pointers, a linear
/// 24 bit address zero extended into 32 bits, which are accessed by naming
/// their segment in an EXTS ahead of the access.
///
/// Address space 2 is a far pointer that has been promised to stay inside one
/// segment.  It is the same 32 bits and is accessed the same way, so an access
/// through one is a far access and nothing below instruction selection tells
/// them apart; what differs is that its arithmetic touches only the low
/// sixteen bits, which the data layout says by giving it an index size of
/// sixteen.  C166LowerSegPointers.cpp has the rest of the reasoning.
enum : unsigned {
  Near = 0,
  Far = 1,
  Seg = 2,
};

/// Whether an access through this address space is a far access, which is to
/// say one that names its segment in an EXTS.
inline bool isFar(unsigned AS) { return AS == Far || AS == Seg; }
} // end namespace C166AS

namespace C166II {
/// Target flags on a symbol operand, naming which field of the symbol's 24 bit
/// address the operand wants.  These become the seg() and sof() operators the
/// assembler understands, and the relocations behind them.
enum TOF : unsigned {
  MO_None = 0,
  MO_SEG, ///< Bits 23-16 of the address, the segment.
  MO_SOF, ///< Bits 15-0 of the address, the offset within that segment.
};
} // end namespace C166II

namespace C166MAC {
/// Which of the coprocessor's multiply-accumulates a MAC32rr stands for.
///
/// The four differ only in how the product is formed and whether it is added
/// or taken away, so they share one pseudo and one expansion; this says which
/// CoXXX comes out of it.  The accumulator is loaded and stored the same way
/// for all four, because CoLOAD sign extends into a forty bit accumulator that
/// is truncated back to thirty two on the way out, and 2^32 divides 2^40 - so
/// what the top eight bits hold never reaches the answer.
enum Kind : unsigned {
  Signed = 0,          ///< CoMAC:   ACC += (signed)a * (signed)b
  Unsigned = 1,        ///< CoMACu:  ACC += (unsigned)a * (unsigned)b
  SignedNegate = 2,    ///< CoMAC-:  ACC -= (signed)a * (signed)b
  UnsignedNegate = 3,  ///< CoMACu-: ACC -= (unsigned)a * (unsigned)b
};

/// Which of the coprocessor's two 40 bit comparisons a MINMAX32rr stands for.
///
/// Both take the larger or smaller of the accumulator and a 40 bit operand
/// built from two registers and sign extended, so a 32 bit signed minimum or
/// maximum is CoLOAD, one of these, and the two words back out.  There is no
/// unsigned pair: the comparison is signed and nothing makes it not be.
enum MinMax : unsigned {
  Max = 0, ///< CoMAX: ACC = max(ACC, operand)
  Min = 1, ///< CoMIN: ACC = min(ACC, operand)
};
} // end namespace C166MAC

namespace C166CC {
/// C166 condition codes.  These are the values encoded in the 'cc' field of
/// the conditional jump and call instructions.
enum CondCode {
  COND_UC = 0x0,  // unconditional
  COND_NET = 0x1, // not end of table
  COND_Z = 0x2,   // zero / equal
  COND_NZ = 0x3,  // not zero / not equal
  COND_V = 0x4,   // overflow
  COND_NV = 0x5,  // no overflow
  COND_N = 0x6,   // negative
  COND_NN = 0x7,  // not negative
  COND_ULT = 0x8, // unsigned lower (carry set)
  COND_UGE = 0x9, // unsigned greater or equal (carry clear)
  COND_SGT = 0xA, // signed greater
  COND_SLE = 0xB, // signed less or equal
  COND_SLT = 0xC, // signed less
  COND_SGE = 0xD, // signed greater or equal
  COND_UGT = 0xE, // unsigned greater
  COND_ULE = 0xF, // unsigned lower or equal

  COND_INVALID = -1
};

/// Return the condition code that is true exactly when \p CC is false.
inline CondCode getOppositeCondition(CondCode CC) {
  switch (CC) {
  default:
    return COND_INVALID;
  case COND_Z:
    return COND_NZ;
  case COND_NZ:
    return COND_Z;
  case COND_V:
    return COND_NV;
  case COND_NV:
    return COND_V;
  case COND_N:
    return COND_NN;
  case COND_NN:
    return COND_N;
  case COND_ULT:
    return COND_UGE;
  case COND_UGE:
    return COND_ULT;
  case COND_UGT:
    return COND_ULE;
  case COND_ULE:
    return COND_UGT;
  case COND_SGT:
    return COND_SLE;
  case COND_SLE:
    return COND_SGT;
  case COND_SLT:
    return COND_SGE;
  case COND_SGE:
    return COND_SLT;
  }
}

/// Return the assembler mnemonic for a condition code.
inline const char *getCondCodeName(CondCode CC) {
  switch (CC) {
  case COND_UC:
    return "cc_UC";
  case COND_NET:
    return "cc_NET";
  case COND_Z:
    return "cc_EQ";
  case COND_NZ:
    return "cc_NE";
  case COND_V:
    return "cc_V";
  case COND_NV:
    return "cc_NV";
  case COND_N:
    return "cc_N";
  case COND_NN:
    return "cc_NN";
  case COND_ULT:
    return "cc_ULT";
  case COND_UGE:
    return "cc_UGE";
  case COND_SGT:
    return "cc_SGT";
  case COND_SLE:
    return "cc_SLE";
  case COND_SLT:
    return "cc_SLT";
  case COND_SGE:
    return "cc_SGE";
  case COND_UGT:
    return "cc_UGT";
  case COND_ULE:
    return "cc_ULE";
  default:
    return nullptr;
  }
}

/// The other name of each encoding that has two.
///
/// The architecture spells four of these both ways - "cc_z" and "cc_EQ" are the
/// same condition - and getCondCodeName above returns only one of each pair.
/// The assembler accepts either, so anything that has to know every spelling
/// there is, such as the set of words a symbol may not be called, has to read
/// both lists.
inline ArrayRef<std::pair<const char *, CondCode>> getCondCodeAliases() {
  static const std::pair<const char *, CondCode> Aliases[] = {
      {"cc_z", COND_Z},
      {"cc_nz", COND_NZ},
      {"cc_c", COND_ULT},
      {"cc_nc", COND_UGE},
  };
  return Aliases;
}
} // end namespace C166CC

class C166TargetMachine;
class FunctionPass;
class Loop;
class ModulePass;
class PassRegistry;
class ScalarEvolution;

FunctionPass *createC166ISelDag(C166TargetMachine &TM,
                                CodeGenOptLevel OptLevel);

/// Fold an EXTend prefix into the one before it where the two say the same
/// thing and nothing between them touches memory.
FunctionPass *createC166MergeExtendPass();
void initializeC166MergeExtendPass(PassRegistry &);
FunctionPass *createC166FoldComparePass();
void initializeC166FoldComparePass(PassRegistry &);

FunctionPass *createC166MACChainPass();
void initializeC166MACChainPass(PassRegistry &);

FunctionPass *createC166MACRepeatPass();
void initializeC166MACRepeatPass(PassRegistry &);

FunctionPass *createC166LowerSegPointersPass();
void initializeC166LowerSegPointersPass(PassRegistry &);

namespace C166 {
/// True where this loop is a dot product one repeated coprocessor instruction
/// does in a single go, which C166MACRepeat is about to make it.
///
/// It is asked twice: once by that pass, and once by the unrolling preferences,
/// because a loop that is about to become one instruction must not be unrolled
/// into forty first.  C166MACRepeat.cpp holds the reasoning and both answers.
bool isRepeatedCoMACLoop(Loop *L, ScalarEvolution &SE);
} // end namespace C166

/// Take the thread-local marker off every global that has one.  This part runs
/// one thread, so per-thread storage and static storage are the same storage.
ModulePass *createC166LowerThreadLocalPass();
void initializeC166LowerThreadLocalPass(PassRegistry &);

void initializeC166AsmPrinterPass(PassRegistry &);
void initializeC166DAGToDAGISelLegacyPass(PassRegistry &);

} // end namespace llvm

#endif // LLVM_LIB_TARGET_C166_C166_H
