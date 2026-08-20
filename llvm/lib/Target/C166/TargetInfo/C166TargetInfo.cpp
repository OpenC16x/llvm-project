//===-- C166TargetInfo.cpp - C166 Target Implementation -------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "TargetInfo/C166TargetInfo.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/Compiler.h"
using namespace llvm;

Target &llvm::getTheC166Target() {
  static Target TheC166Target;
  return TheC166Target;
}

extern "C" LLVM_ABI LLVM_EXTERNAL_VISIBILITY void
LLVMInitializeC166TargetInfo() {
  RegisterTarget<Triple::c166> X(getTheC166Target(), "c166",
                                 "Infineon C166 [experimental]", "C166");
}
