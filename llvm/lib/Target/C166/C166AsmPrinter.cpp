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
#include "MCTargetDesc/C166MCAsmInfo.h"
#include "TargetInfo/C166TargetInfo.h"
#include "llvm/BinaryFormat/ELF.h"
#include "llvm/CodeGen/AsmPrinter.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Mangler.h"
#include "llvm/IR/Module.h"
#include "llvm/MC/MCAsmInfo.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCSectionELF.h"
#include "llvm/MC/MCStreamer.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/MC/MCSymbol.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/FormatVariadic.h"
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
  void emitEndOfAsmFile(Module &M) override;
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

/// Emit the vector table slots that __attribute__((interrupt(n))) asked for.
///
/// A slot is a jump rather than an address: TRAP and a hardware interrupt both
/// branch to the slot instead of reading through it, so what has to be there
/// is a JMPS to the handler.  Each one goes in a section of its own named for
/// its trap number, which is what startup/vectors.ld places at 4n into the
/// segment the table lives in; the padding to three digits is so that the name
/// sorts the way the number does, for anyone reading a map file.
///
/// This is done at the end of the file rather than beside each function
/// because a slot is not part of the function's section, and because doing it
/// in one place is what makes two handlers claiming one slot something this
/// can see rather than something the linker trips over later.
void C166AsmPrinter::emitEndOfAsmFile(Module &M) {
  StringRef ClaimedBy[128] = {};

  for (const Function &F : M) {
    if (F.isDeclaration())
      continue;
    Attribute A = F.getFnAttribute("c166-interrupt-vector");
    if (!A.isStringAttribute())
      continue;
    // The front end has already checked this; what reaches here unchecked is
    // hand written IR, which gets a diagnostic rather than a crash.
    unsigned Number = 0;
    if (A.getValueAsString().getAsInteger(10, Number) || Number == 0 ||
        Number > 127) {
      M.getContext().emitError("'c166-interrupt-vector' on '" + F.getName() +
                               "' must be a trap number 1 to 127, not '" +
                               A.getValueAsString() + "'");
      continue;
    }

    // Two handlers in one translation unit asking for the same slot.  Across
    // translation units the linker catches it, because the second section in
    // a slot pushes the location counter past where the next slot has to
    // start, but it can only say so in terms of addresses.
    if (!ClaimedBy[Number].empty()) {
      M.getContext().emitError("interrupt vector " + Twine(Number) +
                               " is claimed by both '" + ClaimedBy[Number] +
                               "' and '" + F.getName() + "'");
      continue;
    }
    ClaimedBy[Number] = F.getName();

    MCSection *Slot = OutContext.getELFSection(
        formatv(".vectors.{0:3}", Number).str(), ELF::SHT_PROGBITS,
        ELF::SHF_ALLOC | ELF::SHF_EXECINSTR);
    OutStreamer->switchSection(Slot);

    const MCExpr *Ref = MCSymbolRefExpr::create(getSymbol(&F), OutContext);
    MCInst Jump;
    Jump.setOpcode(C166::JMPS);
    Jump.addOperand(MCOperand::createExpr(
        MCSpecifierExpr::create(Ref, C166::S_SEG, OutContext)));
    Jump.addOperand(MCOperand::createExpr(
        MCSpecifierExpr::create(Ref, C166::S_SOF, OutContext)));
    OutStreamer->emitInstruction(Jump, TM.getMCSubtargetInfo());
  }
}

char C166AsmPrinter::ID = 0;

INITIALIZE_PASS(C166AsmPrinter, "c166-asm-printer", "C166 Assembly Printer",
                false, false)

extern "C" LLVM_ABI LLVM_EXTERNAL_VISIBILITY void
LLVMInitializeC166AsmPrinter() {
  RegisterAsmPrinter<C166AsmPrinter> X(getTheC166Target());
}
