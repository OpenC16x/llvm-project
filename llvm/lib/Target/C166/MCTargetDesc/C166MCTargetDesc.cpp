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
#include "C166UnwindRules.h"
#include "llvm/Support/LEB128.h"
#include "C166.h"
#include "C166InstPrinter.h"
#include "C166MCAsmInfo.h"
#include "TargetInfo/C166TargetInfo.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/MC/MCAsmInfo.h"
#include "llvm/MC/MCDwarf.h"
#include "llvm/MC/MCInstrInfo.h"
#include "llvm/MC/MCRegisterInfo.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/Compiler.h"

#include <string>
#include <vector>

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
  // The second argument is the DWARF return address column.  On most targets
  // that is a register holding the return address; here nothing does - a CALL
  // puts it on the hardware stack - so it is the synthetic PC, and
  // C166FrameLowering emits a rule that computes it from the stack.  It used
  // to be R0, which is the ABI stack pointer and meant nothing at all.
  InitC166MCRegisterInfo(X, C166::PC);
  return X;
}

/// The words the assembler will not read as a symbol, because it reads them as
/// something else first.
///
/// An operand like "t2" is the T2 timer at FE40H, not a program's variable
/// called t2, and the assembler has no way to tell that anyone meant otherwise:
/// a symbol may be defined after it is used, so there is nothing to consult at
/// the point the name is read.  Left alone that is silent - "mov r2, t2" would
/// assemble to a load from FE40H with no relocation and no diagnostic - which
/// matters because it is what the compiler itself writes for a variable of that
/// name, so compiling to assembly and assembling that back would not produce
/// the same program.
///
/// Saying the names are reserved is what stops it: MCSymbol::print quotes a
/// name that is, so the compiler writes "mov r2, \"t2\"", and a quoted name
/// reaches the parser as a string rather than an identifier and can only be a
/// symbol.  The register keeps the bare spelling, so a disassembly still says
/// "mov r2, mdl" and still assembles back to the bytes it came from.
static void populateReservedIdentifiers(MCAsmInfo &MAI,
                                        const MCRegisterInfo &MRI) {
  // The set holds references rather than copies, and the lookup lowercases
  // what it is asked about, so the names have to be lower case and have to
  // outlive every MCAsmInfo.  They are gathered once into a table of their own
  // for both of those reasons.
  static const std::vector<std::string> Names = [&MRI] {
    std::vector<std::string> V;

    // Every register name, which is every special function register, every
    // extended one and every X-peripheral one as well as R0 to R15: the parser
    // looks all of them up before it considers a symbol.  The printer's table
    // is the assembly spelling, and PC has none - it exists for the debug
    // information - so it is skipped along with anything else that gains one.
    for (unsigned I = 1, E = MRI.getNumRegs(); I != E; ++I)
      if (const char *Name = C166InstPrinter::getRegisterName(I))
        if (Name[0])
          V.push_back(StringRef(Name).lower());

    // The condition codes, both spellings of the four that have two.  The
    // parser takes any "cc_" name to be one of these, so a symbol whose name
    // merely starts that way is still rejected - but that is a diagnostic
    // rather than a wrong program, which is the difference that matters.
    for (unsigned CC = 0; CC != 16; ++CC)
      if (const char *Name = C166CC::getCondCodeName(C166CC::CondCode(CC)))
        V.push_back(StringRef(Name).lower());
    for (auto [Alias, CC] : C166CC::getCondCodeAliases())
      V.push_back(StringRef(Alias).lower());

    return V;
  }();

  auto &Set = MAI.getReservedIdentifiers();
  for (const std::string &Name : Names)
    Set.insert(CachedHashStringRef(Name));
}

static MCAsmInfo *createC166MCAsmInfo(const MCRegisterInfo &MRI,
                                      const Triple &TT,
                                      const MCTargetOptions &Options) {
  MCAsmInfo *MAI = new C166MCAsmInfo(TT, Options);

  populateReservedIdentifiers(*MAI, MRI);

  // The ABI stack lives in R0 and grows down.  On function entry it points
  // straight at the first stack argument: the return address is kept on the
  // separate hardware stack and is not part of this frame.
  MAI->addInitialFrameState(MCCFIInstruction::cfiDefCfa(
      nullptr, MRI.getDwarfRegNum(C166::R0, /*isEH=*/false), 0));

  // And where that return address is.  Almost every function was entered with
  // a near call and has pushed nothing else, so those rules go here rather than
  // into every frame; the other shapes say so in their own.
  SmallVector<MCCFIInstruction, 4> Rules;
  C166::getUnwindRules(Rules, MRI, C166::EntryKind::Near, /*Depth=*/0);
  for (const MCCFIInstruction &Inst : Rules)
    MAI->addInitialFrameState(Inst);

  return MAI;
}

