//===-- Execute.cpp - Decode and run one C166 instruction ----------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Decoding is done by the target's own MCDisassembler, so this cannot decode
// an instruction differently from the way the backend encodes it, and it can
// only run what the backend models.  Dispatch is on the instruction's TableGen
// name, which is why there is a table mapping those to the enum below: it
// avoids including the target's generated headers from outside the target.
//
// The semantics are from the C166 Family Instruction Set Manual (V2.0,
// 2001-03).  Two of them are worth pointing at because they are not what a
// reader used to other machines would assume:
//
//   * SUB, SUBC, CMP and NEG set C when a borrow is generated, so C is set
//     exactly when an unsigned subtraction wrapped.  It is not an inverted
//     borrow.
//   * ADDC and SUBC set Z only if the result is zero AND Z was already set,
//     which is how a multi-word sequence accumulates one zero test.
//
//===----------------------------------------------------------------------===//

#include "Machine.h"
#include "llvm/MC/MCAsmInfo.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCDisassembler/MCDisassembler.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCInstPrinter.h"
#include "llvm/MC/MCInstrInfo.h"
#include "llvm/MC/MCRegisterInfo.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/Format.h"
#include "llvm/Support/raw_ostream.h"
#include <map>
#include <memory>

using namespace llvm;
using namespace c166sim;

namespace {

/// The instructions this simulator knows how to run, named after the records
/// in C166InstrInfo.td.
enum class Op {
  Unknown,
#define OPS(X)                                                                                      \
  X(ADD16rr)                                                                                        \
  X(ADD16ri)                                                                                        \
  X(ADD16ri3)                                                                                       \
  X(ADD16ra)                                                                                        \
  X(ADDB8rr) X(ADDB8ri) X(ADDB8ri3) X(ADDB8ra) X(ADDC16rr) X(ADDC16ri) X(                           \
      ADDC16ri3) X(ADDC16ra) X(SUB16rr) X(SUB16ri) X(SUB16ri3) X(SUB16ra) X(SUBB8rr)                \
      X(SUBB8ri) X(SUBB8ri3) X(SUBB8ra) X(SUBC16rr) X(SUBC16ri) X(SUBC16ri3) X(                     \
          SUBC16ra) X(AND16rr) X(AND16ri) X(AND16ri3) X(AND16ra) X(ANDB8rr)                         \
          X(ANDB8ri) X(ANDB8ri3) X(ANDB8ra) X(OR16rr) X(OR16ri) X(OR16ri3) X(                       \
              OR16ra) X(ORB8rr) X(ORB8ri) X(ORB8ri3) X(ORB8ra) X(XOR16rr) X(XOR16ri)                \
              X(XOR16ri3) X(XOR16ra) X(XORB8rr) X(XORB8ri) X(XORB8ri3) X(                           \
                  XORB8ra) X(CMP16rr) X(CMP16ri) X(CMP16ri3) X(ADD16regi) X(ADD16rega) X(SUB16regi) \
                  X(SUB16rega) X(ADDC16regi) X(ADDC16rega) X(SUBC16regi) X(SUBC16rega) X(           \
                      AND16regi) X(AND16rega) X(OR16regi) X(OR16rega) X(XOR16regi)                  \
                      X(XOR16rega) X(CMP16regi) X(MOV16regi) X(                                     \
                          MOV16rega) X(ADDB8regi) X(ADDB8rega) X(SUBB8regi) X(SUBB8rega)            \
                          X(ANDB8regi) X(ANDB8rega) X(ORB8regi) X(ORB8rega) X(XORB8regi) X(         \
                              XORB8rega) X(CMPB8regi) X(MOVB8regi) X(MOVB8rega) X(CMPB8rr)          \
                              X(CMPB8ri) X(CMPB8ri3) X(SHL16rr) X(SHL16ri) X(                       \
                                  SHR16rr) X(SHR16ri) X(ASHR16rr) X(ASHR16ri) X(ROL16rr)            \
                                  X(ROL16ri) X(ROR16rr) X(ROR16ri) X(CPL16r) X(CPLB8r) X(NEG16r) X( \
                                      NEGB8r) X(MULrr) X(MULUrr) X(DIVr) X(DIVUr) X(MOVfromMDL)     \
                                      X(MOVfromMDH) X(MOVtoMDL) X(MOVtoMDH) X(MOV16rr) X(           \
                                          MOV16ri) X(MOV16ri4) X(MOV16ra) X(MOV16ar) X(MOV16rm)     \
                                          X(MOV16mr) X(MOV16rp) X(MOV16pr) X(MOV16rpi) X(           \
                                              MOVB8rr) X(MOVB8ri) X(MOVB8ri4) X(MOVB8ra)            \
                                              X(MOVB8ar) X(MOVB8rm) X(MOVB8mr) X(                   \
                                                  MOVB8rp) X(MOVB8pr) X(MOVB8rpi) X(MOVBS16r8)      \
                                                  X(MOVBZ16r8) X(JMPA) X(JMPAcc) X(JMPR) X(         \
                                                      JMPRcc) X(JMPI) X(JMPS)                       \
                                                      X(TRAP) X(CALLA) X(CALLI) X(CALLS) X(                 \
                                                          RET) X(RETI) X(RETS) X(PUSH) X(POP)       \
                                                          X(JB) X(JNB) X(                           \
                                                              JBC) X(JNBS) X(BSET) X(BCLR) X(BAND)  \
                                                              X(BOR) X(BXOR) X(BMOV) X(             \
                                                                  BMOVN) X(BCMP) X(BFLDL)           \
                                                                  X(BFLDH) X(EXTSi) X(EXTSr) X(     \
                                                                      EXTSRi) X(EXTSRr) X(EXTPi)    \
                                                                      X(EXTPr) X(EXTPRi) X(         \
                                                                          EXTPRr) X(EXTR)           \
                                                                          X(ATOMIC) X(              \
                                                                              NOP) X(DISWDT)        \
                                                                              X(EINIT) X(           \
                                                                                  SRVWDT) X(SRST)   \
                                                                                  X(IDLE)           \
                                                                                      X(PWRDN)
#define X(N) N,
  OPS(X)
#undef X
};

/// Everything the decoder needs, set up once.
struct Decoder {
  std::unique_ptr<const MCRegisterInfo> MRI;
  std::unique_ptr<const MCAsmInfo> MAI;
  std::unique_ptr<const MCInstrInfo> MII;
  std::unique_ptr<const MCSubtargetInfo> STI;
  std::unique_ptr<MCContext> Ctx;
  std::unique_ptr<MCDisassembler> DisAsm;
  std::unique_ptr<MCInstPrinter> Printer;
  /// Target opcode number to Op.
  std::vector<Op> OpOf;
  std::string Error;

