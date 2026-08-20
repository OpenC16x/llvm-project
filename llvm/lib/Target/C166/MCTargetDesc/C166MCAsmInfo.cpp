//===-- C166MCAsmInfo.cpp - C166 asm properties ---------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "C166MCAsmInfo.h"
using namespace llvm;

void C166MCAsmInfo::anchor() {}

C166MCAsmInfo::C166MCAsmInfo(const Triple &TT, const MCTargetOptions &Options)
    : MCAsmInfoELF(Options) {
  CodePointerSize = 2;
  CalleeSaveStackSlotSize = 2;

  CommentString = ";";

  AlignmentIsInBytes = false;
  UsesELFSectionDirectiveForBSS = true;

  // There is no assembly parser for C166 yet, so inline assembly has to be
  // passed through to the output verbatim.
  UseIntegratedAssembler = false;

  SupportsDebugInformation = true;
  ExceptionsType = ExceptionHandling::DwarfCFI;
}