bool C166::isSFRInSelectedMap(MCRegister Reg, const MCSubtargetInfo &STI) {
  if (getC166MCRegisterClass(C166::SFRXC164RegClassID).contains(Reg))
    return STI.hasFeature(C166::FeatureSFRXC164);
  if (getC166MCRegisterClass(C166::SFRC167RegClassID).contains(Reg))
    return STI.hasFeature(C166::FeatureSFRC167);
  // Except for the coprocessor's four offset registers, which are the MAC
  // unit's rather than a part's: the ST10F269 datasheet gives QX0 F000H, QX1
  // F002H, QR0 F004H and QR1 F006H, the same four at the same addresses the
  // XC164CM User's Manual gives, so what decides whether they can be named is
  // whether there is a unit to own them.
  if (getC166MCRegisterClass(C166::CoOFFSRegClassID).contains(Reg))
    return STI.hasFeature(C166::FeatureMAC);
  // The rest of the extended and X-peripheral registers are the XC164CM's
  // entire, so they go the same way as its own short-address ones.  They are
  // reachable by address rather than through a "reg" field, which makes them
  // harmless to encode and not at all harmless to name: the address would be
  // written, and would mean something else on a part that has something else
  // there.
  if (getC166MCRegisterClass(C166::ESFRRegClassID).contains(Reg) ||
      getC166MCRegisterClass(C166::XSFRRegClassID).contains(Reg))
    return STI.hasFeature(C166::FeatureSFRXC164);
  return true;
}

