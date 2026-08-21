//===-- C166ISelLowering.h - C166 DAG Lowering Interface --------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the interfaces that C166 uses to lower LLVM code into a
// selection DAG.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_C166_C166ISELLOWERING_H
#define LLVM_LIB_TARGET_C166_C166ISELLOWERING_H

#include "C166.h"
#include "llvm/CodeGen/SelectionDAG.h"
#include "llvm/CodeGen/TargetLowering.h"

namespace llvm {

class C166Subtarget;

class C166TargetLowering : public TargetLowering {
public:
  explicit C166TargetLowering(const TargetMachine &TM,
                              const C166Subtarget &STI);

  MVT::SimpleValueType getCmpLibcallReturnType() const override {
    return MVT::i16;
  }

  EVT getSetCCResultType(const DataLayout &DL, LLVMContext &Context,
                         EVT VT) const override {
    if (!VT.isVector())
      return MVT::i16;
    return VT.changeVectorElementTypeToInteger();
  }

  Register
  getExceptionPointerRegister(ExceptionHandling EH,
                              const Constant *PersonalityFn) const override {
    return C166::R2;
  }

  Register
  getExceptionSelectorRegister(ExceptionHandling EH,
                               const Constant *PersonalityFn) const override {
    return C166::R3;
  }

  SDValue LowerOperation(SDValue Op, SelectionDAG &DAG) const override;

  void ReplaceNodeResults(SDNode *N, SmallVectorImpl<SDValue> &Results,
                          SelectionDAG &DAG) const override;

  SDValue LowerGlobalAddress(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerBlockAddress(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerExternalSymbol(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerJumpTable(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerBR_CC(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerSETCC(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerSELECT_CC(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerFRAMEADDR(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerVASTART(SDValue Op, SelectionDAG &DAG) const;
  bool getPostIndexedAddressParts(SDNode *N, SDNode *Op, SDValue &Base,
                                  SDValue &Offset, ISD::MemIndexedMode &AM,
                                  SelectionDAG &DAG) const override;

  SDValue LowerLOAD(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerSTORE(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerADDRSPACECAST(SDValue Op, SelectionDAG &DAG) const;
  SDValue LowerFarSymbol(SDValue Op, SelectionDAG &DAG) const;

  ConstraintType getConstraintType(StringRef Constraint) const override;
  std::pair<unsigned, const TargetRegisterClass *>
  getRegForInlineAsmConstraint(const TargetRegisterInfo *TRI,
                               StringRef Constraint, MVT VT) const override;

  /// Truncating a word to a byte is free: the byte registers RLn/RHn are
  /// sub-registers of R0-R7.
  bool isTruncateFree(Type *Ty1, Type *Ty2) const override;
  bool isTruncateFree(EVT VT1, EVT VT2) const override;

  bool isLegalICmpImmediate(int64_t Imm) const override;
  bool isLegalAddImmediate(int64_t Imm) const override;

  MachineBasicBlock *
  EmitInstrWithCustomInserter(MachineInstr &MI,
                              MachineBasicBlock *BB) const override;

private:
  SDValue LowerCCCCallTo(TargetLowering::CallLoweringInfo &CLI,
                         SmallVectorImpl<SDValue> &InVals) const;

  SDValue LowerCCCArguments(SDValue Chain, CallingConv::ID CallConv,
                            bool IsVarArg,
                            const SmallVectorImpl<ISD::InputArg> &Ins,
                            const SDLoc &DL, SelectionDAG &DAG,
                            SmallVectorImpl<SDValue> &InVals) const;

  SDValue LowerCallResult(SDValue Chain, SDValue InGlue,
                          CallingConv::ID CallConv, bool IsVarArg,
                          const SmallVectorImpl<ISD::InputArg> &Ins,
                          const SDLoc &DL, SelectionDAG &DAG,
                          SmallVectorImpl<SDValue> &InVals) const;

  SDValue
  LowerFormalArguments(SDValue Chain, CallingConv::ID CallConv, bool IsVarArg,
                       const SmallVectorImpl<ISD::InputArg> &Ins,
                       const SDLoc &DL, SelectionDAG &DAG,
                       SmallVectorImpl<SDValue> &InVals) const override;

  bool isEligibleForTailCall(const TargetLowering::CallLoweringInfo &CLI,
                             const SmallVectorImpl<CCValAssign> &ArgLocs,
                             unsigned StackSize) const;

  SDValue LowerCall(TargetLowering::CallLoweringInfo &CLI,
                    SmallVectorImpl<SDValue> &InVals) const override;

  bool CanLowerReturn(CallingConv::ID CallConv, MachineFunction &MF,
                      bool IsVarArg,
                      const SmallVectorImpl<ISD::OutputArg> &Outs,
                      LLVMContext &Context, const Type *RetTy) const override;

  SDValue LowerReturn(SDValue Chain, CallingConv::ID CallConv, bool IsVarArg,
                      const SmallVectorImpl<ISD::OutputArg> &Outs,
                      const SmallVectorImpl<SDValue> &OutVals, const SDLoc &DL,
                      SelectionDAG &DAG) const override;
};

} // end namespace llvm

#endif
