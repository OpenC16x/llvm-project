//===-- C166InstPrinter.cpp - Convert C166 MCInst to assembly -------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "C166InstPrinter.h"
#include "C166.h"
#include "llvm/MC/MCAsmInfo.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCInstrInfo.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/raw_ostream.h"
using namespace llvm;

#define DEBUG_TYPE "asm-printer"

// Include the auto-generated portion of the assembly writer.
#include "C166GenAsmWriter.inc"

void C166InstPrinter::printRegName(raw_ostream &O, MCRegister Reg) {
  O << getRegisterName(Reg);
}

void C166InstPrinter::printInst(const MCInst *MI, uint64_t Address,
                                StringRef Annot, const MCSubtargetInfo &STI,
                                raw_ostream &O) {
  printInstruction(MI, Address, O);
  printAnnotation(O, Annot);
}

void C166InstPrinter::printOperand(const MCInst *MI, unsigned OpNo,
                                   raw_ostream &O) {
  const MCOperand &Op = MI->getOperand(OpNo);
  if (Op.isReg()) {
    O << getRegisterName(Op.getReg());
  } else if (Op.isImm()) {
    O << '#' << Op.getImm();
  } else {
    assert(Op.isExpr() && "unknown operand kind in printOperand");
    O << '#';
    MAI.printExpr(O, *Op.getExpr());
  }
}

void C166InstPrinter::printMemRIOperand(const MCInst *MI, unsigned OpNo,
                                        raw_ostream &O) {
  const MCOperand &Base = MI->getOperand(OpNo);
  const MCOperand &Disp = MI->getOperand(OpNo + 1);

  O << '[' << getRegisterName(Base.getReg());
  if (Disp.isImm()) {
    int64_t Imm = Disp.getImm();
    if (Imm != 0)
      O << "+#" << Imm;
  } else {
    assert(Disp.isExpr() && "unknown displacement in printMemRIOperand");
    O << "+#";
    MAI.printExpr(O, *Disp.getExpr());
  }
  O << ']';
}

void C166InstPrinter::printAddr16Operand(const MCInst *MI, unsigned OpNo,
                                         raw_ostream &O) {
  const MCOperand &Op = MI->getOperand(OpNo);
  if (Op.isImm()) {
    // A data address is an unsigned 16 bit quantity.
    O << (static_cast<uint64_t>(Op.getImm()) & 0xffff);
  } else {
    assert(Op.isExpr() && "unknown operand kind in printAddr16Operand");
    MAI.printExpr(O, *Op.getExpr());
  }
}

void C166InstPrinter::printBrTargetOperand(const MCInst *MI, unsigned OpNo,
                                           raw_ostream &O) {
  printAddr16Operand(MI, OpNo, O);
}

void C166InstPrinter::printCallTargetOperand(const MCInst *MI, unsigned OpNo,
                                             raw_ostream &O) {
  printAddr16Operand(MI, OpNo, O);
}

void C166InstPrinter::printCCOperand(const MCInst *MI, unsigned OpNo,
                                     raw_ostream &O) {
  auto CC = static_cast<C166CC::CondCode>(MI->getOperand(OpNo).getImm());
  if (const char *Name = C166CC::getCondCodeName(CC))
    O << Name;
  else
    llvm_unreachable("Unsupported condition code");
}
