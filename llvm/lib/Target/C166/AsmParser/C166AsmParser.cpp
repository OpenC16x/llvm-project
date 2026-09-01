//===- C166AsmParser.cpp - Parse C166 assembly to MCInst instructions -----===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Operand parsing is syntax directed, because on C166 the printed form is what
// distinguishes the three flavours of 16 bit operand:
//
//   #1234          an immediate      (#data16 / #data8 / #data4)
//   label          an address        (mem / caddr)
//   [r1+#4]        a memory operand
//   cc_ULT         a condition code
//
//===----------------------------------------------------------------------===//

#include "C166.h"
#include "MCTargetDesc/C166MCAsmInfo.h"
#include "MCTargetDesc/C166MCTargetDesc.h"
#include "TargetInfo/C166TargetInfo.h"
#include "llvm/ADT/SmallBitVector.h"
#include "llvm/ADT/StringSwitch.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCInstrInfo.h"
#include "llvm/MC/MCParser/AsmLexer.h"
#include "llvm/MC/MCParser/MCParsedAsmOperand.h"
#include "llvm/MC/MCParser/MCTargetAsmParser.h"
#include "llvm/MC/MCRegisterInfo.h"
#include <list>
#include "llvm/MC/MCStreamer.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/Compiler.h"

using namespace llvm;

#define DEBUG_TYPE "c166-asm-parser"

// Defined by the generated matcher at the bottom of this file.
static MCRegister MatchRegisterName(StringRef Name);

namespace {

/// A parsed C166 assembly operand.
class C166Operand : public MCParsedAsmOperand {
  enum KindTy {
    k_Token,
    k_Register,
    k_Immediate,
    k_Address,
    k_CondCode,
    k_Memory,
    k_SFR,
    k_BitAddr,
  } Kind;

  struct MemoryOp {
    MCRegister Base;
    const MCExpr *Disp;
    // "[Rw]", "[Rw + #0]" and "[Rw+]" are three different instructions, so
    // which one was written has to survive parsing.
    bool HasDisp;
    bool PostInc;
    // "[-Rw]" steps the pointer back before writing through it, which is the
    // only auto-stepping store the machine has.
    bool PreDec;
    // What the MAC unit's pointer forms do to the pointer after reading it.
    // The values are the ones the instruction carries: 1 leaves it alone, 2
    // and 3 step it by a word, and 4 to 7 step it by one of the two offset
    // registers.  An ordinary "[Rw]" is 1 and "[Rw+]" is 2, so the older forms
    // and these are the same syntax where they overlap.
    unsigned CoUpdate;
    // Whether the base is one of the MAC unit's own pointers rather than a
    // general purpose register, which the instruction encodes differently.
    bool IsIdx;
  };

  StringRef Tok;
  MCRegister Reg;
  const MCExpr *Imm = nullptr;
  int64_t CC = 0;
  MemoryOp Mem;
  int64_t BitOff = 0;
  int64_t BitPos = 0;
  /// The word a bit lives in, when it is a symbol rather than a number.  The
  /// linker turns the address into a bitoff, because the mapping is two
  /// windows from two bases and only the final address decides which.
  const MCExpr *BitOffExpr = nullptr;

  SMLoc Start, End;

public:
  C166Operand(KindTy Kind, SMLoc S, SMLoc E) : Kind(Kind), Start(S), End(E) {}

  bool isToken() const override { return Kind == k_Token; }
  // A special function register name is both: PUSH and POP name one in their
  // "reg" field, and every other instruction that mentions one means the
  // address it is mapped to.
  bool isReg() const override { return Kind == k_Register || Kind == k_SFR; }
  bool isImm() const override { return Kind == k_Immediate; }
  bool isMem() const override { return Kind == k_Memory; }
  bool isAddr() const { return Kind == k_Address || Kind == k_SFR; }
  bool isCondCode() const { return Kind == k_CondCode; }
  bool isMemR() const {
    return Kind == k_Memory && !Mem.HasDisp && !Mem.PostInc && !Mem.PreDec &&
           !Mem.IsIdx && Mem.CoUpdate == 1;
  }

  /// A MAC unit pointer: any of the update forms, on a general purpose
  /// register.  "[Rw]" and "[Rw+]" are also ordinary memory operands, and
  /// which one an instruction wants is what tells them apart.
  bool isCoPtr() const {
    return Kind == k_Memory && !Mem.HasDisp && !Mem.IsIdx && !Mem.PreDec;
  }

  /// The same on IDX0 or IDX1.
  bool isCoIdx() const {
    return Kind == k_Memory && !Mem.HasDisp && Mem.IsIdx;
  }
  bool isMemRI() const { return Kind == k_Memory && Mem.HasDisp; }
  bool isMemRPostInc() const { return Kind == k_Memory && Mem.PostInc; }
  bool isMemRPreDec() const { return Kind == k_Memory && Mem.PreDec; }

  /// The ALU's indirect forms reach only R0 to R3, which is all their two bit
  /// pointer field can name.
  bool isPointerReg() const {
    return Mem.Base == C166::R0 || Mem.Base == C166::R1 ||
           Mem.Base == C166::R2 || Mem.Base == C166::R3;
  }
  bool isMemRP() const { return isMemR() && isPointerReg(); }
  bool isMemRPPostInc() const { return isMemRPostInc() && isPointerReg(); }

