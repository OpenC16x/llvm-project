//===-- C166AsmPrinter.cpp - C166 LLVM assembly writer --------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains a printer that converts from our internal representation
// of machine-dependent LLVM code to the C166 assembly language.
//
//===----------------------------------------------------------------------===//

#include "C166.h"
#include "C166MCInstLower.h"
#include "C166TargetMachine.h"
#include "MCTargetDesc/C166InstPrinter.h"
#include "TargetInfo/C166TargetInfo.h"
#include "llvm/CodeGen/AsmPrinter.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Mangler.h"
#include "llvm/MC/MCAsmInfo.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCStreamer.h"
#include "llvm/MC/MCSymbol.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;

#define DEBUG_TYPE "asm-printer"

namespace {

class C166AsmPrinter : public AsmPrinter {
public:
  static char ID;

  C166AsmPrinter(TargetMachine &TM, std::unique_ptr<MCStreamer> Streamer)
      : AsmPrinter(TM, std::move(Streamer), ID) {}

  StringRef getPassName() const override { return "C166 Assembly Printer"; }

  void PrintSymbolOperand(const MachineOperand &MO, raw_ostream &O) override;
  void printOperand(const MachineInstr *MI, int OpNum, raw_ostream &O);
  bool PrintAsmOperand(const MachineInstr *MI, unsigned OpNo,
                       const char *ExtraCode, raw_ostream &O) override;
  bool PrintAsmMemoryOperand(const MachineInstr *MI, unsigned OpNo,
                             const char *ExtraCode, raw_ostream &O) override;
  void emitInstruction(const MachineInstr *MI) override;
  const MCExpr *lowerConstant(const Constant *CV, const Constant *BaseCV,
                              uint64_t Offset) override;
};

} // end anonymous namespace

void C166AsmPrinter::PrintSymbolOperand(const MachineOperand &MO,
                                        raw_ostream &O) {
  uint64_t Offset = MO.getOffset();
  if (Offset)
    O << '(' << Offset << '+';

  getSymbol(MO.getGlobal())->print(O, MAI);

  if (Offset)
    O << ')';
}

void C166AsmPrinter::printOperand(const MachineInstr *MI, int OpNum,
                                  raw_ostream &O) {
  const MachineOperand &MO = MI->getOperand(OpNum);
  switch (MO.getType()) {
  case MachineOperand::MO_Register:
    O << C166InstPrinter::getRegisterName(MO.getReg());
    return;
  case MachineOperand::MO_Immediate:
    O << '#' << MO.getImm();
    return;
  case MachineOperand::MO_MachineBasicBlock:
    MO.getMBB()->getSymbol()->print(O, MAI);
    return;
  case MachineOperand::MO_GlobalAddress:
    O << '#';
    PrintSymbolOperand(MO, O);
    return;
  default:
    llvm_unreachable("Not implemented yet!");
  }
}

bool C166AsmPrinter::PrintAsmOperand(const MachineInstr *MI, unsigned OpNo,
                                     const char *ExtraCode, raw_ostream &O) {
  if (ExtraCode && ExtraCode[0])
    return AsmPrinter::PrintAsmOperand(MI, OpNo, ExtraCode, O);

  printOperand(MI, OpNo, O);
  return false;
}

bool C166AsmPrinter::PrintAsmMemoryOperand(const MachineInstr *MI,
                                           unsigned OpNo,
                                           const char *ExtraCode,
                                           raw_ostream &O) {
  if (ExtraCode && ExtraCode[0])
    return true; // Unknown modifier.

  const MachineOperand &Base = MI->getOperand(OpNo);
  const MachineOperand &Disp = MI->getOperand(OpNo + 1);

  O << '[' << C166InstPrinter::getRegisterName(Base.getReg());
  if (Disp.isImm() && Disp.getImm() != 0)
    O << "+#" << Disp.getImm();
  O << ']';
  return false;
}

// A far function has no near address to store: it can only be reached with a
// CALLS that names its segment, so a pointer to one would be a near address
// that nothing can call correctly.  ISel catches the same mistake in code;
// this catches it in an initialiser.
const MCExpr *C166AsmPrinter::lowerConstant(const Constant *CV,
                                            const Constant *BaseCV,
                                            uint64_t Offset) {
  if (const auto *F = dyn_cast<Function>(CV); F && F->hasFnAttribute("far"))
    OutContext.reportError(
        SMLoc(), "cannot take the address of far function '" + F->getName() +
                     "'; it can only be called by name");
  return AsmPrinter::lowerConstant(CV, BaseCV, Offset);
}

void C166AsmPrinter::emitInstruction(const MachineInstr *MI) {
  C166_MC::verifyInstructionPredicates(MI->getOpcode(),
                                       getSubtargetInfo().getFeatureBits());

  C166MCInstLower MCInstLowering(OutContext, *this);

  auto Emit = [&](const MachineInstr *MI) {
    MCInst TmpInst;
    MCInstLowering.Lower(MI, TmpInst);
    EmitToStreamer(*OutStreamer, TmpInst);
  };

  // A bundle is only ever a group of instructions that must not be separated,
  // such as an EXTS and the far access it covers, so emit its contents in
  // order and let the assembler see nothing unusual.
  if (!MI->isBundle()) {
    Emit(MI);
    return;
  }

  const MachineBasicBlock &MBB = *MI->getParent();
  for (auto I = std::next(MI->getIterator());
       I != MBB.instr_end() && I->isInsideBundle(); ++I)
    if (!I->isDebugInstr() && !I->isImplicitDef())
      Emit(&*I);
}

char C166AsmPrinter::ID = 0;

INITIALIZE_PASS(C166AsmPrinter, "c166-asm-printer", "C166 Assembly Printer",
                false, false)

extern "C" LLVM_ABI LLVM_EXTERNAL_VISIBILITY void
LLVMInitializeC166AsmPrinter() {
  RegisterAsmPrinter<C166AsmPrinter> X(getTheC166Target());
}
