//===-- C166MCAsmInfo.cpp - C166 asm properties ---------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "C166MCAsmInfo.h"
#include "llvm/ADT/StringSwitch.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/raw_ostream.h"
using namespace llvm;

void C166MCAsmInfo::anchor() {}

C166MCAsmInfo::C166MCAsmInfo(const Triple &TT, const MCTargetOptions &Options)
    : MCAsmInfoELF(Options) {
  CodePointerSize = 2;
  CalleeSaveStackSlotSize = 2;

  CommentString = ";";

  AlignmentIsInBytes = false;
  UsesELFSectionDirectiveForBSS = true;

  SupportsDebugInformation = true;
  ExceptionsType = ExceptionHandling::DwarfCFI;
}

C166::Specifier C166::parseSpecifier(StringRef Name) {
  return StringSwitch<C166::Specifier>(Name.lower())
      .Case("seg", C166::S_SEG)
      .Case("sof", C166::S_SOF)
      .Case("pag", C166::S_PAG)
      .Case("pof", C166::S_POF)
      .Default(C166::S_None);
}

StringRef C166::getSpecifierName(uint16_t S) {
  switch (S) {
  case C166::S_SEG:
    return "seg";
  case C166::S_SOF:
    return "sof";
  case C166::S_PAG:
    return "pag";
  case C166::S_POF:
    return "pof";
  }
  llvm_unreachable("unknown C166 relocation specifier");
}

uint64_t C166::applySpecifier(uint16_t S, uint64_t Value) {
  switch (S) {
  case C166::S_SEG:
    return (Value >> 16) & 0xff;
  case C166::S_SOF:
    return Value & 0xffff;
  case C166::S_PAG:
    return (Value >> 14) & 0x3ff;
  case C166::S_POF:
    return Value & 0x3fff;
  }
  return Value;
}

void C166MCAsmInfo::printSpecifierExpr(raw_ostream &OS,
                                       const MCSpecifierExpr &Expr) const {
  OS << C166::getSpecifierName(Expr.getSpecifier()) << '(';
  printExpr(OS, *Expr.getSubExpr());
  OS << ')';
}
