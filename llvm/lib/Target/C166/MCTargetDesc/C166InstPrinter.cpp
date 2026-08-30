//===-- C166InstPrinter.cpp - Convert C166 MCInst to assembly -------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "C166InstPrinter.h"
#include "C166MCTargetDesc.h"
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
  this->STI = &STI;
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
    // A data address is an unsigned 16 bit quantity.  Where one of the special
    // function registers is mapped, name it: the assembler takes the name back
    // as the same address, so this still reassembles to the bytes it came
    // from, and "mov r2, mdl" says what it is doing.
    uint64_t Addr = static_cast<uint64_t>(Op.getImm()) & 0xffff;
    if (StringRef Name = C166::getSFRName(MRI, Addr, STI); !Name.empty())
      O << Name;
    else
      O << Addr;
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

  // The displacement counts words from the instruction after this one, so
  // naming the target means knowing how long this one is: the bit test
  // branches are four bytes and JMPR is two.  Naming it is what a disassembly
  // wants, but only the distance can be fed back to the assembler, so the
  // distance stays the default.
  int64_t Words = SignExtend64<8>(Op.getImm());
  if (PrintBranchImmAsAddress) {
    uint64_t Size = MII.get(MI->getOpcode()).getSize();
    O << formatHex((Address + Size + 2 * Words) & 0xffff);
  } else {
    O << Words;
  }
}

void C166InstPrinter::printMemRPostIncOperand(const MCInst *MI, unsigned OpNo,
                                              raw_ostream &O) {
  O << '[' << getRegisterName(MI->getOperand(OpNo).getReg()) << "+]";
}

void C166InstPrinter::printBitOffOperand(const MCInst *MI, unsigned OpNo,
                                         raw_ostream &O) {
  const MCOperand &Op = MI->getOperand(OpNo);
  if (!Op.isImm()) {
    assert(Op.isExpr() && "unknown operand kind in printBitOffOperand");
    MAI.printExpr(O, *Op.getExpr());
    return;
  }

  // F0H to FFH is the general purpose register window, and 80H to EFH names a
  // bit-addressable special function register, whose bitoff is the short
  // address it already carries.  Below that is bit-addressable RAM, which has
  // no names to give it.
  uint64_t Off = static_cast<uint64_t>(Op.getImm()) & 0xff;
  if (Off >= 0xf0) {
    O << "r" << (Off - 0xf0);
    return;
  }
  if (C166::isSFRBitAddressable(Off)) {
    StringRef Name =
        C166::getSFRName(MRI, C166::getSFRAddressForShort(Off), STI);
    if (!Name.empty()) {
      O << Name;
      return;
    }
  }
  O << Off;
}

/// The bit position of a bit address, which is written bare rather than as a
/// constant: it names which bit of the word, not a value to operate with.
void C166InstPrinter::printMemRPreDecOperand(const MCInst *MI, unsigned OpNo,
                                            raw_ostream &O) {
  O << "[-" << getRegisterName(MI->getOperand(OpNo).getReg()) << ']';
}

void C166InstPrinter::printBitPosOperand(const MCInst *MI, unsigned OpNo,
                                         raw_ostream &O) {
  O << (MI->getOperand(OpNo).getImm() & 0xf);
}

void C166InstPrinter::printBitAddrOperand(const MCInst *MI, unsigned OpNo,
                                          raw_ostream &O) {
  printBitOffOperand(MI, OpNo, O);
  O << '.' << (MI->getOperand(OpNo + 1).getImm() & 0xf);
}

/// The step a MAC unit pointer takes after it is read.  1 leaves it alone and
/// prints nothing; the rest are what the manual writes.
static const char *coStep(unsigned Update, bool IsIdx) {
  switch (Update) {
  case 1:
    return "";
  case 2:
    return "+";
  case 3:
    return "-";
  case 4:
    return IsIdx ? "+qx0" : "+qr0";
  case 5:
    return IsIdx ? "-qx0" : "-qr0";
  case 6:
    return IsIdx ? "+qx1" : "+qr1";
  case 7:
    return IsIdx ? "-qx1" : "-qr1";
  default:
    return "?";
  }
}

void C166InstPrinter::printCoPtrOperand(const MCInst *MI, unsigned OpNo,
                                        raw_ostream &O) {
  O << '[' << getRegisterName(MI->getOperand(OpNo).getReg())
    << coStep(MI->getOperand(OpNo + 1).getImm(), /*IsIdx=*/false) << ']';
}

void C166InstPrinter::printCoIdxOperand(const MCInst *MI, unsigned OpNo,
                                        raw_ostream &O) {
  O << '[' << getRegisterName(MI->getOperand(OpNo).getReg())
    << coStep(MI->getOperand(OpNo + 1).getImm(), /*IsIdx=*/true) << ']';
}

void C166InstPrinter::printCCOperand(const MCInst *MI, unsigned OpNo,
                                     raw_ostream &O) {
  auto CC = static_cast<C166CC::CondCode>(MI->getOperand(OpNo).getImm());
  if (const char *Name = C166CC::getCondCodeName(CC))
    O << Name;
  else
    llvm_unreachable("Unsupported condition code");
}