  /// True when this is an immediate whose value is known and in [Low, High].
  /// A symbol reference is accepted for the wider fields, which can hold a
  /// relocation; a four bit field cannot, so it must be a known constant.
  bool isImmInRange(int64_t Low, int64_t High, bool AllowSymbol) const {
    if (Kind != k_Immediate)
      return false;

    const auto *CE = dyn_cast<MCConstantExpr>(Imm);
    if (!CE)
      return AllowSymbol;

    return CE->getValue() >= Low && CE->getValue() <= High;
  }

  bool isImm3() const { return isImmInRange(0, 7, /*AllowSymbol=*/false); }
  bool isImm4() const { return isImmInRange(0, 15, /*AllowSymbol=*/false); }
  // The MAC shifter's count.  Five bits are encoded and only 0 to 8 are
  // defined; see "coshift" in C166InstrInfo.td.
  // Any constant immediate.  The shifter's range is checked after the match
  // instead; see matchAndEmitInstruction.  Accepting the whole of it here is
  // what makes a count of -1 and a count of 9 get the same answer, rather
  // than one of them falling through to another form's diagnostic.
  bool isCoShift() const {
    return Kind == k_Immediate && isa<MCConstantExpr>(Imm);
  }
  // How many times a repeatable coprocessor instruction runs.  1 is MRW and
  // is only ever produced by the parser below, never written; 0 is the plain
  // form and has no prefix to write it with.  See "corepeat" in
  // C166InstrInfo.td.
  bool isCoRepeat() const { return isImmInRange(1, 31, /*AllowSymbol=*/false); }
  bool isData8() const { return isImmInRange(-128, 255, /*AllowSymbol=*/true); }
  bool isData16() const {
    return isImmInRange(-32768, 65535, /*AllowSymbol=*/true);
  }
  bool isBitAddr() const { return Kind == k_BitAddr; }
  // A symbol is allowed here: a bit variable's word number is the linker's
  // answer, so the byte is a relocation rather than a value the assembler can
  // range check.
  bool isBitOff() const { return isImmInRange(0, 255, /*AllowSymbol=*/true); }
  bool isMask8() const { return isImmInRange(0, 255, /*AllowSymbol=*/true); }
  bool isIrang2() const { return isImmInRange(1, 4, /*AllowSymbol=*/false); }
  bool isTrap7() const { return isImmInRange(0, 127, /*AllowSymbol=*/false); }
  bool isSeg8() const { return isImmInRange(0, 255, /*AllowSymbol=*/true); }
  bool isPag10() const { return isImmInRange(0, 1023, /*AllowSymbol=*/true); }

  MCRegister getReg() const override {
    assert((Kind == k_Register || Kind == k_SFR) && "Not a register operand");
    return Reg;
  }

  StringRef getToken() const {
    assert(Kind == k_Token && "Not a token operand");
    return Tok;
  }

  SMLoc getStartLoc() const override { return Start; }
  SMLoc getEndLoc() const override { return End; }

  static void addExpr(MCInst &Inst, const MCExpr *Expr) {
    if (auto *CE = dyn_cast<MCConstantExpr>(Expr))
      Inst.addOperand(MCOperand::createImm(CE->getValue()));
    else
      Inst.addOperand(MCOperand::createExpr(Expr));
  }

  void addRegOperands(MCInst &Inst, unsigned N) const {
    assert(N == 1 && "Invalid number of operands");
    Inst.addOperand(MCOperand::createReg(Reg));
  }

  void addImmOperands(MCInst &Inst, unsigned N) const {
    assert(N == 1 && "Invalid number of operands");
    if (Kind == k_CondCode)
      Inst.addOperand(MCOperand::createImm(CC));
    else
      addExpr(Inst, Imm);
  }

  void addBitAddrOperands(MCInst &Inst, unsigned N) const {
    assert(N == 2 && "Invalid number of operands");
    if (BitOffExpr)
      Inst.addOperand(MCOperand::createExpr(BitOffExpr));
    else
      Inst.addOperand(MCOperand::createImm(BitOff));
    Inst.addOperand(MCOperand::createImm(BitPos));
  }

  static std::unique_ptr<C166Operand> createBitAddr(int64_t Off, int64_t Pos,
                                                    SMLoc S, SMLoc E) {
    auto Op = std::make_unique<C166Operand>(k_BitAddr, S, E);
    Op->BitOff = Off;
    Op->BitPos = Pos;
    return Op;
  }

  static std::unique_ptr<C166Operand>
  createBitAddrSym(const MCExpr *Word, int64_t Pos, SMLoc S, SMLoc E) {
    auto Op = std::make_unique<C166Operand>(k_BitAddr, S, E);
    Op->BitOffExpr = Word;
    Op->BitPos = Pos;
    return Op;
  }

  static std::unique_ptr<C166Operand>
  createSFR(MCRegister Reg, const MCExpr *Addr, SMLoc S, SMLoc E) {
    auto Op = std::make_unique<C166Operand>(k_SFR, S, E);
    Op->Reg = Reg;
    Op->Imm = Addr;
    return Op;
  }

  void addMemRIOperands(MCInst &Inst, unsigned N) const {
    assert(N == 2 && "Invalid number of operands");
    Inst.addOperand(MCOperand::createReg(Mem.Base));
    addExpr(Inst, Mem.Disp);
  }

  void addMemROperands(MCInst &Inst, unsigned N) const {
    assert(N == 1 && "Invalid number of operands");
    Inst.addOperand(MCOperand::createReg(Mem.Base));
  }

  /// A pointer form is two machine operands, the register and what happens to
  /// it, which the encoder puts in two different places in the instruction.
  void addCoPtrOperands(MCInst &Inst, unsigned N) const {
    assert(N == 2 && "Invalid number of operands");
    Inst.addOperand(MCOperand::createReg(Mem.Base));
    Inst.addOperand(MCOperand::createImm(Mem.CoUpdate));
  }

