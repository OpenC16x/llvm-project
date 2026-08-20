//===-- C166Subtarget.cpp - C166 Subtarget Information --------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the C166 specific subclass of TargetSubtargetInfo.
//
//===----------------------------------------------------------------------===//

#include "C166Subtarget.h"
#include "C166.h"
#include "C166SelectionDAGInfo.h"
#include "llvm/MC/TargetRegistry.h"

using namespace llvm;

#define DEBUG_TYPE "c166-subtarget"

#define GET_SUBTARGETINFO_TARGET_DESC
#define GET_SUBTARGETINFO_CTOR
#include "C166GenSubtargetInfo.inc"

void C166Subtarget::anchor() {}

C166Subtarget &C166Subtarget::initializeSubtargetDependencies(StringRef CPU,
                                                              StringRef FS) {
  StringRef CPUName = CPU;
  if (CPUName.empty())
    CPUName = "c166";

  ParseSubtargetFeatures(CPUName, /*TuneCPU=*/CPUName, FS);
  return *this;
}

C166Subtarget::C166Subtarget(const Triple &TT, const std::string &CPU,
                             const std::string &FS, const TargetMachine &TM)
    : C166GenSubtargetInfo(TT, CPU, /*TuneCPU=*/CPU, FS),
      InstrInfo(initializeSubtargetDependencies(CPU, FS)), TLInfo(TM, *this),
      FrameLowering(*this) {
  TSInfo = std::make_unique<C166SelectionDAGInfo>();
}

C166Subtarget::~C166Subtarget() = default;

const SelectionDAGTargetInfo *C166Subtarget::getSelectionDAGInfo() const {
  return TSInfo.get();
}
