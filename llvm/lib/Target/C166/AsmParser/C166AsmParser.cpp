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
    k_BitAddr,
  } Kind;

  struct MemoryOp {
    MCRegister Base;
    const MCExpr *Disp;
    // "[Rw]", "[Rw + #0]" and "[Rw+]" are three different instructions, so
    // which one was written has to survive parsing.
    bool HasDisp;
    bool PostInc;
  };

  StringRef Tok;
  MCRegister Reg;
  const MCExpr *Imm = nullptr;
  int64_t CC = 0;
  MemoryOp Mem;
  int64_t BitOff = 0;
  int64_t BitPos = 0;

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
    return Kind == k_Memory && !Mem.HasDisp && !Mem.PostInc;
  }
  bool isMemRI() const { return Kind == k_Memory && Mem.HasDisp; }
  bool isMemRPostInc() const { return Kind == k_Memory && Mem.PostInc; }

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
  bool isData8() const { return isImmInRange(-128, 255, /*AllowSymbol=*/true); }
  bool isData16() const {
    return isImmInRange(-32768, 65535, /*AllowSymbol=*/true);
  }
  bool isBitAddr() const { return Kind == k_BitAddr; }
  bool isBitOff() const { return isImmInRange(0, 255, /*AllowSymbol=*/false); }
  bool isMask8() const { return isImmInRange(0, 255, /*AllowSymbol=*/true); }
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

  void addBitAddrOperands(MCInst &Inst, unsigned N) const {
    assert(N == 2 && "Invalid number of operands");
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
      OS << "BitAddr:" << BitOff << '.' << BitPos;
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
                                                SMLoc S, SMLoc E) {
    auto Op = std::make_unique<C166Operand>(k_Memory, S, E);
    Op->Mem.Base = Base;
    Op->Mem.Disp = Disp;
    Op->Mem.HasDisp = HasDisp;
    Op->Mem.PostInc = PostInc;
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
  bool parseBitOffValue(int64_t &Off, StringRef &BitPosText);

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

/// The 8 bit "bitoff" word address that names a word in the bit-addressable
/// space, or -1 when the name is not a bit-addressable word.  General purpose
/// registers are F0H + n; a special function register keeps the short address
/// it already carries, but only the ones from FF00H to FFDEH are bit
/// addressable at all - FE00H to FEFEH, where MDL, MDH, CP, SP and the DPPs
/// live, is not.
static int64_t matchBitAddressableWord(StringRef Name) {
  std::string Lowered = Name.lower();
  StringRef Number = Lowered;
  if (Number.consume_front("r")) {
    unsigned N;
    if (!Number.getAsInteger(10, N) && N < 16)
      return 0xF0 + N;
    return -1;
  }

  int64_t Addr = matchSpecialFunctionRegister(Name);
  if (Addr < 0xFF00 || Addr > 0xFFDE)
    return -1;
  return 0x80 + ((Addr - 0xFF00) / 2);
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
  bool HasDisp = false;
  bool PostInc = false;
  if (getLexer().is(AsmToken::Plus)) {
    Lex(); // eat '+'
    // "[Rw+]" steps the pointer past what was read rather than adding
    // anything to it.
    if (getLexer().is(AsmToken::RBrac)) {
      PostInc = true;
    } else {
      if (getLexer().isNot(AsmToken::Hash))
        return Error(getLexer().getLoc(), "expected '#' before a displacement");
      Lex(); // eat '#'
      if (parseExpressionWithSpecifier(Disp))
        return true;
      HasDisp = true;
    }
  }

  if (getLexer().isNot(AsmToken::RBrac))
    return Error(getLexer().getLoc(), "expected ']'");
  SMLoc E = getLexer().getLoc();
  Lex();

  Operands.push_back(
      C166Operand::createMem(Base, Disp, HasDisp, PostInc, S, E));
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
bool C166AsmParser::parseBitOffValue(int64_t &Off, StringRef &BitPosText) {
  BitPosText = StringRef();
  SMLoc S = getLexer().getLoc();

  if (getLexer().is(AsmToken::Identifier)) {
    StringRef Name = getLexer().getTok().getIdentifier();
    StringRef Word = Name;
    if (size_t Dot = Name.rfind('.'); Dot != StringRef::npos) {
      Word = Name.take_front(Dot);
      BitPosText = Name.drop_front(Dot + 1);
    }

    Off = matchBitAddressableWord(Word);
    if (Off < 0)
      return Error(S, "not a bit-addressable word");
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
  if (parseBitOffValue(Off, BitPosText))
    return true;
  if (!BitPosText.empty())
    return Error(S, "expected a bit-addressable word, not a bit address");

  Operands.push_back(C166Operand::createImm(
      MCConstantExpr::create(Off, getContext()), S, getLexer().getLoc()));
  return false;
}

bool C166AsmParser::parseBitAddr(OperandVector &Operands) {
  SMLoc S = getLexer().getLoc();
  int64_t Off;
  StringRef BitPosText;
  if (parseBitOffValue(Off, BitPosText))
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
      C166Operand::createBitAddr(Off, Pos, S, getLexer().getLoc()));
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
