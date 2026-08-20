//===-- C166MCTargetDesc.cpp - C166 Target Descriptions -------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file provides C166 specific target descriptions.
//
//===----------------------------------------------------------------------===//

#include "C166MCTargetDesc.h"
#include "C166InstPrinter.h"
#include "C166MCAsmInfo.h"
#include "TargetInfo/C166TargetInfo.h"
#include "llvm/MC/MCDwarf.h"
#include "llvm/MC/MCInstrInfo.h"
#include "llvm/MC/MCRegisterInfo.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/Compiler.h"

using namespace llvm;

#define GET_INSTRINFO_MC_DESC
#define ENABLE_INSTR_PREDICATE_VERIFIER
#include "C166GenInstrInfo.inc"

#define GET_SUBTARGETINFO_MC_DESC
#include "C166GenSubtargetInfo.inc"

#define GET_REGINFO_MC_DESC
#include "C166GenRegisterInfo.inc"

static MCInstrInfo *createC166MCInstrInfo() {
  MCInstrInfo *X = new MCInstrInfo();
  InitC166MCInstrInfo(X);
  return X;
}

static MCRegisterInfo *createC166MCRegisterInfo(const Triple &TT) {
  MCRegisterInfo *X = new MCRegisterInfo();
  InitC166MCRegisterInfo(X, C166::R0);
  return X;
}

static MCAsmInfo *createC166MCAsmInfo(const MCRegisterInfo &MRI,
                                      const Triple &TT,
                                      const MCTargetOptions &Options) {
  MCAsmInfo *MAI = new C166MCAsmInfo(TT, Options);

  // The ABI stack lives in R0 and grows down.  On function entry it points
  // straight at the first stack argument: the return address is kept on the
  // separate hardware stack and is not part of this frame.
  MCCFIInstruction Inst =
      MCCFIInstruction::cfiDefCfa(nullptr, MRI.getDwarfRegNum(C166::R0, true), 0);
  MAI->addInitialFrameState(Inst);

  return MAI;
}

static MCSubtargetInfo *createC166MCSubtargetInfo(const Triple &TT,
                                                  StringRef CPU, StringRef FS) {
  return createC166MCSubtargetInfoImpl(TT, CPU, /*TuneCPU=*/CPU, FS);
}

static MCInstPrinter *createC166MCInstPrinter(const Triple &T,
                                              unsigned SyntaxVariant,
                                              const MCAsmInfo &MAI,
                                              const MCInstrInfo &MII,
                                              const MCRegisterInfo &MRI) {
  if (SyntaxVariant == 0)
    return new C166InstPrinter(MAI, MII, MRI);
  return nullptr;
}

extern "C" LLVM_ABI LLVM_EXTERNAL_VISIBILITY void LLVMInitializeC166TargetMC() {
  Target &T = getTheC166Target();

  TargetRegistry::RegisterMCAsmInfo(T, createC166MCAsmInfo);
  TargetRegistry::RegisterMCInstrInfo(T, createC166MCInstrInfo);
  TargetRegistry::RegisterMCRegInfo(T, createC166MCRegisterInfo);
  TargetRegistry::RegisterMCSubtargetInfo(T, createC166MCSubtargetInfo);
  TargetRegistry::RegisterMCInstPrinter(T, createC166MCInstPrinter);
}
