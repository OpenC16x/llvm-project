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
#include "llvm/Support/MathExtras.h"
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

  // The displacement is always printed, zero included: "[Rw]" is the shorter
  // instruction rather than the same one with nothing added.
  O << '[' << getRegisterName(Base.getReg()) << "+#";
  if (Disp.isImm())
    O << Disp.getImm();
  else {
    assert(Disp.isExpr() && "unknown displacement in printMemRIOperand");
    MAI.printExpr(O, *Disp.getExpr());
  }
  O << ']';
}

void C166InstPrinter::printMemROperand(const MCInst *MI, unsigned OpNo,
                                       raw_ostream &O) {
  O << '[' << getRegisterName(MI->getOperand(OpNo).getReg()) << ']';
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

void C166InstPrinter::printRelTargetOperand(const MCInst *MI, uint64_t Address,
                                            unsigned OpNo, raw_ostream &O) {
  const MCOperand &Op = MI->getOperand(OpNo);
  if (!Op.isImm()) {
    assert(Op.isExpr() && "unknown operand kind in printRelTargetOperand");
    MAI.printExpr(O, *Op.getExpr());
    return;
  }

  // The displacement counts words from the instruction after this one, which
  // is four bytes on: every relative branch here is a long one.  Naming the
  // target instead is what a disassembly wants, but only the distance can be
  // fed back to the assembler, so it stays the default.
  int64_t Words = SignExtend64<8>(Op.getImm());
  if (PrintBranchImmAsAddress)
    O << formatHex((Address + 4 + 2 * Words) & 0xffff);
  else
    O << Words;
}

void C166InstPrinter::printBitOffOperand(const MCInst *MI, unsigned OpNo,
                                         raw_ostream &O) {
  const MCOperand &Op = MI->getOperand(OpNo);
  if (!Op.isImm()) {
    assert(Op.isExpr() && "unknown operand kind in printBitOffOperand");
    MAI.printExpr(O, *Op.getExpr());
    return;
  }

  // F0H to FFH is the general purpose register window, which reads better by
  // name; the rest of the bit-addressable space has no names to give it.
  uint64_t Off = static_cast<uint64_t>(Op.getImm()) & 0xff;
  if (Off >= 0xf0)
    O << "r" << (Off - 0xf0);
  else
    O << Off;
}

void C166InstPrinter::printBitAddrOperand(const MCInst *MI, unsigned OpNo,
                                          raw_ostream &O) {
  printBitOffOperand(MI, OpNo, O);
  O << '.' << (MI->getOperand(OpNo + 1).getImm() & 0xf);
}

void C166InstPrinter::printCCOperand(const MCInst *MI, unsigned OpNo,
                                     raw_ostream &O) {
  auto CC = static_cast<C166CC::CondCode>(MI->getOperand(OpNo).getImm());
  if (const char *Name = C166CC::getCondCodeName(CC))
    O << Name;
  else
    llvm_unreachable("Unsupported condition code");
}