  void addCoIdxOperands(MCInst &Inst, unsigned N) const {
    assert(N == 2 && "Invalid number of operands");
    Inst.addOperand(MCOperand::createReg(Mem.Base));
    Inst.addOperand(MCOperand::createImm(Mem.CoUpdate));
  }

  void print(raw_ostream &OS, const MCAsmInfo &MAI) const override {
    switch (Kind) {
    case k_Token:
      OS << "Token:" << Tok;
      break;
    case k_Register:
      OS << "Register:" << Reg.id();
      break;
    case k_Immediate:
      OS << "Immediate:";
      MAI.printExpr(OS, *Imm);
      break;
    case k_Address:
      OS << "Address:";
      MAI.printExpr(OS, *Imm);
      break;
    case k_SFR:
      OS << "SFR:" << Reg.id() << ':';
      MAI.printExpr(OS, *Imm);
      break;
    case k_BitAddr:
      OS << "BitAddr:";
      if (BitOffExpr)
        MAI.printExpr(OS, *BitOffExpr);
      else
        OS << BitOff;
      OS << '.' << BitPos;
      break;
    case k_CondCode:
      OS << "CondCode:" << CC;
      break;
    case k_Memory:
      OS << "Memory:[" << Mem.Base.id() << "+";
      MAI.printExpr(OS, *Mem.Disp);
      OS << "]";
      break;
    }
  }

  static std::unique_ptr<C166Operand> createToken(StringRef Str, SMLoc S) {
    auto Op = std::make_unique<C166Operand>(k_Token, S, S);
    Op->Tok = Str;
    return Op;
  }

  static std::unique_ptr<C166Operand> createReg(MCRegister Reg, SMLoc S,
                                                SMLoc E) {
    auto Op = std::make_unique<C166Operand>(k_Register, S, E);
    Op->Reg = Reg;
    return Op;
  }

  static std::unique_ptr<C166Operand> createImm(const MCExpr *Val, SMLoc S,
                                                SMLoc E) {
    auto Op = std::make_unique<C166Operand>(k_Immediate, S, E);
    Op->Imm = Val;
    return Op;
  }

  static std::unique_ptr<C166Operand> createAddr(const MCExpr *Val, SMLoc S,
                                                 SMLoc E) {
    auto Op = std::make_unique<C166Operand>(k_Address, S, E);
    Op->Imm = Val;
    return Op;
  }

  static std::unique_ptr<C166Operand> createCondCode(int64_t CC, SMLoc S,
                                                     SMLoc E) {
    auto Op = std::make_unique<C166Operand>(k_CondCode, S, E);
    Op->CC = CC;
    return Op;
  }

  static std::unique_ptr<C166Operand> createMem(MCRegister Base,
                                                const MCExpr *Disp,
                                                bool HasDisp, bool PostInc,
                                                bool PreDec, unsigned CoUpdate,
                                                bool IsIdx, SMLoc S, SMLoc E) {
    auto Op = std::make_unique<C166Operand>(k_Memory, S, E);
    Op->Mem.Base = Base;
    Op->Mem.Disp = Disp;
    Op->Mem.HasDisp = HasDisp;
    Op->Mem.PostInc = PostInc;
    Op->Mem.PreDec = PreDec;
    Op->Mem.CoUpdate = CoUpdate;
    Op->Mem.IsIdx = IsIdx;
    return Op;
  }
};

/// Parses C166 assembly from a stream.
class C166AsmParser : public MCTargetAsmParser {
  MCAsmParser &Parser;

  bool matchAndEmitInstruction(SMLoc IDLoc, unsigned &Opcode,
                               OperandVector &Operands, MCStreamer &Out,
                               uint64_t &ErrorInfo,
                               bool MatchingInlineAsm) override;

  bool parseBitOff(OperandVector &Operands);
  bool parseBitAddr(OperandVector &Operands);
  bool parseBitOffValue(int64_t &Off, StringRef &BitPosText,
                        const MCExpr *&Word);

  /// MatchRegisterName, less the registers this part does not have.
  ///
  /// The register file holds every derivative's special function register map
  /// at once, because the same short address names different registers on
  /// different parts.  Naming one from another part's map has to be refused
  /// rather than encoded: the bytes would assemble, and would mean something
  /// else on the part they were assembled for.
  MCRegister matchRegisterInMap(StringRef Name) const {
    MCRegister Reg = MatchRegisterName(Name.lower());
    if (Reg && !C166::isSFRInSelectedMap(Reg, getSTI()))
      return MCRegister();
    return Reg;
  }

  /// Refuse a name the register file knows but this part does not have.
  ///
  /// Every path that turns a name into a short address goes through here, and
  /// there are three of them - a register operand, a special function register
  /// reached by address, and a bit address.  Saying which is which is better
  /// than the operand diagnostic that would otherwise come out, and a name
  /// reaching none of them is not an error here at all, so this returns an
  /// empty message for anything it does not recognise.
  std::string reportRegisterNotInMap(SMLoc S, StringRef Name) const {
    MCRegister Reg = MatchRegisterName(Name.lower());
    if (!Reg || matchRegisterInMap(Name))
      return std::string();
    // The coprocessor's offset registers are missing for a different reason
    // than the rest: they are not another part's, they are the MAC unit's, and
    // what is absent is the unit.
    if (getC166MCRegisterClass(C166::CoOFFSRegClassID).contains(Reg))
      return ("'" + Name.lower() +
              "' is a register of the multiply-accumulate unit, which the "
              "selected processor does not have");
    return ("'" + Name.lower() +
            "' is not a register on the selected processor; it is another "
            "derivative's special function register");
  }