  Decoder() {
    Triple TT("c166");
    std::string Err;
    const Target *T = TargetRegistry::lookupTarget(TT, Err);
    if (!T) {
      Error = "no C166 target registered: " + Err;
      return;
    }
    MRI.reset(T->createMCRegInfo(TT));
    MCTargetOptions Options;
    MAI.reset(T->createMCAsmInfo(*MRI, TT, Options));
    MII.reset(T->createMCInstrInfo());
    STI.reset(T->createMCSubtargetInfo(TT, "", ""));
    Ctx = std::make_unique<MCContext>(TT, *MAI, *MRI, *STI);
    DisAsm.reset(T->createMCDisassembler(*STI, *Ctx));
    Printer.reset(T->createMCInstPrinter(TT, 0, *MAI, *MII, *MRI));
    if (!DisAsm) {
      Error = "the C166 target has no disassembler";
      return;
    }
    static const std::pair<StringRef, Op> Names[] = {
#define X(N) {#N, Op::N},
        OPS(X)
#undef X
    };
    std::map<StringRef, Op> ByName(std::begin(Names), std::end(Names));
    OpOf.assign(MII->getNumOpcodes(), Op::Unknown);
    for (unsigned I = 0, E = MII->getNumOpcodes(); I != E; ++I) {
      auto It = ByName.find(MII->getName(I));
      if (It != ByName.end())
        OpOf[I] = It->second;
    }
  }
};

Decoder &decoder() {
  static Decoder D;
  return D;
}

} // namespace

// ---------------------------------------------------------------------------
// Flag helpers.  Each one names the manual's wording for the flag it sets.
// ---------------------------------------------------------------------------

namespace {

struct Executor {
  Machine &M;
  explicit Executor(Machine &M) : M(M) {}

  /// E is "op2 is the lowest possible negative number", which is what walks
  /// off the end of a table.
  void setE(uint32_t Op2, bool Byte) {
    M.setFlag(PSW_E, Byte ? (Op2 & 0xFF) == 0x80 : (Op2 & 0xFFFF) == 0x8000);
  }

  void setZN(uint32_t Res, bool Byte) {
    uint32_t R = Byte ? (Res & 0xFF) : (Res & 0xFFFF);
    M.setFlag(PSW_Z, R == 0);
    M.setFlag(PSW_N, Byte ? (R >> 7) & 1 : (R >> 15) & 1);
  }

  /// ADDC and SUBC keep Z set only if it already was, so that a wide value
  /// tests as zero exactly when every word of it did.
  void setZNCarrying(uint32_t Res, bool Byte, bool PrevZ) {
    uint32_t R = Byte ? (Res & 0xFF) : (Res & 0xFFFF);
    M.setFlag(PSW_Z, R == 0 && PrevZ);
    M.setFlag(PSW_N, Byte ? (R >> 7) & 1 : (R >> 15) & 1);
  }

  uint32_t doAdd(uint32_t A, uint32_t B, bool Byte, bool WithCarry) {
    unsigned Bits = Byte ? 8 : 16;
    uint32_t Mask = Byte ? 0xFF : 0xFFFF;
    uint32_t Cin = WithCarry && M.flag(PSW_C) ? 1 : 0;
    bool PrevZ = M.flag(PSW_Z);
    uint32_t Wide = (A & Mask) + (B & Mask) + Cin;
    uint32_t R = Wide & Mask;
    setE(B, Byte);
    // V is a signed overflow: both operands the same sign, result the other.
    bool SA = (A >> (Bits - 1)) & 1, SB = (B >> (Bits - 1)) & 1;
    bool SR = (R >> (Bits - 1)) & 1;
    M.setFlag(PSW_V, SA == SB && SR != SA);
    M.setFlag(PSW_C, (Wide >> Bits) & 1);
    if (WithCarry)
      setZNCarrying(R, Byte, PrevZ);
    else
      setZN(R, Byte);
    return R;
  }

  uint32_t doSub(uint32_t A, uint32_t B, bool Byte, bool WithCarry) {
    unsigned Bits = Byte ? 8 : 16;
    uint32_t Mask = Byte ? 0xFF : 0xFFFF;
    uint32_t Cin = WithCarry && M.flag(PSW_C) ? 1 : 0;
    bool PrevZ = M.flag(PSW_Z);
    uint32_t Wide = (A & Mask) - (B & Mask) - Cin;
    uint32_t R = Wide & Mask;
    setE(B, Byte);
    bool SA = (A >> (Bits - 1)) & 1, SB = (B >> (Bits - 1)) & 1;
    bool SR = (R >> (Bits - 1)) & 1;
    M.setFlag(PSW_V, SA != SB && SR != SA);
    // C is a borrow, so it is set exactly when the subtraction wrapped.
    M.setFlag(PSW_C,
              ((A & Mask) < (B & Mask) + Cin) || ((B & Mask) + Cin > Mask));
    if (WithCarry)
      setZNCarrying(R, Byte, PrevZ);
    else
      setZN(R, Byte);
    return R;
  }

  uint32_t doLogic(uint32_t A, uint32_t B, bool Byte, char Kind) {
    uint32_t Mask = Byte ? 0xFF : 0xFFFF;
    uint32_t R = Kind == '&' ? (A & B) : Kind == '|' ? (A | B) : (A ^ B);
    R &= Mask;
    setE(B, Byte);
    M.setFlag(PSW_V, false);
    M.setFlag(PSW_C, false);
    setZN(R, Byte);
    return R;
  }

  /// The shift and rotate loops, written the way the manual writes them: the
  /// carry starts clear, each cycle folds the outgoing carry into V before
  /// replacing it, and a count of zero leaves both clear.  SHL and ROL have
  /// no V at all (their flag row reads 0*0S*).
  enum class ShiftKind { Left, Right, Arith, RotL, RotR };
  uint16_t doShift(uint16_t A, unsigned Count, ShiftKind K) {
    bool C = false, V = false;
    bool AccumulatesV =
        K == ShiftKind::Right || K == ShiftKind::Arith || K == ShiftKind::RotR;
    for (unsigned I = 0; I != Count; ++I) {
      if (AccumulatesV)
        V = V || C;
      switch (K) {
      case ShiftKind::Left:
        C = (A >> 15) & 1;
        A = uint16_t(A << 1);
        break;
      case ShiftKind::Right:
        C = A & 1;
        A = uint16_t(A >> 1);
        break;
      case ShiftKind::Arith:
        C = A & 1;
        A = uint16_t(int16_t(A) >> 1);
        break;
      case ShiftKind::RotL:
        C = (A >> 15) & 1;
        A = uint16_t((A << 1) | unsigned(C));
        break;
      case ShiftKind::RotR:
        C = A & 1;
        A = uint16_t((A >> 1) | (unsigned(C) << 15));
        break;
      }
    }
    M.setFlag(PSW_E, false);
    M.setFlag(PSW_C, C);
    M.setFlag(PSW_V, AccumulatesV && V);
    setZN(A, false);
    return A;
  }

