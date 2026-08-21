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
#include "llvm/ADT/StringSwitch.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCInstrInfo.h"
#include "llvm/MC/MCParser/AsmLexer.h"
#include "llvm/MC/MCParser/MCParsedAsmOperand.h"
#include "llvm/MC/MCParser/MCTargetAsmParser.h"
#include "llvm/MC/MCRegisterInfo.h"
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
  } Kind;

  struct MemoryOp {
    MCRegister Base;
    const MCExpr *Disp;
  };

  StringRef Tok;
  MCRegister Reg;
  const MCExpr *Imm = nullptr;
  int64_t CC = 0;
  MemoryOp Mem;

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
  bool isMemRI() const { return Kind == k_Memory; }

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

  bool isImm4() const { return isImmInRange(0, 15, /*AllowSymbol=*/false); }
  bool isData8() const { return isImmInRange(-128, 255, /*AllowSymbol=*/true); }
  bool isData16() const {
    return isImmInRange(-32768, 65535, /*AllowSymbol=*/true);
  }
  bool isIrang2() const { return isImmInRange(1, 4, /*AllowSymbol=*/false); }
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

  static std::unique_ptr<C166Operand>
  createMem(MCRegister Base, const MCExpr *Disp, SMLoc S, SMLoc E) {
    auto Op = std::make_unique<C166Operand>(k_Memory, S, E);
    Op->Mem.Base = Base;
    Op->Mem.Disp = Disp;
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

  bool parseRegister(MCRegister &Reg, SMLoc &StartLoc, SMLoc &EndLoc) override;
  ParseStatus tryParseRegister(MCRegister &Reg, SMLoc &StartLoc,
                               SMLoc &EndLoc) override;

  bool parseInstruction(ParseInstructionInfo &Info, StringRef Name,
                        SMLoc NameLoc, OperandVector &Operands) override;

  bool parseOperand(OperandVector &Operands, StringRef Mnemonic);
  bool parseExpressionWithSpecifier(const MCExpr *&Res);
  bool reportOperandError(SMLoc IDLoc, OperandVector &Operands,
                          uint64_t ErrorInfo, const Twine &Msg);
  bool parseMemory(OperandVector &Operands);
  bool parseBracketedRegister(OperandVector &Operands);

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

/// The special function registers are memory mapped, and an assembler is
/// expected to know where.  Only PUSH and POP name one as a register; anywhere
/// else an SFR name stands for its address.  Values are from the C166 User's
/// Manual register table (and agree with the 8 bit short register addresses
/// the registers carry as their hardware encoding).
static int64_t matchSpecialFunctionRegister(StringRef Name) {
  return StringSwitch<int64_t>(Name.lower())
      .Case("dpp0", 0xFE00)
      .Case("dpp1", 0xFE02)
      .Case("dpp2", 0xFE04)
      .Case("dpp3", 0xFE06)
      .Case("mdh", 0xFE0C)
      .Case("mdl", 0xFE0E)
      .Case("cp", 0xFE10)
      .Case("sp", 0xFE12)
      .Case("mdc", 0xFF0E)
      .Case("psw", 0xFF10)
      .Default(-1);
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
  // returns one of each pair, so match the aliases here.
  return StringSwitch<int64_t>(Name.lower())
      .Case("cc_z", C166CC::COND_Z)
      .Case("cc_nz", C166CC::COND_NZ)
      .Case("cc_c", C166CC::COND_ULT)
      .Case("cc_nc", C166CC::COND_UGE)
      .Default(-1);
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

  MCRegister Base;
  SMLoc RegStart = getLexer().getLoc();
  SMLoc RegEnd;
  if (parseRegister(Base, RegStart, RegEnd))
    return Error(RegStart, "expected a base register");

  const MCExpr *Disp = MCConstantExpr::create(0, getContext());
  if (getLexer().is(AsmToken::Plus)) {
    Lex(); // eat '+'
    if (getLexer().isNot(AsmToken::Hash))
      return Error(getLexer().getLoc(), "expected '#' before a displacement");
    Lex(); // eat '#'
    if (parseExpressionWithSpecifier(Disp))
      return true;
  }

  if (getLexer().isNot(AsmToken::RBrac))
    return Error(getLexer().getLoc(), "expected ']'");
  SMLoc E = getLexer().getLoc();
  Lex();

  Operands.push_back(C166Operand::createMem(Base, Disp, S, E));
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

bool C166AsmParser::parseOperand(OperandVector &Operands, StringRef Mnemonic) {
  SMLoc S = getLexer().getLoc();

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

    if (Name.starts_with_insensitive("cc_")) {
      int64_t CC = matchCondCode(Name);
      if (CC < 0)
        return Error(S, "invalid condition code");
      SMLoc E = getLexer().getLoc();
      Lex();
      Operands.push_back(C166Operand::createCondCode(CC, S, E));
      return false;
    }

    if (int64_t Addr = matchSpecialFunctionRegister(Name); Addr >= 0) {
      SMLoc E = getLexer().getLoc();
      Lex();
      // The backend does not model every special function register, so one it
      // knows only an address for stays a plain address operand.
      MCRegister Reg = MatchRegisterName(Name.lower());
      const MCExpr *AddrExpr = MCConstantExpr::create(Addr, getContext());
      Operands.push_back(Reg ? C166Operand::createSFR(Reg, AddrExpr, S, E)
                             : C166Operand::createAddr(AddrExpr, S, E));
      return false;
    }

    if (MCRegister Reg = MatchRegisterName(Name.lower())) {
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
  Operands.push_back(C166Operand::createToken(Name, NameLoc));

  if (getLexer().is(AsmToken::EndOfStatement))
    return false;

  if (parseOperand(Operands, Name))
    return true;

  while (getLexer().is(AsmToken::Comma)) {
    Lex();
    if (parseOperand(Operands, Name))
      return true;
  }

  if (getLexer().isNot(AsmToken::EndOfStatement))
    return Error(getLexer().getLoc(), "unexpected token in operand list");

  return false;
}

ParseStatus C166AsmParser::tryParseRegister(MCRegister &Reg, SMLoc &StartLoc,
                                            SMLoc &EndLoc) {
  if (getLexer().isNot(AsmToken::Identifier))
    return ParseStatus::NoMatch;

  StringRef Name = getLexer().getTok().getIdentifier();
  Reg = MatchRegisterName(Name.lower());
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
    Inst.setLoc(IDLoc);
    Out.emitInstruction(Inst, getSTI());
    return false;
  case Match_MnemonicFail:
    return Error(IDLoc, "invalid instruction mnemonic");
  case Match_InvalidOperand:
    return reportOperandError(IDLoc, Operands, ErrorInfo,
                              "invalid operand for instruction");
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