  bool parseRegister(MCRegister &Reg, SMLoc &StartLoc, SMLoc &EndLoc) override;
  ParseStatus tryParseRegister(MCRegister &Reg, SMLoc &StartLoc,
                               SMLoc &EndLoc) override;

  bool parseInstruction(ParseInstructionInfo &Info, StringRef Name,
                        SMLoc NameLoc, OperandVector &Operands) override;

  bool parseCoRepeatCount(const MCExpr *&Count, SMLoc &Loc);

  /// What a repeatable coprocessor instruction gets when it is written with
  /// no prefix, which is the plain form: a zero repeat field, the same bits
  /// every instruction here encoded before the prefix was accepted at all.
  std::unique_ptr<C166Operand> defaultCoRepeatOperands() {
    SMLoc L = getLexer().getLoc();
    return C166Operand::createImm(MCConstantExpr::create(0, getContext()), L, L);
  }

  bool parseOperand(OperandVector &Operands, StringRef Mnemonic);
  bool parseExpressionWithSpecifier(const MCExpr *&Res);
  bool reportOperandError(SMLoc IDLoc, OperandVector &Operands,
                          uint64_t ErrorInfo, const Twine &Msg);
  bool parseMemory(OperandVector &Operands);
  bool parseBracketedRegister(OperandVector &Operands);

  // Whether the statement being matched began with a repeat prefix, so that a
  // failure can say the instruction is not repeatable rather than blaming the
  // count.  Set by parseInstruction, read by matchAndEmitInstruction, which
  // runs immediately after it for the same statement.
  bool SawRepeatPrefix = false;

  // Mnemonics that had a trailing minus glued back on; see parseInstruction.
  // A list rather than a vector because the operands hold references into it.
  std::list<std::string> GluedMnemonics;

  MCAsmParser &getParser() const { return Parser; }
  AsmLexer &getLexer() const { return Parser.getLexer(); }

  /// @name Auto-generated Matcher Functions
  /// {

#define GET_ASSEMBLER_HEADER
#include "C166GenAsmMatcher.inc"

  /// }

public:
  enum C166MatchResultTy : unsigned {
    Match_Dummy = FIRST_TARGET_MATCH_RESULT_TY,
#define GET_OPERAND_DIAGNOSTIC_TYPES
#include "C166GenAsmMatcher.inc"
#undef GET_OPERAND_DIAGNOSTIC_TYPES
  };