  /// A "bitoff" names a word rather than an address: 00H to 7FH is internal
  /// RAM at FD00H + 2*bitoff, 80H to EFH is the special function registers at
  /// FF00H + 2*(bitoff - 80H), and F0H to FFH is R0 to R15.
  uint32_t bitWordAddress(unsigned BitOff) const {
    BitOff &= 0xFF;
    if (BitOff < 0x80)
      return 0xFD00 + 2 * BitOff;
    if (BitOff < 0xF0)
      return 0xFF00 + 2 * (BitOff - 0x80);
    return uint16_t(M.CP + 2 * (BitOff - 0xF0));
  }

  bool getBit(unsigned BitOff, unsigned Pos) {
    return (M.read16(bitWordAddress(BitOff)) >> (Pos & 15)) & 1;
  }
  void putBit(unsigned BitOff, unsigned Pos, bool V) {
    uint32_t A = bitWordAddress(BitOff);
    uint16_t W = M.read16(A);
    uint16_t Mask = uint16_t(1) << (Pos & 15);
    M.write16(A, V ? (W | Mask) : (W & ~Mask));
  }

  /// BSET, BCLR, BMOV and BMOVN all report the previous state of the bit they
  /// read: Z is its negation and N is the bit itself.
  void setBitFlags(bool Prev) {
    M.setFlag(PSW_E, false);
    M.setFlag(PSW_Z, !Prev);
    M.setFlag(PSW_V, false);
    M.setFlag(PSW_C, false);
    M.setFlag(PSW_N, Prev);
  }

  /// Table 5 of the instruction set manual, which is where the boolean form
  /// of each condition is written down, did not survive the extraction this
  /// was built from; these are the conventional readings of the names, and
  /// they agree with what C166ISelLowering.cpp selects for each comparison.
  /// c166-sim-conditions.c exercises all sixteen against a host compiler.
  bool testCond(unsigned CC) const {
    bool Z = M.flag(PSW_Z), C = M.flag(PSW_C);
    bool N = M.flag(PSW_N), V = M.flag(PSW_V), E = M.flag(PSW_E);
    switch (CC & 0xF) {
    case 0x0:
      return true; // cc_UC
    case 0x1:
      return !Z && !E; // cc_NET, not equal and not end of table
    case 0x2:
      return Z; // cc_Z / cc_EQ
    case 0x3:
      return !Z; // cc_NZ / cc_NE
    case 0x4:
      return V; // cc_V
    case 0x5:
      return !V; // cc_NV
    case 0x6:
      return N; // cc_N
    case 0x7:
      return !N; // cc_NN
    case 0x8:
      return C; // cc_C / cc_ULT
    case 0x9:
      return !C; // cc_NC / cc_UGE
    case 0xA:
      return !(Z || (N != V)); // cc_SGT
    case 0xB:
      return Z || (N != V); // cc_SLE
    case 0xC:
      return N != V; // cc_SLT
    case 0xD:
      return N == V; // cc_SGE
    case 0xE:
      return !Z && !C; // cc_UGT
    case 0xF:
      return Z || C; // cc_ULE
    }
    return false;
  }
};

} // namespace

namespace {
void executeOne(Machine &M, const MCInst &MI, Op O, uint32_t PC);

/// A 24 bit address as text, for the messages below.
std::string addrStr(uint32_t A) {
  std::string S;
  raw_string_ostream OS(S);
  OS << format_hex(A, 8);
  return S;
}
} // namespace

bool Machine::step() {
  if (Stop != StopReason::Running)
    return false;
  Decoder &D = decoder();
  if (!D.Error.empty()) {
    Stop = StopReason::BadAccess;
    StopDetail = D.Error;
    return false;
  }
  if (MaxSteps && Steps >= MaxSteps) {
    Stop = StopReason::StepLimit;
    return false;
  }

  uint32_t PC = (uint32_t(CSP) << 16) | IP;
  if (HasExitAddress && PC == ExitAddress) {
    Stop = StopReason::Exited;
    // R2 is where the ABI returns a word, so that is the program's result.
    ExitCode = getWordReg(2);
    return false;
  }

  uint8_t Bytes[4];
  for (unsigned I = 0; I != 4; ++I)
    Bytes[I] = Mem[(PC + I) & AddressMask];

  MCInst MI;
  uint64_t Size = 0;
  auto Res = D.DisAsm->getInstruction(MI, Size, ArrayRef<uint8_t>(Bytes, 4), PC,
                                      nulls());
  if (Res != MCDisassembler::Success || Size == 0) {
    Stop = StopReason::Unsupported;
    StopDetail = (Twine("cannot decode the bytes at ") + addrStr(PC) + ": " +
                  addrStr(Bytes[0]) + " " + addrStr(Bytes[1]))
                     .str();
    return false;
  }

  if (Trace && TraceOS) {
    *TraceOS << formatv("{0:x-6}  ", PC);
    D.Printer->printInst(&MI, PC, "", *D.STI, *TraceOS);
    *TraceOS << "\n";
  }

  Op O = MI.getOpcode() < D.OpOf.size() ? D.OpOf[MI.getOpcode()] : Op::Unknown;
  if (O == Op::Unknown) {
    std::string Text;
    raw_string_ostream OS(Text);
    OS << D.MII->getName(MI.getOpcode()) << " (";
    for (unsigned I = 0, E = MI.getNumOperands(); I != E; ++I) {
      const MCOperand &MO = MI.getOperand(I);
      if (I)
        OS << ", ";
      if (MO.isReg())
        OS << "reg:" << D.MRI->getName(MO.getReg());
      else if (MO.isImm())
        OS << "imm:" << MO.getImm();
      else
        OS << "?";
    }
    OS << ")";
    Stop = StopReason::Unsupported;
    StopDetail =
        (Twine("no simulator support for ") + Text + " at " + addrStr(PC))
            .str();
    return false;
  }

  ++Steps;
  IP = uint16_t(IP + Size);
  executeOne(*this, MI, O, PC);
  retireExtend();
  return Stop == StopReason::Running;
}

