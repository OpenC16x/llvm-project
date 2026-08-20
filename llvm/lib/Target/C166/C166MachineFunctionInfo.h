//===-- C166MachineFunctionInfo.h - C166 machine function info --*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares C166 specific per-machine-function information.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_C166_C166MACHINEFUNCTIONINFO_H
#define LLVM_LIB_TARGET_C166_C166MACHINEFUNCTIONINFO_H

#include "llvm/CodeGen/MachineFunction.h"

namespace llvm {

/// C166MachineFunctionInfo - This class is derived from MachineFunction and
/// contains private C166 target-specific information for each MachineFunction.
class C166MachineFunctionInfo : public MachineFunctionInfo {
  virtual void anchor();

  /// Size of the callee saved register portion of the stack frame in bytes.
  unsigned CalleeSavedFrameSize = 0;

  /// FrameIndex of the start of the varargs area.
  int VarArgsFrameIndex = 0;

  /// Virtual register holding the sret argument, if any.
  Register SRetReturnReg;

public:
  C166MachineFunctionInfo() = default;

  C166MachineFunctionInfo(const Function &F, const TargetSubtargetInfo *STI) {}

  MachineFunctionInfo *
  clone(BumpPtrAllocator &Allocator, MachineFunction &DestMF,
        const DenseMap<MachineBasicBlock *, MachineBasicBlock *> &Src2DstMBB)
      const override;

  unsigned getCalleeSavedFrameSize() const { return CalleeSavedFrameSize; }
  void setCalleeSavedFrameSize(unsigned Bytes) { CalleeSavedFrameSize = Bytes; }

  Register getSRetReturnReg() const { return SRetReturnReg; }
  void setSRetReturnReg(Register Reg) { SRetReturnReg = Reg; }

  int getVarArgsFrameIndex() const { return VarArgsFrameIndex; }
  void setVarArgsFrameIndex(int Index) { VarArgsFrameIndex = Index; }
};

} // end namespace llvm

#endif