  C166AsmParser(const MCSubtargetInfo &STI, MCAsmParser &Parser,
                const MCInstrInfo &MII)
      : MCTargetAsmParser(STI, MII), Parser(Parser) {
    MCAsmParserExtension::Initialize(Parser);
    setAvailableFeatures(ComputeAvailableFeatures(STI.getFeatureBits()));
  }
};

/// Only PUSH and POP name a special function register as a register; anywhere
/// else the name stands for the address it is mapped at, which is the table
/// C166MCTargetDesc holds so that the disassembler can print the same names
/// back.
static int64_t matchSpecialFunctionRegister(const MCRegisterInfo &MRI,
                                            StringRef Name) {
  return C166::getSFRAddress(MRI, Name);
}

/// The 8 bit "bitoff" word address that names a word in the bit-addressable
/// space, or -1 when the name is not a bit-addressable word.  General purpose
/// registers are F0H + n; a special function register keeps the short address
/// it already carries, but only the ones from FF00H to FFDEH are bit
/// addressable at all - FE00H to FEFEH, where MDL, MDH, CP, SP and the DPPs
/// live, is not.
static int64_t matchBitAddressableWord(const MCRegisterInfo &MRI,
                                       StringRef Name) {
  std::string Lowered = Name.lower();
  StringRef Number = Lowered;
  if (Number.consume_front("r")) {
    unsigned N;
    if (!Number.getAsInteger(10, N) && N < 16)
      return 0xF0 + N;
    return -1;
  }

  int64_t Short = C166::getSFRShortAddress(MRI, Name);
  if (Short < 0 || !C166::isSFRBitAddressable(Short))
    return -1;
  return Short;
}

/// Map a cc_XX mnemonic onto its encoding.
static int64_t matchCondCode(StringRef Name) {
  for (int64_t CC = 0; CC <= 0xF; ++CC) {
    const char *Text =
        C166CC::getCondCodeName(static_cast<C166CC::CondCode>(CC));
    if (Text && Name.equals_insensitive(Text))
      return CC;
  }

  // The architecture gives several encodings two names; getCondCodeName only
  // returns one of each pair, so the other names come from the list beside it.
  for (auto [Alias, CC] : C166CC::getCondCodeAliases())
    if (Name.equals_insensitive(Alias))
      return CC;
  return -1;
}

bool C166AsmParser::parseBracketedRegister(OperandVector &Operands) {
  // The indirect jump and call forms print literal brackets around a plain
  // register: "jmpi cc_UC, [r5]".
  SMLoc S = getLexer().getLoc();
  Operands.push_back(C166Operand::createToken("[", S));
  Lex();

  MCRegister Reg;
  SMLoc RegStart = getLexer().getLoc();
  SMLoc RegEnd;
  if (parseRegister(Reg, RegStart, RegEnd))
    return Error(RegStart, "expected a register");
  Operands.push_back(C166Operand::createReg(Reg, RegStart, RegEnd));

  if (getLexer().isNot(AsmToken::RBrac))
    return Error(getLexer().getLoc(), "expected ']'");
  Operands.push_back(C166Operand::createToken("]", getLexer().getLoc()));
  Lex();
  return false;
}

bool C166AsmParser::parseMemory(OperandVector &Operands) {
  // [Rw] or [Rw + #data16]
  SMLoc S = getLexer().getLoc();
  Lex(); // eat '['

  // "[-Rw]" steps the pointer back before writing through it.  It is the only
  // auto-stepping store form, which is why there is no "[Rw-]" to go with the
  // post-incrementing load.
  bool PreDec = false;
  if (getLexer().is(AsmToken::Minus)) {
    Lex(); // eat '-'
    PreDec = true;
  }

  MCRegister Base;
  SMLoc RegStart = getLexer().getLoc();
  SMLoc RegEnd;
  if (parseRegister(Base, RegStart, RegEnd))
    return Error(RegStart, "expected a base register");

  bool IsIdx = Base == C166::IDX0 || Base == C166::IDX1;

  const MCExpr *Disp = MCConstantExpr::create(0, getContext());
  bool HasDisp = false;
  bool PostInc = false;
  // 1 is "leave the pointer alone", which is what a plain "[Rw]" means to the
  // MAC unit and what every other instruction does anyway.
  unsigned CoUpdate = 1;

  // The offset registers a pointer can be stepped by are the MAC unit's, and
  // which pair depends on which pointer: IDX0 and IDX1 step by QX0 and QX1,
  // a general purpose register by QR0 and QR1.
  auto ParseStep = [&](bool Minus) -> bool {
    if (getLexer().is(AsmToken::RBrac)) {
      CoUpdate = Minus ? 3 : 2;
      return false;
    }
    if (getLexer().isNot(AsmToken::Identifier))
      return Error(getLexer().getLoc(),
                   Minus ? "expected an offset register"
                         : "expected '#' before a displacement");
    StringRef Name = getLexer().getTok().getIdentifier().lower();
    StringRef Want0 = IsIdx ? "qx0" : "qr0";
    StringRef Want1 = IsIdx ? "qx1" : "qr1";
    if (Name == Want0)
      CoUpdate = Minus ? 5 : 4;
    else if (Name == Want1)
      CoUpdate = Minus ? 7 : 6;
    else
      return Error(getLexer().getLoc(),
                   IsIdx ? "expected qx0 or qx1" : "expected qr0 or qr1");
    Lex();
    return false;
  };

  if (getLexer().is(AsmToken::Plus)) {
    Lex(); // eat '+'
    // "[Rw+]" steps the pointer past what was read rather than adding
    // anything to it.
    if (getLexer().is(AsmToken::RBrac)) {
      PostInc = true;
      CoUpdate = 2;
    } else if (getLexer().is(AsmToken::Hash)) {
      Lex(); // eat '#'
      if (parseExpressionWithSpecifier(Disp))
        return true;
      HasDisp = true;
    } else if (ParseStep(/*Minus=*/false)) {
      return true;
    }
  } else if (getLexer().is(AsmToken::Minus)) {
    Lex(); // eat '-'
    if (ParseStep(/*Minus=*/true))
      return true;
  }

  if (getLexer().isNot(AsmToken::RBrac))
    return Error(getLexer().getLoc(), "expected ']'");
  SMLoc E = getLexer().getLoc();
  Lex();

  Operands.push_back(C166Operand::createMem(Base, Disp, HasDisp, PostInc,
                                            PreDec, CoUpdate, IsIdx, S, E));
  return false;
}

/// Parse an expression, allowing one of the seg/sof/pag/pof operators around
/// it.  They pick a field out of a 24 bit address and turn into a relocation
/// of their own, so they are only recognised at the top of an operand rather
/// than as something that could appear inside arithmetic.
bool C166AsmParser::parseExpressionWithSpecifier(const MCExpr *&Res) {
  if (getLexer().is(AsmToken::Identifier)) {
    C166::Specifier Spec =
        C166::parseSpecifier(getLexer().getTok().getIdentifier());
    if (Spec != C166::S_None && getLexer().peekTok().is(AsmToken::LParen)) {
      SMLoc S = getLexer().getLoc();
      Lex(); // eat the operator
      Lex(); // eat '('
      const MCExpr *Sub;
      if (getParser().parseExpression(Sub))
        return true;
      if (getLexer().isNot(AsmToken::RParen))
        return Error(getLexer().getLoc(), "expected ')'");
      Lex();
      Res = MCSpecifierExpr::create(Sub, Spec, getContext(), S);
      return false;
    }
  }
  return getParser().parseExpression(Res);
}

/// How many of an instruction's leading operands are bit addresses.  The bit
/// test branches take one and then a relative target; the two operand bit
/// instructions take two.
static unsigned countBitAddrOperands(StringRef Mnemonic) {
  return StringSwitch<unsigned>(Mnemonic)
      .Cases({"bclr", "bset", "jb", "jbc", "jnb", "jnbs"}, 1)
      .Cases({"band", "bor", "bxor", "bmov", "bmovn", "bcmp"}, 2)
      .Default(0);
}

/// Read the word half of a bit address.  The assembly lexer folds '.' into an
/// identifier, so "psw.3" arrives as a single token and the bit position has
/// to be split back off here; it is returned as text for the caller to parse,
/// empty when the token did not carry one.
bool C166AsmParser::parseBitOffValue(int64_t &Off, StringRef &BitPosText,
                                     const MCExpr *&Word) {
  BitPosText = StringRef();
  Word = nullptr;
  SMLoc S = getLexer().getLoc();

  if (getLexer().is(AsmToken::Identifier)) {
    StringRef Name = getLexer().getTok().getIdentifier();
    StringRef WordName = Name;
    if (size_t Dot = Name.rfind('.'); Dot != StringRef::npos) {
      WordName = Name.take_front(Dot);
      BitPosText = Name.drop_front(Dot + 1);
    }

    if (std::string Msg = reportRegisterNotInMap(S, WordName); !Msg.empty())
      return Error(S, Msg);

    Off = matchBitAddressableWord(*getContext().getRegisterInfo(), WordName);
    if (Off >= 0) {
      Lex();
      return false;
    }

    // A name the register file knows is a register, and a register that is not
    // bit addressable is an error rather than a symbol: MDL is at FE0EH, which
    // has a short address and no bitoff, and reading it as an undefined symbol
    // would turn that into a link error about a name the program never used.
    if (MatchRegisterName(WordName.lower()))
      return Error(S, "not a bit-addressable word");

    // Otherwise it is a symbol - a variable placed in the bit-addressable RAM,
    // which is what __bitaddr does.  Which word of that space it is is not
    // known until it has an address, so the byte is left to a relocation.
    Word = MCSymbolRefExpr::create(getContext().getOrCreateSymbol(WordName),
                                   getContext());
    Lex();
    return false;
  }

  // A decimal word written up against its bit position, "136.10", is lexed as
  // a floating point literal; take that apart rather than reject it.  A hex
  // one cannot be: "0x88.15" is a lexer error before it reaches here, so it
  // has to be written with the '.' spaced out.
  if (getLexer().is(AsmToken::Real)) {
    StringRef Text = getLexer().getTok().getString();
    StringRef Word = Text.take_front(Text.find('.'));
    BitPosText = Text.drop_front(Word.size() + 1);
    if (Word.getAsInteger(10, Off) || Off < 0 || Off > 0xFF)
      return Error(S, "expected a bit-addressable word");
    Lex();
    return false;
  }

  // Otherwise it is the bit-addressable word number itself, which is what the
  // instruction encodes.
  const MCExpr *Expr;
  if (getParser().parseExpression(Expr))
    return true;
  const auto *CE = dyn_cast<MCConstantExpr>(Expr);
  if (!CE || CE->getValue() < 0 || CE->getValue() > 0xFF)
    return Error(S, "expected a bit-addressable word");
  Off = CE->getValue();
  return false;
}

bool C166AsmParser::parseBitOff(OperandVector &Operands) {
  SMLoc S = getLexer().getLoc();
  int64_t Off;
  StringRef BitPosText;
  const MCExpr *Word;
  if (parseBitOffValue(Off, BitPosText, Word))
    return true;
  if (!BitPosText.empty())
    return Error(S, "expected a bit-addressable word, not a bit address");

  // A symbol goes through as itself; the byte it becomes is the linker's,
  // because the mapping from address to bitoff depends on the address.
  Operands.push_back(C166Operand::createImm(
      Word ? Word : MCConstantExpr::create(Off, getContext()), S,
      getLexer().getLoc()));
  return false;
}

bool C166AsmParser::parseBitAddr(OperandVector &Operands) {
  SMLoc S = getLexer().getLoc();
  int64_t Off;
  StringRef BitPosText;
  const MCExpr *Word;
  if (parseBitOffValue(Off, BitPosText, Word))
    return true;

  // A numeric word leaves the '.' and the position behind as separate tokens.
  if (BitPosText.empty()) {
    if (getLexer().isNot(AsmToken::Dot))
      return Error(getLexer().getLoc(), "expected '.' and a bit position");
    Lex();
    if (getLexer().isNot(AsmToken::Integer))
      return Error(getLexer().getLoc(), "expected a bit position");
    BitPosText = getLexer().getTok().getString();
    Lex();
  }

  unsigned Pos;
  if (BitPosText.getAsInteger(10, Pos) || Pos > 15)
    return Error(S, "expected a bit position in [0, 15]");

  Operands.push_back(
      Word ? C166Operand::createBitAddrSym(Word, Pos, S, getLexer().getLoc())
           : C166Operand::createBitAddr(Off, Pos, S, getLexer().getLoc()));
  return false;
}

bool C166AsmParser::parseOperand(OperandVector &Operands, StringRef Mnemonic) {
  SMLoc S = getLexer().getLoc();

  // Operands.size() counts the mnemonic, so it is one more than the index of
  // the operand about to be parsed.
  if (Operands.size() <= countBitAddrOperands(Mnemonic))
    return parseBitAddr(Operands);
  if (Operands.size() == 1 && (Mnemonic == "bfldl" || Mnemonic == "bfldh"))
    return parseBitOff(Operands);

  switch (getLexer().getKind()) {
  case AsmToken::Hash: {
    Lex(); // eat '#'
    const MCExpr *Val;
    if (parseExpressionWithSpecifier(Val))
      return true;
    Operands.push_back(C166Operand::createImm(Val, S, getLexer().getLoc()));
    return false;
  }
  case AsmToken::LBrac:
    // Only the indirect jump and call forms spell out the brackets; every
    // other bracketed operand is a memory reference.
    if (Mnemonic == "jmpi" || Mnemonic == "calli")
      return parseBracketedRegister(Operands);
    return parseMemory(Operands);
  case AsmToken::Identifier: {
    StringRef Name = getLexer().getTok().getIdentifier();

    // The MAC unit's rounding forms are spelled with a word after the
    // operands rather than in the mnemonic, so it reaches here as an operand
    // and the matcher wants it as the literal it is.
    if (Name.equals_insensitive("rnd") &&
        Mnemonic.starts_with_insensitive("co")) {
      SMLoc E = getLexer().getLoc();
      Lex();
      Operands.push_back(C166Operand::createToken("rnd", S));
      return false;
    }

    if (Name.starts_with_insensitive("cc_")) {
      int64_t CC = matchCondCode(Name);
      if (CC < 0)
        return Error(S, "invalid condition code");
      SMLoc E = getLexer().getLoc();
      Lex();
      Operands.push_back(C166Operand::createCondCode(CC, S, E));
      return false;
    }

    // A name the register file knows but this part does not have is an error
    // rather than a symbol.  It cannot become one anyway - every register name
    // is reserved, whichever map it is in.
    if (std::string Msg = reportRegisterNotInMap(S, Name); !Msg.empty())
      return Error(S, Msg);

    if (int64_t Addr =
            matchSpecialFunctionRegister(*getContext().getRegisterInfo(), Name);
        Addr >= 0) {
      SMLoc E = getLexer().getLoc();
      Lex();
      // The address came from the register file, so there is a register behind
      // every name that gets here; the fallback stands in case one is ever
      // known by address alone.
      MCRegister Reg = matchRegisterInMap(Name);
      const MCExpr *AddrExpr = MCConstantExpr::create(Addr, getContext());
      Operands.push_back(Reg ? C166Operand::createSFR(Reg, AddrExpr, S, E)
                             : C166Operand::createAddr(AddrExpr, S, E));
      return false;
    }

    if (MCRegister Reg = matchRegisterInMap(Name)) {
      SMLoc E = getLexer().getLoc();
      Lex();
      Operands.push_back(C166Operand::createReg(Reg, S, E));
      return false;
    }
    break;
  }
  default:
    break;
  }

  // Anything else is an address: a symbol, a number or an expression.
  const MCExpr *Val;
  if (parseExpressionWithSpecifier(Val))
    return true;
  Operands.push_back(C166Operand::createAddr(Val, S, getLexer().getLoc()));
  return false;
}

bool C166AsmParser::parseInstruction(ParseInstructionInfo &Info, StringRef Name,
                                     SMLoc NameLoc, OperandVector &Operands) {
  // Several of the MAC unit's mnemonics end in a minus - CoMAC- negates what
  // it accumulates - which the lexer hands over as a separate token.  Glue it
  // back on when it is the next thing and there is nothing between, so that
  // the name is what the manual writes.
  // The minus has to be the very next character rather than merely the next
  // token, which is what says it belongs to the name rather than being an
  // operand.  The glued name is kept by the parser because the operand holds
  // it as a reference.
  if (getLexer().is(AsmToken::Minus) && Name.starts_with_insensitive("co") &&
      getLexer().getTok().getLoc().getPointer() ==
          NameLoc.getPointer() + Name.size()) {
    GluedMnemonics.emplace_back((Name + "-").str());
    Name = GluedMnemonics.back();
    Lex();
  }

  // "Repeat 3 times CoMAC ..." and "Repeat MRW times CoMAC ...", the two forms
  // PM0036 2.4.7 gives.  The prefix puts two tokens in front of the mnemonic
  // and the matcher keys on the mnemonic being first, so it is taken apart
  // here: the count is remembered, the real mnemonic becomes Name, and the
  // instruction is then parsed exactly as if it stood alone.  The count goes
  // on the end afterwards, which is where its operand is.
  const MCExpr *Repeat = nullptr;
  SMLoc RepeatLoc;
  SawRepeatPrefix = false;
  if (Name.equals_insensitive("repeat")) {
    SawRepeatPrefix = true;
    RepeatLoc = getLexer().getLoc();
    if (parseCoRepeatCount(Repeat, RepeatLoc))
      return true;
    if (getLexer().isNot(AsmToken::Identifier) ||
        !getLexer().getTok().getIdentifier().equals_insensitive("times"))
      return Error(getLexer().getLoc(), "expected 'times' after a repeat count");
    Lex();
    if (getLexer().isNot(AsmToken::Identifier))
      return Error(getLexer().getLoc(),
                   "expected a coprocessor instruction after 'repeat'");
    NameLoc = getLexer().getLoc();
    GluedMnemonics.emplace_back(getLexer().getTok().getIdentifier().str());
    Name = GluedMnemonics.back();
    Lex();
    // The same trailing minus the glue above handles, now that the mnemonic
    // has moved: "Repeat 3 times CoMAC- R3, [R7-]".
    if (getLexer().is(AsmToken::Minus) &&
        getLexer().getTok().getLoc().getPointer() ==
            NameLoc.getPointer() + Name.size()) {
      GluedMnemonics.emplace_back((Name + "-").str());
      Name = GluedMnemonics.back();
      Lex();
    }
  }

  Operands.push_back(C166Operand::createToken(Name, NameLoc));

  if (getLexer().isNot(AsmToken::EndOfStatement)) {
    if (parseOperand(Operands, Name))
      return true;

    while (getLexer().is(AsmToken::Comma)) {
      Lex();
      if (parseOperand(Operands, Name))
        return true;
    }

    if (getLexer().isNot(AsmToken::EndOfStatement))
      return Error(getLexer().getLoc(), "unexpected token in operand list");
  }

  if (Repeat)
    Operands.push_back(
        C166Operand::createImm(Repeat, RepeatLoc, getLexer().getLoc()));

  return false;
}

/// The count in "Repeat <count> times", which is either MRW or a literal.
///
/// MRW encodes as 1 and a literal encodes as itself, so 0 and 1 are both
/// refused: 0 is the plain form, which is written by leaving the prefix off,
/// and 1 would mean MRW rather than once.  Refusing them is what keeps the
/// one thing PM0036 does not state outright - how a literal maps onto the
/// field - from being guessed at in either direction.
bool C166AsmParser::parseCoRepeatCount(const MCExpr *&Count, SMLoc &Loc) {
  Loc = getLexer().getLoc();
  if (getLexer().is(AsmToken::Identifier) &&
      getLexer().getTok().getIdentifier().equals_insensitive("mrw")) {
    Lex();
    Count = MCConstantExpr::create(1, getContext());
    return false;
  }

  int64_t Value;
  if (getParser().parseAbsoluteExpression(Value))
    return true;
  if (Value < 2 || Value > 31)
    return Error(Loc, "repeat count must be in the range [2, 31], or MRW; "
                      "a count of 1 is the instruction without the prefix");
  Count = MCConstantExpr::create(Value, getContext());
  return false;
}

ParseStatus C166AsmParser::tryParseRegister(MCRegister &Reg, SMLoc &StartLoc,
                                            SMLoc &EndLoc) {
  if (getLexer().isNot(AsmToken::Identifier))
    return ParseStatus::NoMatch;

  StringRef Name = getLexer().getTok().getIdentifier();
  Reg = matchRegisterInMap(Name);
  if (!Reg)
    return ParseStatus::NoMatch;

  StartLoc = getLexer().getLoc();
  Lex();
  EndLoc = getLexer().getLoc();
  return ParseStatus::Success;
}

bool C166AsmParser::parseRegister(MCRegister &Reg, SMLoc &StartLoc,
                                  SMLoc &EndLoc) {
  return !tryParseRegister(Reg, StartLoc, EndLoc).isSuccess();
}

} // end anonymous namespace

