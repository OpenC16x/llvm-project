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
#include "llvm/Target/TargetMachine.h"

namespace llvm {

namespace C166AS {
/// C166 address spaces.  The default one holds the 16 bit near pointers the
/// hardware addresses directly; address space 1 holds far pointers, a linear
/// 24 bit address zero extended into 32 bits, which are accessed by naming
/// their segment in an EXTS ahead of the access.
enum : unsigned {
  Near = 0,
  Far = 1,
};
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
} // end namespace C166CC

class C166TargetMachine;
class FunctionPass;
class PassRegistry;

FunctionPass *createC166ISelDag(C166TargetMachine &TM,
                                CodeGenOptLevel OptLevel);

/// Fold an EXTend prefix into the one before it where the two say the same
/// thing and nothing between them touches memory.
FunctionPass *createC166MergeExtendPass();
void initializeC166MergeExtendPass(PassRegistry &);

void initializeC166AsmPrinterPass(PassRegistry &);
void initializeC166DAGToDAGISelLegacyPass(PassRegistry &);

} // end namespace llvm

#endif // LLVM_LIB_TARGET_C166_C166_H