static MCSubtargetInfo *createC166MCSubtargetInfo(const Triple &TT,
                                                  StringRef CPU, StringRef FS) {
  // The same default the code generator picks, for the same reason: "c166" is
  // the first generation part, which has neither the EXTend instructions nor
  // ATOMIC, and an assembler that rejected those by default would reject the
  // output of a compiler that was not told a CPU either.  See C166.td.
  if (CPU.empty())
    CPU = "generic";
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

// The spelling to match is the one the disassembler prints, which is the
// register's AsmName.  MCRegisterInfo::getName gives the TableGen record name
// instead - "SYSSP" where the assembly says "sp" - so both directions go
// through the printer's table and cannot disagree about a name.
int64_t C166::getSFRShortAddress(const MCRegisterInfo &MRI, StringRef Name) {
  std::string Lowered = Name.lower();
  for (MCPhysReg Reg : MRI.getRegClass(C166::SFRRegClassID))
    if (Lowered == C166InstPrinter::getRegisterName(Reg))
      return MRI.getEncodingValue(Reg);
  return -1;
}

int64_t C166::getSFRAddress(const MCRegisterInfo &MRI, StringRef Name) {
  if (int64_t Short = getSFRShortAddress(MRI, Name); Short >= 0)
    return getSFRAddressForShort(Short);

  // An extended register is reachable by address even though it is not
  // reachable through a "reg" field.
  std::string Lowered = Name.lower();
  for (MCPhysReg Reg : MRI.getRegClass(C166::ESFRRegClassID))
    if (Lowered == C166InstPrinter::getRegisterName(Reg))
      return getESFRAddressForShort(MRI.getEncodingValue(Reg));

  // An X-peripheral register carries its whole address, having no short one.
  for (MCPhysReg Reg : MRI.getRegClass(C166::XSFRRegClassID))
    if (Lowered == C166InstPrinter::getRegisterName(Reg))
      return MRI.getEncodingValue(Reg);
  return -1;
}

StringRef C166::getSFRName(const MCRegisterInfo &MRI, uint64_t Addr,
                           const MCSubtargetInfo *STI) {
  for (MCPhysReg Reg : MRI.getRegClass(C166::SFRRegClassID))
    if (getSFRAddressForShort(MRI.getEncodingValue(Reg)) == Addr &&
        (!STI || isSFRInSelectedMap(Reg, *STI)))
      return C166InstPrinter::getRegisterName(Reg);
  for (MCPhysReg Reg : MRI.getRegClass(C166::ESFRRegClassID))
    if (getESFRAddressForShort(MRI.getEncodingValue(Reg)) == Addr)
      return C166InstPrinter::getRegisterName(Reg);
  for (MCPhysReg Reg : MRI.getRegClass(C166::XSFRRegClassID))
    if (MRI.getEncodingValue(Reg) == Addr)
      return C166InstPrinter::getRegisterName(Reg);
  return StringRef();
}

extern "C" LLVM_ABI LLVM_EXTERNAL_VISIBILITY void LLVMInitializeC166TargetMC() {
  Target &T = getTheC166Target();

  TargetRegistry::RegisterMCAsmInfo(T, createC166MCAsmInfo);
  TargetRegistry::RegisterMCInstrInfo(T, createC166MCInstrInfo);
  TargetRegistry::RegisterMCRegInfo(T, createC166MCRegisterInfo);
  TargetRegistry::RegisterMCSubtargetInfo(T, createC166MCSubtargetInfo);
  TargetRegistry::RegisterMCInstPrinter(T, createC166MCInstPrinter);
  TargetRegistry::RegisterMCCodeEmitter(T, createC166MCCodeEmitter);
  TargetRegistry::RegisterMCAsmBackend(T, createC166MCAsmBackend);
}

//===----------------------------------------------------------------------===//
// Call frame information
//===----------------------------------------------------------------------===//
//
// See C166UnwindRules.h for what these rules have to say and why they are
// expressions rather than offsets.

namespace {
/// The DWARF operations these rules are built from.
enum : uint8_t {
  DW_OP_const1u = 0x08,
  DW_OP_or = 0x21,
  DW_OP_shl = 0x24,
  DW_OP_bregx = 0x92,
  DW_OP_deref_size = 0x94,
  DW_CFA_expression = 0x10,
  DW_CFA_val_expression = 0x16,
};

void appendULEB(std::string &S, uint64_t V) {
  raw_string_ostream OS(S);
  encodeULEB128(V, OS);
}

void appendSLEB(std::string &S, int64_t V) {
  raw_string_ostream OS(S);
  encodeSLEB128(V, OS);
}

/// "the value in DWARF register Reg, plus Off".
void appendBregx(std::string &S, unsigned Reg, int64_t Off) {
  S.push_back(DW_OP_bregx);
  appendULEB(S, Reg);
  appendSLEB(S, Off);
}

/// "the word at the address on top of the stack", zero extended.  Everything
/// the hardware stack holds is one word wide.
void appendDerefWord(std::string &S) {
  S.push_back(DW_OP_deref_size);
  S.push_back(2);
}

/// "shifted up into the segment field".
void appendToSegment(std::string &S) {
  S.push_back(DW_OP_const1u);
  S.push_back(16);
  S.push_back(DW_OP_shl);
}

/// A DW_CFA_expression or DW_CFA_val_expression naming \p Reg, whose operand
/// is the expression \p Expr.
std::string cfaExpression(uint8_t Op, unsigned Reg, StringRef Expr) {
  std::string S;
  S.push_back(Op);
  appendULEB(S, Reg);
  appendULEB(S, Expr.size());
  S.append(Expr.begin(), Expr.end());
  return S;
}
} // end anonymous namespace

void C166::getUnwindRules(SmallVectorImpl<MCCFIInstruction> &Out,
                          const MCRegisterInfo &MRI, EntryKind Kind,
                          unsigned Depth) {
  // This target emits .debug_frame rather than .eh_frame, and has one register
  // numbering either way, so the two forms of the question have one answer.
  auto Dwarf = [&](MCRegister Reg) {
    return unsigned(MRI.getDwarfRegNum(Reg, /*isEH=*/false));
  };
  const unsigned SP = Dwarf(C166::SYSSP);
  const unsigned CSP = Dwarf(C166::CSP);

  // How much the entry left on the hardware stack, and whether the caller's
  // CSP is part of it.  When it is not, a near call got us here and the
  // caller's CSP is the one we are running with.
  const bool SavesCSP = Kind != EntryKind::Near;
  const bool SavesPSW = Kind == EntryKind::Interrupt;
  const unsigned Size = Kind == EntryKind::Near      ? 2
                        : Kind == EntryKind::Far     ? 4
                                                     : 6;
  const int64_t Base = Depth;

  // The return address: the segment shifted up, with the offset below it.
  std::string RA;
  if (SavesCSP) {
    appendBregx(RA, SP, Base + 2);
    appendDerefWord(RA);
  } else {
    appendBregx(RA, CSP, 0);
  }
  appendToSegment(RA);
  appendBregx(RA, SP, Base);
  appendDerefWord(RA);
  RA.push_back(DW_OP_or);
  Out.push_back(MCCFIInstruction::createEscape(
      nullptr, cfaExpression(DW_CFA_val_expression, Dwarf(C166::PC), RA), {},
      "return address, from the hardware stack"));

  // The caller's hardware stack pointer is ours with the whole record popped.
  std::string CallerSP;
  appendBregx(CallerSP, SP, Base + Size);
  Out.push_back(MCCFIInstruction::createEscape(
      nullptr, cfaExpression(DW_CFA_val_expression, SP, CallerSP), {},
      "caller's hardware stack pointer"));

  // The caller's CSP, which a near call did not disturb.
  if (SavesCSP) {
    std::string Saved;
    appendBregx(Saved, SP, Base + 2);
    Out.push_back(MCCFIInstruction::createEscape(
        nullptr, cfaExpression(DW_CFA_expression, CSP, Saved), {},
        "caller's CSP"));
  } else {
    Out.push_back(MCCFIInstruction::createSameValue(nullptr, CSP));
  }

  // An interrupt saved the PSW too, and RETI is what puts it back.
  if (SavesPSW) {
    std::string Saved;
    appendBregx(Saved, SP, Base + 4);
    Out.push_back(MCCFIInstruction::createEscape(
        nullptr, cfaExpression(DW_CFA_expression, Dwarf(C166::PSW), Saved), {},
        "caller's PSW"));
  }
}
