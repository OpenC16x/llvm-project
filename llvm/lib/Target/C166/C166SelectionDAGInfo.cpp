//===-- C166SelectionDAGInfo.cpp ------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "C166SelectionDAGInfo.h"

#define GET_SDNODE_DESC
#include "C166GenSDNodeInfo.inc"

using namespace llvm;

C166SelectionDAGInfo::C166SelectionDAGInfo()
    : SelectionDAGGenTargetInfo(C166GenSDNodeInfo) {}

C166SelectionDAGInfo::~C166SelectionDAGInfo() = default;
