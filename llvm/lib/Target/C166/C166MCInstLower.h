//===-- C166MCInstLower.h - Lower MachineInstr to MCInst --------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_C166_C166MCINSTLOWER_H
#define LLVM_LIB_TARGET_C166_C166MCINSTLOWER_H

#include "llvm/Support/Compiler.h"

namespace llvm {

class AsmPrinter;
class MCContext;
class MCInst;
class MCOperand;
class MCSymbol;
class MachineInstr;
class MachineOperand;

/// Lowers a C166 MachineInstr into an MCInst.
class LLVM_LIBRARY_VISIBILITY C166MCInstLower {
  MCContext &Ctx;
  AsmPrinter &Printer;

public:
  C166MCInstLower(MCContext &Ctx, AsmPrinter &Printer)
      : Ctx(Ctx), Printer(Printer) {}

  void Lower(const MachineInstr *MI, MCInst &OutMI) const;

  MCOperand lowerSymbolOperand(const MachineOperand &MO, MCSymbol *Sym) const;

  MCSymbol *getGlobalAddressSymbol(const MachineOperand &MO) const;
  MCSymbol *getExternalSymbolSymbol(const MachineOperand &MO) const;
  MCSymbol *getJumpTableSymbol(const MachineOperand &MO) const;
  MCSymbol *getConstantPoolIndexSymbol(const MachineOperand &MO) const;
  MCSymbol *getBlockAddressSymbol(const MachineOperand &MO) const;
};

} // end namespace llvm

#endif