namespace {

/// The GPR index of a word register operand, or ~0 when it is not one.
unsigned wordRegIndex(const MCRegisterInfo &MRI, MCRegister R) {
  StringRef N = MRI.getName(R);
  unsigned Idx;
  if (N.consume_front("R") && !N.consumeInteger(10, Idx) && N.empty() &&
      Idx < 16)
    return Idx;
  return ~0u;
}

/// The byte index of a byte register operand: RL0 is 0, RH0 is 1, RL1 is 2
/// and so on, which is the order the bytes sit in the register window.
unsigned byteRegIndex(const MCRegisterInfo &MRI, MCRegister R) {
  StringRef N = MRI.getName(R);
  bool High = N.starts_with("RH");
  if (!High && !N.starts_with("RL"))
    return ~0u;
  unsigned Idx;
  StringRef Rest = N.drop_front(2);
  if (Rest.getAsInteger(10, Idx) || Idx > 7)
    return ~0u;
  return Idx * 2 + (High ? 1 : 0);
}

/// The address a "reg" field names: a GPR through CP, or an SFR by its short
/// address, which is what the register's encoding carries.
uint32_t reg8Address(Machine &M, const MCRegisterInfo &MRI, MCRegister R) {
  unsigned W = wordRegIndex(MRI, R);
  if (W != ~0u)
    return uint16_t(M.CP + 2 * W);
  return M.regFieldAddress(MRI.getEncodingValue(R));
}

void executeOne(Machine &M, const MCInst &MI, Op O, uint32_t PC) {
  Decoder &D = decoder();
  const MCRegisterInfo &MRI = *D.MRI;
  Executor E(M);

  auto NumOps = MI.getNumOperands();
  auto Reg = [&](unsigned I) { return MI.getOperand(I).getReg(); };
  auto Imm = [&](unsigned I) { return uint32_t(MI.getOperand(I).getImm()); };
  // A two-address instruction decodes with its tied source in the middle, so
  // the second source is always last and the destination always first.
  auto LastImm = [&]() { return Imm(NumOps - 1); };
  auto W = [&](unsigned I) { return M.getWordReg(wordRegIndex(MRI, Reg(I))); };
  auto SetW = [&](unsigned I, uint16_t V) {
    M.setWordReg(wordRegIndex(MRI, Reg(I)), V);
  };
  auto B = [&](unsigned I) { return M.getByteReg(byteRegIndex(MRI, Reg(I))); };
  auto SetB = [&](unsigned I, uint8_t V) {
    M.setByteReg(byteRegIndex(MRI, Reg(I)), V);
  };
  auto LastW = [&]() { return W(NumOps - 1); };
  // A "reg" field operand, which is a register either way but a memory
  // location in both cases: a GPR lives at CP + 2n and an SFR at its own
  // address.
  auto R8 = [&](unsigned I) { return M.read16(reg8Address(M, MRI, Reg(I))); };
  auto W8 = [&](unsigned I, uint16_t V) {
    M.write16(reg8Address(M, MRI, Reg(I)), V);
  };
  // The same field in a byte instruction, where it names one byte.  A byte
  // register n lives at CP + n; a special function register is at its own
  // address, and the byte meant is the low one.
  auto ByteReg8Address = [&](unsigned I) -> uint32_t {
    MCRegister R = Reg(I);
    unsigned B = byteRegIndex(MRI, R);
    if (B != ~0u)
      return uint16_t(M.CP + B);
    return M.regFieldAddress(MRI.getEncodingValue(R));
  };
  auto RB8 = [&](unsigned I) { return M.read8(ByteReg8Address(I)); };
  auto WB8 = [&](unsigned I, uint8_t V) { M.write8(ByteReg8Address(I), V); };
  auto LastB = [&]() { return B(NumOps - 1); };

  // A "mem" operand and an indirect address both go through the DPP window
  // unless an EXTend instruction is overriding it.
  auto Load16At = [&](uint16_t A) { return M.read16(M.mapData(A)); };
  auto Store16At = [&](uint16_t A, uint16_t V) { M.write16(M.mapData(A), V); };
  auto Load8At = [&](uint16_t A) { return M.read8(M.mapData(A)); };
  auto Store8At = [&](uint16_t A, uint8_t V) { M.write8(M.mapData(A), V); };

  auto Unsupported = [&](const Twine &Why) {
    M.Stop = StopReason::Unsupported;
    M.StopDetail = (Twine(Why) + " at " + addrStr(PC)).str();
  };

  switch (O) {
  // -- word arithmetic and logic ----------------------------------------
  case Op::ADD16rr:
    SetW(0, E.doAdd(W(0), LastW(), false, false));
    break;
  case Op::ADD16ri:
  case Op::ADD16ri3:
    SetW(0, E.doAdd(W(0), LastImm(), false, false));
    break;
  case Op::ADD16ra:
    SetW(0, E.doAdd(W(0), Load16At(LastImm()), false, false));
    break;
  case Op::ADDC16rr:
    SetW(0, E.doAdd(W(0), LastW(), false, true));
    break;
  case Op::ADDC16ri:
  case Op::ADDC16ri3:
    SetW(0, E.doAdd(W(0), LastImm(), false, true));
    break;
  case Op::ADDC16ra:
    SetW(0, E.doAdd(W(0), Load16At(LastImm()), false, true));
    break;
  case Op::SUB16rr:
    SetW(0, E.doSub(W(0), LastW(), false, false));
    break;
  case Op::SUB16ri:
  case Op::SUB16ri3:
    SetW(0, E.doSub(W(0), LastImm(), false, false));
    break;
  case Op::SUB16ra:
    SetW(0, E.doSub(W(0), Load16At(LastImm()), false, false));
    break;
  case Op::SUBC16rr:
    SetW(0, E.doSub(W(0), LastW(), false, true));
    break;
  case Op::SUBC16ri:
  case Op::SUBC16ri3:
    SetW(0, E.doSub(W(0), LastImm(), false, true));
    break;
  case Op::SUBC16ra:
    SetW(0, E.doSub(W(0), Load16At(LastImm()), false, true));
    break;
  case Op::AND16rr:
    SetW(0, E.doLogic(W(0), LastW(), false, '&'));
    break;
  case Op::AND16ri:
  case Op::AND16ri3:
    SetW(0, E.doLogic(W(0), LastImm(), false, '&'));
    break;
  case Op::AND16ra:
    SetW(0, E.doLogic(W(0), Load16At(LastImm()), false, '&'));
    break;
  case Op::OR16rr:
    SetW(0, E.doLogic(W(0), LastW(), false, '|'));
    break;
  case Op::OR16ri:
  case Op::OR16ri3:
    SetW(0, E.doLogic(W(0), LastImm(), false, '|'));
    break;
  case Op::OR16ra:
    SetW(0, E.doLogic(W(0), Load16At(LastImm()), false, '|'));
    break;
  case Op::XOR16rr:
    SetW(0, E.doLogic(W(0), LastW(), false, '^'));
    break;
  case Op::XOR16ri:
  case Op::XOR16ri3:
    SetW(0, E.doLogic(W(0), LastImm(), false, '^'));
    break;
  case Op::XOR16ra:
    SetW(0, E.doLogic(W(0), Load16At(LastImm()), false, '^'));
    break;
  case Op::CMP16rr:
    E.doSub(W(0), LastW(), false, false);
    break;
  case Op::CMP16ri:
  case Op::CMP16ri3:
    E.doSub(W(0), LastImm(), false, false);
    break;

  // -- the same with the whole 8 bit "reg" field ------------------------
  // The field names a general purpose register through CP or a special
  // function register by its short address, and both are memory here, so one
  // pair of accessors covers them.
  case Op::ADD16regi:
    W8(0, E.doAdd(R8(0), Imm(1), false, false));
    break;
  case Op::ADD16rega:
    W8(0, E.doAdd(R8(0), Load16At(Imm(1)), false, false));
    break;
  case Op::ADDC16regi:
    W8(0, E.doAdd(R8(0), Imm(1), false, true));
    break;
  case Op::ADDC16rega:
    W8(0, E.doAdd(R8(0), Load16At(Imm(1)), false, true));
    break;
  case Op::SUB16regi:
    W8(0, E.doSub(R8(0), Imm(1), false, false));
    break;
  case Op::SUB16rega:
    W8(0, E.doSub(R8(0), Load16At(Imm(1)), false, false));
    break;
  case Op::SUBC16regi:
    W8(0, E.doSub(R8(0), Imm(1), false, true));
    break;
  case Op::SUBC16rega:
    W8(0, E.doSub(R8(0), Load16At(Imm(1)), false, true));
    break;
  case Op::AND16regi:
    W8(0, E.doLogic(R8(0), Imm(1), false, '&'));
    break;
  case Op::AND16rega:
    W8(0, E.doLogic(R8(0), Load16At(Imm(1)), false, '&'));
    break;
  case Op::OR16regi:
    W8(0, E.doLogic(R8(0), Imm(1), false, '|'));
    break;
  case Op::OR16rega:
    W8(0, E.doLogic(R8(0), Load16At(Imm(1)), false, '|'));
    break;
  case Op::XOR16regi:
    W8(0, E.doLogic(R8(0), Imm(1), false, '^'));
    break;
  case Op::XOR16rega:
    W8(0, E.doLogic(R8(0), Load16At(Imm(1)), false, '^'));
    break;
  case Op::CMP16regi:
    E.doSub(R8(0), Imm(1), false, false);
    break;
  case Op::MOV16regi: {
    uint16_t V = uint16_t(Imm(1));
    E.setE(V, false);
    E.setZN(V, false);
    W8(0, V);
    break;
  }
  case Op::MOV16rega: {
    uint16_t V = Load16At(uint16_t(Imm(1)));
    E.setE(V, false);
    E.setZN(V, false);
    W8(0, V);
    break;
  }

  // The byte instructions with the wide field.  F0H + n is a byte register
  // here, so the location is one byte wide wherever it is.
  case Op::ADDB8regi:
    WB8(0, E.doAdd(RB8(0), Imm(1), true, false));
    break;
  case Op::ADDB8rega:
    WB8(0, E.doAdd(RB8(0), Load8At(Imm(1)), true, false));
    break;
  case Op::SUBB8regi:
    WB8(0, E.doSub(RB8(0), Imm(1), true, false));
    break;
  case Op::SUBB8rega:
    WB8(0, E.doSub(RB8(0), Load8At(Imm(1)), true, false));
    break;
  case Op::ANDB8regi:
    WB8(0, E.doLogic(RB8(0), Imm(1), true, '&'));
    break;
  case Op::ANDB8rega:
    WB8(0, E.doLogic(RB8(0), Load8At(Imm(1)), true, '&'));
    break;
  case Op::ORB8regi:
    WB8(0, E.doLogic(RB8(0), Imm(1), true, '|'));
    break;
  case Op::ORB8rega:
    WB8(0, E.doLogic(RB8(0), Load8At(Imm(1)), true, '|'));
    break;
  case Op::XORB8regi:
    WB8(0, E.doLogic(RB8(0), Imm(1), true, '^'));
    break;
  case Op::XORB8rega:
    WB8(0, E.doLogic(RB8(0), Load8At(Imm(1)), true, '^'));
    break;
  case Op::CMPB8regi:
    E.doSub(RB8(0), Imm(1), true, false);
    break;
  case Op::MOVB8regi: {
    uint8_t V = uint8_t(Imm(1));
    E.setE(V, true);
    E.setZN(V, true);
    WB8(0, V);
    break;
  }
  case Op::MOVB8rega: {
    uint8_t V = Load8At(uint16_t(Imm(1)));
    E.setE(V, true);
    E.setZN(V, true);
    WB8(0, V);
    break;
  }

  // -- byte arithmetic and logic ----------------------------------------
  case Op::ADDB8rr:
    SetB(0, E.doAdd(B(0), LastB(), true, false));
    break;
  case Op::ADDB8ri:
  case Op::ADDB8ri3:
    SetB(0, E.doAdd(B(0), LastImm(), true, false));
    break;
  case Op::ADDB8ra:
    SetB(0, E.doAdd(B(0), Load8At(LastImm()), true, false));
    break;
  case Op::SUBB8rr:
    SetB(0, E.doSub(B(0), LastB(), true, false));
    break;
  case Op::SUBB8ri:
  case Op::SUBB8ri3:
    SetB(0, E.doSub(B(0), LastImm(), true, false));
    break;
  case Op::SUBB8ra:
    SetB(0, E.doSub(B(0), Load8At(LastImm()), true, false));
    break;
  case Op::ANDB8rr:
    SetB(0, E.doLogic(B(0), LastB(), true, '&'));
    break;
  case Op::ANDB8ri:
  case Op::ANDB8ri3:
    SetB(0, E.doLogic(B(0), LastImm(), true, '&'));
    break;
  case Op::ANDB8ra:
    SetB(0, E.doLogic(B(0), Load8At(LastImm()), true, '&'));
    break;
  case Op::ORB8rr:
    SetB(0, E.doLogic(B(0), LastB(), true, '|'));
    break;
  case Op::ORB8ri:
  case Op::ORB8ri3:
    SetB(0, E.doLogic(B(0), LastImm(), true, '|'));
    break;
  case Op::ORB8ra:
    SetB(0, E.doLogic(B(0), Load8At(LastImm()), true, '|'));
    break;
  case Op::XORB8rr:
    SetB(0, E.doLogic(B(0), LastB(), true, '^'));
    break;
  case Op::XORB8ri:
  case Op::XORB8ri3:
    SetB(0, E.doLogic(B(0), LastImm(), true, '^'));
    break;
  case Op::XORB8ra:
    SetB(0, E.doLogic(B(0), Load8At(LastImm()), true, '^'));
    break;
  case Op::CMPB8rr:
    E.doSub(B(0), LastB(), true, false);
    break;
  case Op::CMPB8ri:
  case Op::CMPB8ri3:
    E.doSub(B(0), LastImm(), true, false);
    break;

  // -- one operand ------------------------------------------------------
  // CPL and NEG take E from op1 rather than op2, since there is no op2.
  case Op::CPL16r: {
    uint16_t A = W(0), R = uint16_t(~A);
    E.setE(A, false);
    M.setFlag(PSW_V, false);
    M.setFlag(PSW_C, false);
    E.setZN(R, false);
    SetW(0, R);
    break;
  }
  case Op::CPLB8r: {
    uint8_t A = B(0), R = uint8_t(~A);
    E.setE(A, true);
    M.setFlag(PSW_V, false);
    M.setFlag(PSW_C, false);
    E.setZN(R, true);
    SetB(0, R);
    break;
  }
  case Op::NEG16r: {
    uint16_t A = W(0);
    uint16_t R = E.doSub(0, A, false, false);
    E.setE(A, false);
    SetW(0, R);
    break;
  }
  case Op::NEGB8r: {
    uint8_t A = B(0);
    uint8_t R = E.doSub(0, A, true, false);
    E.setE(A, true);
    SetB(0, R);
    break;
  }
  // -- shifts and rotates ------------------------------------------------
  case Op::SHL16rr:
    SetW(0, E.doShift(W(0), LastW() & 0xF, Executor::ShiftKind::Left));
    break;
  case Op::SHL16ri:
    SetW(0, E.doShift(W(0), LastImm() & 0xF, Executor::ShiftKind::Left));
    break;
  case Op::SHR16rr:
    SetW(0, E.doShift(W(0), LastW() & 0xF, Executor::ShiftKind::Right));
    break;
  case Op::SHR16ri:
    SetW(0, E.doShift(W(0), LastImm() & 0xF, Executor::ShiftKind::Right));
    break;
  case Op::ASHR16rr:
    SetW(0, E.doShift(W(0), LastW() & 0xF, Executor::ShiftKind::Arith));
    break;
  case Op::ASHR16ri:
    SetW(0, E.doShift(W(0), LastImm() & 0xF, Executor::ShiftKind::Arith));
    break;
  case Op::ROL16rr:
    SetW(0, E.doShift(W(0), LastW() & 0xF, Executor::ShiftKind::RotL));
    break;
  case Op::ROL16ri:
    SetW(0, E.doShift(W(0), LastImm() & 0xF, Executor::ShiftKind::RotL));
    break;
  case Op::ROR16rr:
    SetW(0, E.doShift(W(0), LastW() & 0xF, Executor::ShiftKind::RotR));
    break;
  case Op::ROR16ri:
    SetW(0, E.doShift(W(0), LastImm() & 0xF, Executor::ShiftKind::RotR));
    break;

  // -- multiply and divide, which go through the MDL/MDH pair -------------
  // MDC.MDRIU, which the manual also sets here, is not modelled: nothing the
  // backend emits reads it.
  case Op::MULrr: {
    int32_t P = int32_t(int16_t(W(0))) * int32_t(int16_t(W(1)));
    M.MDL = uint16_t(P);
    M.MDH = uint16_t(uint32_t(P) >> 16);
    M.setFlag(PSW_E, false);
    M.setFlag(PSW_Z, P == 0);
    M.setFlag(PSW_V, P != int32_t(int16_t(P)));
    M.setFlag(PSW_C, false);
    M.setFlag(PSW_N, (uint32_t(P) >> 31) & 1);
    break;
  }
  case Op::MULUrr: {
    uint32_t P = uint32_t(W(0)) * uint32_t(W(1));
    M.MDL = uint16_t(P);
    M.MDH = uint16_t(P >> 16);
    M.setFlag(PSW_E, false);
    M.setFlag(PSW_Z, P == 0);
    M.setFlag(PSW_V, (P >> 16) != 0);
    M.setFlag(PSW_C, false);
    M.setFlag(PSW_N, (P >> 31) & 1);
    break;
  }
  case Op::DIVr:
  case Op::DIVUr: {
    uint16_t Divisor = W(0);
    M.setFlag(PSW_E, false);
    M.setFlag(PSW_C, false);
    if (Divisor == 0) {
      // The manual says the result is not valid, and marks the overflow.
      M.setFlag(PSW_V, true);
      break;
    }
    M.setFlag(PSW_V, false);
    uint16_t Q, R;
    if (O == Op::DIVr) {
      int16_t N = int16_t(M.MDL), Dv = int16_t(Divisor);
      Q = uint16_t(int16_t(N / Dv));
      R = uint16_t(int16_t(N % Dv));
    } else {
      Q = uint16_t(M.MDL / Divisor);
      R = uint16_t(M.MDL % Divisor);
    }
    M.MDL = Q;
    M.MDH = R;
    E.setZN(Q, false);
    break;
  }
  case Op::MOVfromMDL:
    SetW(0, M.MDL);
    break;
  case Op::MOVfromMDH:
    SetW(0, M.MDH);
    break;
  case Op::MOVtoMDL:
    M.MDL = W(0);
    break;
  case Op::MOVtoMDH:
    M.MDH = W(0);
    break;

  // -- moves -------------------------------------------------------------
  // MOV and MOVB set E, Z and N but leave V and C alone, which is what lets a
  // carry survive across the register shuffling around a wide addition.
  case Op::MOV16rr: {
    uint16_t V = W(1);
    E.setE(V, false);
    E.setZN(V, false);
    SetW(0, V);
    break;
  }
  case Op::MOV16ri:
  case Op::MOV16ri4: {
    uint16_t V = uint16_t(LastImm());
    E.setE(V, false);
    E.setZN(V, false);
    SetW(0, V);
    break;
  }
  case Op::MOV16ra: {
    uint16_t V = Load16At(uint16_t(LastImm()));
    E.setE(V, false);
    E.setZN(V, false);
    SetW(0, V);
    break;
  }
  case Op::MOV16ar: {
    uint16_t V = W(1);
    E.setE(V, false);
    E.setZN(V, false);
    Store16At(uint16_t(Imm(0)), V);
    break;
  }
  case Op::MOV16rp: {
    uint16_t V = Load16At(W(1));
    E.setE(V, false);
    E.setZN(V, false);
    SetW(0, V);
    break;
  }
  case Op::MOV16pr: {
    uint16_t V = W(1);
    E.setE(V, false);
    E.setZN(V, false);
    Store16At(W(0), V);
    break;
  }
  case Op::MOV16rm: {
    uint16_t A = uint16_t(W(1) + Imm(2));
    uint16_t V = Load16At(A);
    E.setE(V, false);
    E.setZN(V, false);
    SetW(0, V);
    break;
  }
  case Op::MOV16mr: {
    uint16_t A = uint16_t(W(0) + Imm(1));
    uint16_t V = W(2);
    E.setE(V, false);
    E.setZN(V, false);
    Store16At(A, V);
    break;
  }
  case Op::MOV16rpi: {
    // Two results: the loaded word and the stepped pointer.  The pointer moves
    // by the width of the access.
    unsigned Base = wordRegIndex(MRI, Reg(2));
    uint16_t A = M.getWordReg(Base);
    uint16_t V = Load16At(A);
    E.setE(V, false);
    E.setZN(V, false);
    SetW(0, V);
    M.setWordReg(wordRegIndex(MRI, Reg(1)), uint16_t(A + 2));
    break;
  }
  case Op::MOVB8rr: {
    uint8_t V = B(1);
    E.setE(V, true);
    E.setZN(V, true);
    SetB(0, V);
    break;
  }
  case Op::MOVB8ri:
  case Op::MOVB8ri4: {
    uint8_t V = uint8_t(LastImm());
    E.setE(V, true);
    E.setZN(V, true);
    SetB(0, V);
    break;
  }
  case Op::MOVB8ra: {
    uint8_t V = Load8At(uint16_t(LastImm()));
    E.setE(V, true);
    E.setZN(V, true);
    SetB(0, V);
    break;
  }
  case Op::MOVB8ar: {
    uint8_t V = B(1);
    E.setE(V, true);
    E.setZN(V, true);
    Store8At(uint16_t(Imm(0)), V);
    break;
  }
  case Op::MOVB8rp: {
    uint8_t V = Load8At(W(1));
    E.setE(V, true);
    E.setZN(V, true);
    SetB(0, V);
    break;
  }
  case Op::MOVB8pr: {
    uint8_t V = B(1);
    E.setE(V, true);
    E.setZN(V, true);
    Store8At(W(0), V);
    break;
  }
  case Op::MOVB8rm: {
    uint16_t A = uint16_t(W(1) + Imm(2));
    uint8_t V = Load8At(A);
    E.setE(V, true);
    E.setZN(V, true);
    SetB(0, V);
    break;
  }
  case Op::MOVB8mr: {
    uint16_t A = uint16_t(W(0) + Imm(1));
    uint8_t V = B(2);
    E.setE(V, true);
    E.setZN(V, true);
    Store8At(A, V);
    break;
  }
  case Op::MOVB8rpi: {
    unsigned Base = wordRegIndex(MRI, Reg(2));
    uint16_t A = M.getWordReg(Base);
    uint8_t V = Load8At(A);
    E.setE(V, true);
    E.setZN(V, true);
    SetB(0, V);
    M.setWordReg(wordRegIndex(MRI, Reg(1)), uint16_t(A + 1));
    break;
  }
  case Op::MOVBZ16r8: {
    uint8_t V = B(1);
    SetW(0, V);
    M.setFlag(PSW_E, false);
    M.setFlag(PSW_Z, V == 0);
    M.setFlag(PSW_N, false);
    break;
  }
  case Op::MOVBS16r8: {
    uint8_t V = B(1);
    uint16_t R = uint16_t(int16_t(int8_t(V)));
    SetW(0, R);
    M.setFlag(PSW_E, false);
    M.setFlag(PSW_Z, V == 0);
    M.setFlag(PSW_N, (V >> 7) & 1);
    break;
  }

  // -- control flow ------------------------------------------------------
  case Op::JMPA:
    M.IP = uint16_t(Imm(0));
    break;
  case Op::JMPAcc:
    // This one lists its target before its condition.
    if (E.testCond(Imm(1)))
      M.IP = uint16_t(Imm(0));
    break;
  case Op::JMPR:
  case Op::JMPRcc: {
    // The displacement counts words from the instruction after this one, which
    // is where IP already is.  JMPRcc lists its target before its condition,
    // the same way JMPAcc does.
    int8_t Rel = int8_t(Imm(0));
    if (O == Op::JMPR || E.testCond(Imm(1)))
      M.IP = uint16_t(M.IP + 2 * Rel);
    break;
  }
  case Op::JMPI:
    if (E.testCond(Imm(0)))
      M.IP = W(1);
    break;
  case Op::JMPS:
    M.CSP = uint16_t(Imm(0));
    M.IP = uint16_t(Imm(1));
    break;
  case Op::CALLA:
    if (E.testCond(Imm(0))) {
      M.push(M.IP);
      M.IP = uint16_t(Imm(1));
    }
    break;
  case Op::CALLI:
    if (E.testCond(Imm(0))) {
      uint16_t T = W(1);
      M.push(M.IP);
      M.IP = T;
    }
    break;
  case Op::CALLS:
    // CALLS pushes the segment as well, which is what RETS pops back.
    M.push(M.CSP);
    M.push(M.IP);
    M.CSP = uint16_t(Imm(0));
    M.IP = uint16_t(Imm(1));
    break;
  case Op::RET:
    M.IP = M.pop();
    break;
  case Op::RETS:
    M.IP = M.pop();
    M.CSP = M.pop();
    break;
  case Op::TRAP:
    // A trap saves what a hardware interrupt saves and then branches to the
    // vector table entry itself - it does not read a vector through it.  The
    // entry for trap n is the double word at 4n, which is where a project puts
    // a jump to its handler.  CSP is pushed and cleared because segmentation is
    // on, which is the only mode this simulator runs in, and RETI undoes all
    // three pushes in the opposite order.
    M.push(M.PSW);
    M.push(M.CSP);
    M.CSP = 0;
    M.push(M.IP);
    M.IP = uint16_t(4 * (Imm(0) & 0x7F));
    break;
  case Op::RETI:
    // The interrupted state is IP, then CSP while segmentation is on, then
    // PSW.  Segmentation is enabled out of reset, which is the only mode this
    // simulator runs in.
    M.IP = M.pop();
    M.CSP = M.pop();
    M.PSW = M.pop();
    break;
  case Op::PUSH:
    M.push(M.read16(reg8Address(M, MRI, Reg(0))));
    break;
  case Op::POP:
    M.write16(reg8Address(M, MRI, Reg(0)), M.pop());
    break;

  // -- bit test branches -------------------------------------------------
  // The displacement counts words from the instruction after this one, which
  // is where IP already is.
  case Op::JB:
  case Op::JNB:
  case Op::JBC:
  case Op::JNBS: {
    unsigned Off = Imm(0), Pos = Imm(1);
    int8_t Rel = int8_t(Imm(2));
    bool Bit = E.getBit(Off, Pos);
    bool Want = (O == Op::JB || O == Op::JBC);
    if (Bit == Want) {
      // JBC clears the bit it found set, JNBS sets the bit it found clear.
      if (O == Op::JBC)
        E.putBit(Off, Pos, false);
      else if (O == Op::JNBS)
        E.putBit(Off, Pos, true);
      M.IP = uint16_t(M.IP + 2 * Rel);
    }
    if (O == Op::JBC || O == Op::JNBS)
      E.setBitFlags(Bit);
    break;
  }

  // -- bit manipulation --------------------------------------------------
  case Op::BSET: {
    bool Prev = E.getBit(Imm(0), Imm(1));
    E.putBit(Imm(0), Imm(1), true);
    E.setBitFlags(Prev);
    break;
  }
  case Op::BCLR: {
    bool Prev = E.getBit(Imm(0), Imm(1));
    E.putBit(Imm(0), Imm(1), false);
    E.setBitFlags(Prev);
    break;
  }
  case Op::BMOV: {
    bool Src = E.getBit(Imm(2), Imm(3));
    E.putBit(Imm(0), Imm(1), Src);
    E.setBitFlags(Src);
    break;
  }
  case Op::BMOVN: {
    bool Src = E.getBit(Imm(2), Imm(3));
    E.putBit(Imm(0), Imm(1), !Src);
    E.setBitFlags(Src);
    break;
  }
  case Op::BAND:
  case Op::BOR:
  case Op::BXOR:
  case Op::BCMP: {
    bool A = E.getBit(Imm(0), Imm(1)), Bb = E.getBit(Imm(2), Imm(3));
    // These four report the two bits rather than the result: Z is their NOR,
    // V their OR, C their AND and N their XOR.
    M.setFlag(PSW_E, false);
    M.setFlag(PSW_Z, !(A || Bb));
    M.setFlag(PSW_V, A || Bb);
    M.setFlag(PSW_C, A && Bb);
    M.setFlag(PSW_N, A != Bb);
    if (O == Op::BAND)
      E.putBit(Imm(0), Imm(1), A && Bb);
    else if (O == Op::BOR)
      E.putBit(Imm(0), Imm(1), A || Bb);
    else if (O == Op::BXOR)
      E.putBit(Imm(0), Imm(1), A != Bb);
    break;
  }
  case Op::BFLDL:
  case Op::BFLDH: {
    uint32_t A = E.bitWordAddress(Imm(0));
    uint16_t Cur = M.read16(A);
    uint16_t Mask = uint16_t(Imm(1)), Val = uint16_t(Imm(2));
    uint16_t R =
        O == Op::BFLDL
            ? uint16_t((Cur & ~Mask) | (Val & Mask))
            : uint16_t((Cur & ~(Mask << 8)) | ((Val << 8) & (Mask << 8)));
    M.write16(A, R);
    M.setFlag(PSW_E, false);
    M.setFlag(PSW_Z, R == 0);
    M.setFlag(PSW_V, false);
    M.setFlag(PSW_C, false);
    M.setFlag(PSW_N, (R >> 15) & 1);
    break;
  }

  // -- addressing overrides ----------------------------------------------
  // The count is written 1 to 4 and covers that many following instructions.
  // retireExtend() counts one off after each, so it is set one high here to
  // account for the EXTend instruction itself not being covered.
  case Op::EXTSi:
  case Op::EXTSRi:
    M.Extend = ExtendKind::Segment;
    M.ExtendValue = Imm(0);
    M.ExtendCount = Imm(1) + 1;
    break;
  case Op::EXTSr:
  case Op::EXTSRr:
    M.Extend = ExtendKind::Segment;
    M.ExtendValue = W(0);
    M.ExtendCount = Imm(1) + 1;
    break;
  case Op::EXTPi:
  case Op::EXTPRi:
    M.Extend = ExtendKind::Page;
    M.ExtendValue = Imm(0);
    M.ExtendCount = Imm(1) + 1;
    break;
  case Op::EXTPr:
  case Op::EXTPRr:
    M.Extend = ExtendKind::Page;
    M.ExtendValue = W(0);
    M.ExtendCount = Imm(1) + 1;
    break;
  case Op::EXTR:
    // Only the SFR space switch, which this simulator does not model because
    // nothing here uses the extended SFRs.  Counting it keeps the sequence
    // length right.
    M.ExtendCount = Imm(0) + 1;
    break;
  case Op::ATOMIC:
    // Interrupts are not simulated, so locking them is a no-op.
    M.ExtendCount = Imm(0) + 1;
    break;

  // -- system ------------------------------------------------------------
  case Op::NOP:
  case Op::DISWDT:
  case Op::EINIT:
  case Op::SRVWDT:
    break;
  case Op::SRST:
    M.Stop = StopReason::Halted;
    M.StopDetail = "the program executed SRST";
    break;
  case Op::IDLE:
  case Op::PWRDN:
    M.Stop = StopReason::Halted;
    M.StopDetail = "the program idled with no interrupt source to wake it";
    break;

  case Op::Unknown:
    Unsupported(Twine("no simulator support for ") +
                D.MII->getName(MI.getOpcode()));
    break;
  }
}

} // namespace