#define GET_REGISTER_MATCHER
#define GET_MATCHER_IMPLEMENTATION
#include "C166GenAsmMatcher.inc"

bool C166AsmParser::reportOperandError(SMLoc IDLoc, OperandVector &Operands,
                                       uint64_t ErrorInfo, const Twine &Msg) {
  SMLoc ErrorLoc = IDLoc;
  if (ErrorInfo != ~0ULL) {
    if (ErrorInfo >= Operands.size())
      return Error(IDLoc, "too few operands for instruction");
    ErrorLoc = ((C166Operand &)*Operands[ErrorInfo]).getStartLoc();
    if (ErrorLoc == SMLoc())
      ErrorLoc = IDLoc;
  }
  return Error(ErrorLoc, Msg);
}

bool C166AsmParser::matchAndEmitInstruction(SMLoc IDLoc, unsigned &Opcode,
                                            OperandVector &Operands,
                                            MCStreamer &Out,
                                            uint64_t &ErrorInfo,
                                            bool MatchingInlineAsm) {
  MCInst Inst;
  unsigned Result =
      MatchInstructionImpl(Operands, Inst, ErrorInfo, MatchingInlineAsm);

  switch (Result) {
  case Match_Success:
    // The shifter authorizes only 8 bit shifts and the count field is five
    // bits wide, so the range is the shifter's rather than the field's:
    // PM0036 2.4.8, "shift values must be between 0-8 (inclusive)".  Checked
    // here rather than by the operand class so that the diagnostic names the
    // count, which is what was written; see CoShiftAsmOperand.
    if (Inst.getNumOperands() &&
        (this->MII.get(Inst.getOpcode()).TSFlags & 2)) {
      const MCOperand &S = Inst.getOperand(0);
      if (S.isImm() && (S.getImm() < 0 || S.getImm() > 8))
        return Error(IDLoc, "shift count must be in the range [0, 8]");
    }
    Inst.setLoc(IDLoc);
    Out.emitInstruction(Inst, getSTI());
    return false;
  case Match_MnemonicFail:
    return Error(IDLoc, "invalid instruction mnemonic");
  case Match_InvalidOperand:
    // A repeat prefix on a form the manual does not mark repeatable fails
    // here, because the count has nowhere to go: only the eighty nine
    // repeatable forms carry that operand.  Saying which it was beats the
    // operand diagnostic, which would point at the count and call it invalid
    // when the count is the one thing that was fine.
    if (SawRepeatPrefix && ErrorInfo + 1 == Operands.size())
      return Error(IDLoc,
                   "this instruction cannot be repeated; the manual's Rep "
                   "column marks it no, and it has no repeat field");
    return reportOperandError(IDLoc, Operands, ErrorInfo,
                              "invalid operand for instruction");
  case Match_MissingFeature:
    // The mnemonic exists but not on the part being assembled for.  Naming the
    // feature rather than the part is what the -mattr= spelling wants, and the
    // processor list in C166.td says which processors have it.
    return Error(IDLoc, "instruction requires a feature the selected processor "
                        "does not have");
  default:
    // Anything else is one of the per-operand diagnostics TableGen generated
    // from a DiagnosticString.
    if (const char *Diag = getMatchKindDiag((C166MatchResultTy)Result))
      return reportOperandError(IDLoc, Operands, ErrorInfo, Diag);
    break;
  }

  llvm_unreachable("Unknown match result");
}

extern "C" LLVM_ABI LLVM_EXTERNAL_VISIBILITY void
LLVMInitializeC166AsmParser() {
  RegisterMCAsmParser<C166AsmParser> X(getTheC166Target());
}
