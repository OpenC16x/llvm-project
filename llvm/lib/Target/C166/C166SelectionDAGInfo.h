//===-- C166SelectionDAGInfo.h ----------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_C166_C166SELECTIONDAGINFO_H
#define LLVM_LIB_TARGET_C166_C166SELECTIONDAGINFO_H

#include "llvm/CodeGen/SelectionDAGTargetInfo.h"

#define GET_SDNODE_ENUM
#include "C166GenSDNodeInfo.inc"

namespace llvm {

class C166SelectionDAGInfo : public SelectionDAGGenTargetInfo {
public:
  C166SelectionDAGInfo();

  ~C166SelectionDAGInfo() override;
};

} // namespace llvm

#endif // LLVM_LIB_TARGET_C166_C166SELECTIONDAGINFO_H
