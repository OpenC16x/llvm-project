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
#include "llvm/ADT/Twine.h"
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
#include "llvm/Support/FormatVariadic.h"
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
                                                                                      X(PWRDN) \
  /* The rest of the instruction set: forms nothing here generates, added so  */\
  /* that what can be written by hand can also be run.                       */\
  X(ADD16regm) X(ADDB8regm) X(SUB16regm) X(SUBB8regm) X(ADDC16regm)              \
  X(ADDCB8regm) X(SUBC16regm) X(SUBCB8regm) X(AND16regm) X(ANDB8regm)            \
  X(OR16regm) X(ORB8regm) X(XOR16regm) X(XORB8regm) X(CMP16regm) X(CMPB8regm)    \
  X(ADDCB8rr) X(ADDCB8ri3) X(ADDCB8regi) X(ADDCB8rega)                           \
  X(SUBCB8rr) X(SUBCB8ri3) X(SUBCB8regi) X(SUBCB8rega)                           \
  X(ADD16rp) X(ADD16rppi) X(SUB16rp) X(SUB16rppi) X(AND16rp) X(AND16rppi)        \
  X(OR16rp) X(OR16rppi) X(XOR16rp) X(XOR16rppi) X(ADDC16rp) X(ADDC16rppi)        \
  X(SUBC16rp) X(SUBC16rppi) X(CMP16rp) X(CMP16rppi) X(CMPB8rp) X(CMPB8rppi)      \
  X(CMP16rega) X(CMPB8rega) X(MOV16prd) X(MOVB8prd)                              \
  X(DIVLr) X(DIVLUr) X(PRIORrr)                                                  \
  X(CMPI116ri4) X(CMPI116regi) X(CMPI116rega)                                    \
  X(CMPI216ri4) X(CMPI216regi) X(CMPI216rega)                                    \
  X(CMPD116ri4) X(CMPD116regi) X(CMPD116rega)                                    \
  X(CMPD216ri4) X(CMPD216regi) X(CMPD216rega)                                    \
  X(SCXTregi) X(SCXTrega) X(CALLR) X(PCALL) X(RETP)                            \
  X(CoLOAD_rr) X(CoMAC_rr) X(CoMACu_rr) X(CoMACN_rr) X(CoMACuN_rr)               \
  X(CoMAX_rr) X(CoMIN_rr)                                                      \
  X(CoMUL_rr) X(CoMULu_rr) X(CoMUL_rr_rnd) X(CoMULu_rr_rnd)                    \
  X(CoSTORE_sr)                                                                \
  /* The 89 forms the ST10 Family Programming Manual's Rep column marks     */\
  /* repeatable, which are the ones a filter is written with.               */\
  X(CoADD2_rp) X(CoADD2_xp) X(CoADD_rp) X(CoADD_xp) X(CoASHR_p)               \
  X(CoASHR_p_rnd) X(CoASHR_r) X(CoASHR_r_rnd) X(CoMACMN_xp) X(CoMACMR_xp)     \
  X(CoMACMR_xp_rnd) X(CoMACMRsu_xp) X(CoMACMRsu_xp_rnd) X(CoMACMRu_xp)        \
  X(CoMACMRu_xp_rnd) X(CoMACMRus_xp) X(CoMACMRus_xp_rnd) X(CoMACM_xp)         \
  X(CoMACM_xp_rnd) X(CoMACMsuN_xp) X(CoMACMsu_xp) X(CoMACMsu_xp_rnd)          \
  X(CoMACMuN_xp) X(CoMACMu_xp) X(CoMACMu_xp_rnd) X(CoMACMusN_xp)              \
  X(CoMACMus_xp) X(CoMACMus_xp_rnd) X(CoMACN_rp) X(CoMACN_xp) X(CoMACR_rp)    \
  X(CoMACR_rp_rnd) X(CoMACR_xp) X(CoMACR_xp_rnd) X(CoMACRsu_rp)               \
  X(CoMACRsu_rp_rnd) X(CoMACRsu_xp) X(CoMACRsu_xp_rnd) X(CoMACRu_rp)          \
  X(CoMACRu_rp_rnd) X(CoMACRu_xp) X(CoMACRu_xp_rnd) X(CoMACRus_rp)            \
  X(CoMACRus_rp_rnd) X(CoMACRus_xp) X(CoMACRus_xp_rnd) X(CoMAC_rp)            \
  X(CoMAC_rp_rnd) X(CoMAC_xp) X(CoMAC_xp_rnd) X(CoMACsuN_rp) X(CoMACsuN_xp)   \
  X(CoMACsu_rp) X(CoMACsu_rp_rnd) X(CoMACsu_xp) X(CoMACsu_xp_rnd)             \
  X(CoMACuN_rp) X(CoMACuN_xp) X(CoMACu_rp) X(CoMACu_rp_rnd) X(CoMACu_xp)      \
  X(CoMACu_xp_rnd) X(CoMACusN_rp) X(CoMACusN_xp) X(CoMACus_rp)                \
  X(CoMACus_rp_rnd) X(CoMACus_xp) X(CoMACus_xp_rnd) X(CoMAX_rp) X(CoMAX_xp)   \
  X(CoMIN_rp) X(CoMIN_xp) X(CoMOV_xp) X(CoNOP_q) X(CoNOP_x) X(CoNOP_xp)       \
  X(CoSHL_p) X(CoSHL_r) X(CoSHR_p) X(CoSHR_r) X(CoSTORE_sp) X(CoSUB2R_rp)     \
  X(CoSUB2R_xp) X(CoSUB2_rp) X(CoSUB2_xp) X(CoSUBR_rp) X(CoSUBR_xp)           \
  X(CoSUB_rp) X(CoSUB_xp)

#define X(N) N,
  OPS(X)
#undef X
};

/// How a repeatable coprocessor form is put together.
///
/// Shape is where its operands come from, Kind what it does with them, and
/// Sign how the two words of a product are read.  The rest are the options
/// the mnemonic spells: "M" writes the delay line back, "R" negates the
/// accumulator rather than the product, "-" negates the product, "rnd" rounds
/// to the high word, and "2" doubles the operand.
namespace Sh {
enum Shape {
  RP, ///< Rwn, [Rwm]
  XP, ///< [IDXi], [Rwm]
  P,  ///< [Rwm] alone
  R,  ///< Rwn alone
  Q,  ///< [Rwm] alone, in the two pointer encoding
  X,  ///< [IDXi] alone
  SP  ///< [Rwn], CoReg
};
}
namespace K {
enum Kind { ADD, SUB, MIN, MAX, MAC, MOV, NOP, STORE, SHL, SHR, ASHR };
}
namespace Sg {
enum Sign {
  SS, ///< both signed
  UU, ///< both unsigned, and the product zero extended
  SU, ///< op1 signed, op2 unsigned
  US  ///< op1 unsigned, op2 signed
};
}
enum CoFlag { Move = 1, Rev = 2, Neg = 4, Rnd = 8, Dbl = 16 };

struct CoForm {
  Op O;
  Sh::Shape Shape;
  K::Kind Kind;
  Sg::Sign Sign;
  unsigned Flags;
};

/// Every form the manual marks repeatable, with what the mnemonic says about
/// it.  Read off that manual's instruction tables rather than derived here:
/// the naming is regular, but a table that can be checked against the page is
/// worth more than a rule that cannot.
static const CoForm CoForms[] = {
    {Op::CoADD2_rp,        Sh::RP,  K::ADD,   Sg::SS, Dbl},
    {Op::CoADD2_xp,        Sh::XP,  K::ADD,   Sg::SS, Dbl},
    {Op::CoADD_rp,         Sh::RP,  K::ADD,   Sg::SS, 0},
    {Op::CoADD_xp,         Sh::XP,  K::ADD,   Sg::SS, 0},
    {Op::CoASHR_p,         Sh::P,   K::ASHR,  Sg::SS, 0},
    {Op::CoASHR_p_rnd,     Sh::P,   K::ASHR,  Sg::SS, Rnd},
    {Op::CoASHR_r,         Sh::R,   K::ASHR,  Sg::SS, 0},
    {Op::CoASHR_r_rnd,     Sh::R,   K::ASHR,  Sg::SS, Rnd},
    {Op::CoMACMN_xp,       Sh::XP,  K::MAC,   Sg::SS, Move|Neg},
    {Op::CoMACMR_xp,       Sh::XP,  K::MAC,   Sg::SS, Move|Rev},
    {Op::CoMACMR_xp_rnd,   Sh::XP,  K::MAC,   Sg::SS, Move|Rev|Rnd},
    {Op::CoMACMRsu_xp,     Sh::XP,  K::MAC,   Sg::SU, Move|Rev},
    {Op::CoMACMRsu_xp_rnd, Sh::XP,  K::MAC,   Sg::SU, Move|Rev|Rnd},
    {Op::CoMACMRu_xp,      Sh::XP,  K::MAC,   Sg::UU, Move|Rev},
    {Op::CoMACMRu_xp_rnd,  Sh::XP,  K::MAC,   Sg::UU, Move|Rev|Rnd},
    {Op::CoMACMRus_xp,     Sh::XP,  K::MAC,   Sg::US, Move|Rev},
    {Op::CoMACMRus_xp_rnd, Sh::XP,  K::MAC,   Sg::US, Move|Rev|Rnd},
    {Op::CoMACM_xp,        Sh::XP,  K::MAC,   Sg::SS, Move},
    {Op::CoMACM_xp_rnd,    Sh::XP,  K::MAC,   Sg::SS, Move|Rnd},
    {Op::CoMACMsuN_xp,     Sh::XP,  K::MAC,   Sg::SU, Move|Neg},
    {Op::CoMACMsu_xp,      Sh::XP,  K::MAC,   Sg::SU, Move},
    {Op::CoMACMsu_xp_rnd,  Sh::XP,  K::MAC,   Sg::SU, Move|Rnd},
    {Op::CoMACMuN_xp,      Sh::XP,  K::MAC,   Sg::UU, Move|Neg},
    {Op::CoMACMu_xp,       Sh::XP,  K::MAC,   Sg::UU, Move},
    {Op::CoMACMu_xp_rnd,   Sh::XP,  K::MAC,   Sg::UU, Move|Rnd},
    {Op::CoMACMusN_xp,     Sh::XP,  K::MAC,   Sg::US, Move|Neg},
    {Op::CoMACMus_xp,      Sh::XP,  K::MAC,   Sg::US, Move},
    {Op::CoMACMus_xp_rnd,  Sh::XP,  K::MAC,   Sg::US, Move|Rnd},
    {Op::CoMACN_rp,        Sh::RP,  K::MAC,   Sg::SS, Neg},
    {Op::CoMACN_xp,        Sh::XP,  K::MAC,   Sg::SS, Neg},
    {Op::CoMACR_rp,        Sh::RP,  K::MAC,   Sg::SS, Rev},
    {Op::CoMACR_rp_rnd,    Sh::RP,  K::MAC,   Sg::SS, Rev|Rnd},
    {Op::CoMACR_xp,        Sh::XP,  K::MAC,   Sg::SS, Rev},
    {Op::CoMACR_xp_rnd,    Sh::XP,  K::MAC,   Sg::SS, Rev|Rnd},
    {Op::CoMACRsu_rp,      Sh::RP,  K::MAC,   Sg::SU, Rev},
    {Op::CoMACRsu_rp_rnd,  Sh::RP,  K::MAC,   Sg::SU, Rev|Rnd},
    {Op::CoMACRsu_xp,      Sh::XP,  K::MAC,   Sg::SU, Rev},
    {Op::CoMACRsu_xp_rnd,  Sh::XP,  K::MAC,   Sg::SU, Rev|Rnd},
    {Op::CoMACRu_rp,       Sh::RP,  K::MAC,   Sg::UU, Rev},
    {Op::CoMACRu_rp_rnd,   Sh::RP,  K::MAC,   Sg::UU, Rev|Rnd},
    {Op::CoMACRu_xp,       Sh::XP,  K::MAC,   Sg::UU, Rev},
    {Op::CoMACRu_xp_rnd,   Sh::XP,  K::MAC,   Sg::UU, Rev|Rnd},
    {Op::CoMACRus_rp,      Sh::RP,  K::MAC,   Sg::US, Rev},
    {Op::CoMACRus_rp_rnd,  Sh::RP,  K::MAC,   Sg::US, Rev|Rnd},
    {Op::CoMACRus_xp,      Sh::XP,  K::MAC,   Sg::US, Rev},
    {Op::CoMACRus_xp_rnd,  Sh::XP,  K::MAC,   Sg::US, Rev|Rnd},
    {Op::CoMAC_rp,         Sh::RP,  K::MAC,   Sg::SS, 0},
    {Op::CoMAC_rp_rnd,     Sh::RP,  K::MAC,   Sg::SS, Rnd},
    {Op::CoMAC_xp,         Sh::XP,  K::MAC,   Sg::SS, 0},
    {Op::CoMAC_xp_rnd,     Sh::XP,  K::MAC,   Sg::SS, Rnd},
    {Op::CoMACsuN_rp,      Sh::RP,  K::MAC,   Sg::SU, Neg},
    {Op::CoMACsuN_xp,      Sh::XP,  K::MAC,   Sg::SU, Neg},
    {Op::CoMACsu_rp,       Sh::RP,  K::MAC,   Sg::SU, 0},
    {Op::CoMACsu_rp_rnd,   Sh::RP,  K::MAC,   Sg::SU, Rnd},
    {Op::CoMACsu_xp,       Sh::XP,  K::MAC,   Sg::SU, 0},
    {Op::CoMACsu_xp_rnd,   Sh::XP,  K::MAC,   Sg::SU, Rnd},
    {Op::CoMACuN_rp,       Sh::RP,  K::MAC,   Sg::UU, Neg},
    {Op::CoMACuN_xp,       Sh::XP,  K::MAC,   Sg::UU, Neg},
    {Op::CoMACu_rp,        Sh::RP,  K::MAC,   Sg::UU, 0},
    {Op::CoMACu_rp_rnd,    Sh::RP,  K::MAC,   Sg::UU, Rnd},
    {Op::CoMACu_xp,        Sh::XP,  K::MAC,   Sg::UU, 0},
    {Op::CoMACu_xp_rnd,    Sh::XP,  K::MAC,   Sg::UU, Rnd},
    {Op::CoMACusN_rp,      Sh::RP,  K::MAC,   Sg::US, Neg},
    {Op::CoMACusN_xp,      Sh::XP,  K::MAC,   Sg::US, Neg},
    {Op::CoMACus_rp,       Sh::RP,  K::MAC,   Sg::US, 0},
    {Op::CoMACus_rp_rnd,   Sh::RP,  K::MAC,   Sg::US, Rnd},
    {Op::CoMACus_xp,       Sh::XP,  K::MAC,   Sg::US, 0},
    {Op::CoMACus_xp_rnd,   Sh::XP,  K::MAC,   Sg::US, Rnd},
    {Op::CoMAX_rp,         Sh::RP,  K::MAX,   Sg::SS, 0},
    {Op::CoMAX_xp,         Sh::XP,  K::MAX,   Sg::SS, 0},
    {Op::CoMIN_rp,         Sh::RP,  K::MIN,   Sg::SS, 0},
    {Op::CoMIN_xp,         Sh::XP,  K::MIN,   Sg::SS, 0},
    {Op::CoMOV_xp,         Sh::XP,  K::MOV,   Sg::SS, 0},
    {Op::CoNOP_q,          Sh::Q,   K::NOP,   Sg::SS, 0},
    {Op::CoNOP_x,          Sh::X,   K::NOP,   Sg::SS, 0},
    {Op::CoNOP_xp,         Sh::XP,  K::NOP,   Sg::SS, 0},
    {Op::CoSHL_p,          Sh::P,   K::SHL,   Sg::SS, 0},
    {Op::CoSHL_r,          Sh::R,   K::SHL,   Sg::SS, 0},
    {Op::CoSHR_p,          Sh::P,   K::SHR,   Sg::SS, 0},
    {Op::CoSHR_r,          Sh::R,   K::SHR,   Sg::SS, 0},
    {Op::CoSTORE_sp,       Sh::SP,  K::STORE, Sg::SS, 0},
    {Op::CoSUB2R_rp,       Sh::RP,  K::SUB,   Sg::SS, Rev|Dbl},
    {Op::CoSUB2R_xp,       Sh::XP,  K::SUB,   Sg::SS, Rev|Dbl},
    {Op::CoSUB2_rp,        Sh::RP,  K::SUB,   Sg::SS, Dbl},
    {Op::CoSUB2_xp,        Sh::XP,  K::SUB,   Sg::SS, Dbl},
    {Op::CoSUBR_rp,        Sh::RP,  K::SUB,   Sg::SS, Rev},
    {Op::CoSUBR_xp,        Sh::XP,  K::SUB,   Sg::SS, Rev},
    {Op::CoSUB_rp,         Sh::RP,  K::SUB,   Sg::SS, 0},
    {Op::CoSUB_xp,         Sh::XP,  K::SUB,   Sg::SS, 0},
};

static const CoForm *coFormFor(Op O) {
  for (const CoForm &F : CoForms)
    if (F.O == O)
      return &F;
  return nullptr;
}

/// How many times a repeatable coprocessor form runs.
///
/// The repeat field is five bits: zero is the plain form and runs once, one
/// means take the count from MRW - which the manual gives as (MRW[12:0]) + 1,
/// so up to 8192 - and anything else is the count itself.  MRW is storage at
/// FFDAH here, like every other register the simulator gives no behaviour to,
/// which is all a program setting it and the unit reading it needs.
uint64_t coRepeatCount(Machine &M, const MCInst &MI, unsigned RepOp) {
  if (RepOp >= MI.getNumOperands() || !MI.getOperand(RepOp).isImm())
    return 1;
  int64_t Field = MI.getOperand(RepOp).getImm();
  if (Field == 1)
    return (M.read16(0xFFDA) & 0x1FFF) + 1;
  return Field > 1 ? uint64_t(Field) : 1;
}

/// Where the repeat count sits for each shape, which is always last.
unsigned coRepeatOperand(Sh::Shape Shape) {
  switch (Shape) {
  case Sh::RP:
  case Sh::SP:
    return 3;
  case Sh::XP:
    return 4;
  case Sh::P:
  case Sh::Q:
  case Sh::X:
    return 2;
  case Sh::R:
    return 1;
  }
  return 0;
}

/// Which part is being simulated, which the decoder needs because the same
/// short address names different special function registers on different
/// derivatives - so "mov cpucon1, #x" and "mov addrsel1, #x" are the same
/// bytes and only the part says which was meant.
///
/// The default is the XC164CM, because that is the part this simulator models:
/// CPUCON1's vector spacing, VECSEG, the PLL and the coprocessor are all its.
/// A program for another derivative says so with --mcpu.
static std::string SimCPU = "xc16x";

/// What the part has on top of its core, in the same spelling the compiler
/// takes: the coprocessor is a feature rather than a core, so an ST10 that has
/// one is "--mcpu=st10 --mattr=+mac" here exactly as it is "-mcpu=st10 -mmac"
/// there.  Without this the decoder built for such a part would refuse the
/// instructions the compiler had just emitted for it.
static std::string SimFeatures;

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
    STI.reset(T->createMCSubtargetInfo(TT, SimCPU, SimFeatures));
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

void c166sim::setSimCPU(StringRef CPU, StringRef Features) {
  SimCPU = CPU.str();
  SimFeatures = Features.str();
}

bool c166sim::simCPUHasVectorRegs() { return SimCPU == "xc16x"; }

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
    return M.gprAddress(2 * (BitOff - 0xF0));
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

/// What an instruction costs in states, executing from the internal program
/// memory.
///
/// The figures are the C166 Family Instruction Set Manual's, chapter 7
/// ("Instruction State Times"), version 2.0 of March 2001.  One state is one
/// CPU clock period.  Table 11 gives the minimum times below; everything not
/// named there takes two states, which is most of the instruction set.
///
/// The branches carry two figures, "4 or 2".  The larger is the cost of
/// actually branching, because the instruction stream is broken and the
/// pipeline has to refill; the smaller is what an untaken conditional costs.
/// \p Taken says which happened.
static unsigned baseStateTime(Op O, bool Taken) {
  switch (O) {
  // MUL, MULU.
  case Op::MULrr:
  case Op::MULUrr:
    return 10;
  // DIV, DIVL, DIVU, DIVLU - the slowest instructions on the part.
  case Op::DIVr:
  case Op::DIVUr:
  case Op::DIVLr:
  case Op::DIVLUr:
    return 20;
  // CALLS, CALLR, PCALL, JMPS, TRAP: always four, no untaken form.
  case Op::CALLS:
  case Op::CALLR:
  case Op::PCALL:
  case Op::JMPS:
  case Op::TRAP:
    return 4;
  // RET, RETI, RETP, RETS.
  case Op::RET:
  case Op::RETI:
  case Op::RETP:
  case Op::RETS:
    return 4;
  // CALLI, CALLA, JMPA, JMPI, JMPR: four when taken, two when not.
  case Op::CALLI:
  case Op::CALLA:
  case Op::JMPA:
  case Op::JMPAcc:
  case Op::JMPI:
  case Op::JMPR:
  case Op::JMPRcc:
    return Taken ? 4 : 2;
  // JB, JBC, JNB, JNBS, the bit branches, likewise.
  case Op::JB:
  case Op::JBC:
  case Op::JNB:
  case Op::JNBS:
    return Taken ? 4 : 2;
  // MOV[B] Rn, [Rm + #data16], the one move that is not two states.
  case Op::MOV16rm:
  case Op::MOVB8rm:
    return 4;
  // The MAC unit.  Table 11 is the original C166's and says nothing about a
  // coprocessor that core does not have, so these come from the C166S V2
  // Architecture Overview Handbook's instruction set summary, which counts in
  // cycles rather than states: CoLOAD, CoMUL, CoMAC and CoSTORE are one cycle
  // each, and the rounding forms two.  One cycle is two states - the same
  // table gives "MOV mem, reg" as 4 bytes and 1 cycle, and Table 11 gives it
  // two states - so a plain MAC instruction costs what any ordinary
  // instruction does.  That it agrees with the default below is worth saying
  // out loud rather than leaving to chance: the figure is read, not assumed.
  case Op::CoLOAD_rr:
  case Op::CoMUL_rr:
  case Op::CoMULu_rr:
  case Op::CoMAC_rr:
  case Op::CoMACu_rr:
  case Op::CoMACN_rr:
  case Op::CoMACuN_rr:
  case Op::CoMAX_rr:
  case Op::CoMIN_rr:
  case Op::CoSTORE_sr:
    return 2;
  // The same table gives the rounding forms two cycles rather than one, so
  // these are four states where the plain ones are two.
  case Op::CoMUL_rr_rnd:
  case Op::CoMULu_rr_rnd:
    return 4;
  default:
    return 2;
  }
}

/// Whether \p O reads PSW as an operand rather than as condition flags, which
/// is what section 7.3 charges two states for when the flags were just
/// written.  The bit instructions reach PSW through its bit addresses.
static bool readsPSWAsOperand(Op O) {
  switch (O) {
  case Op::BAND:
  case Op::BOR:
  case Op::BXOR:
  case Op::BMOV:
  case Op::BCMP:
    return true;
  default:
    return false;
  }
}

/// Whether \p O is a conditional branch, which pays a state when the
/// instruction before it wrote PSW (section 7.3, "Testing Branch Conditions").
static bool isConditionalBranch(Op O) {
  switch (O) {
  case Op::JMPAcc:
  case Op::JMPR:
  case Op::JMPRcc:
  case Op::JB:
  case Op::JBC:
  case Op::JNB:
  case Op::JNBS:
    return true;
  default:
    return false;
  }
}

/// Save the interrupted state and branch to a vector table entry.
///
/// This is what TRAP does and what accepting an interrupt does; the core does
/// not distinguish them, which is why the two share it.  CSP is pushed because
/// segmentation is on, the only mode this simulator runs in, and RETI undoes
/// all three pushes in the opposite order.
///
/// Where the entry is comes from two registers on this core rather than being
/// fixed: the table is at the low end of the segment VECSEG names, and
/// CPUCON1's VECSC field says how many words apart the entries are - 2, 4, 8
/// or 16, so 4 bytes a vector at its reset value.  An older part had neither,
/// and behaved as these do out of reset.  The entry is branched *to*, not read
/// through: it is where a project puts a jump to its handler.
static void enterVector(Machine &M, unsigned Vector) {
  unsigned Space = 4u << ((M.CPUCON1 >> 5) & 3);
  M.push(M.PSW);
  M.push(M.CSP);
  M.CSP = M.VECSEG;
  M.push(M.IP);
  M.IP = uint16_t(Space * (Vector & 0x7F));
}

/// The three pipeline effects of section 4.2 that a program has to leave a gap
/// for, checked against the instruction that has just run.
///
/// Each is reported rather than reproduced.  The part does not report them: it
/// uses the value the pipeline still holds and carries on, so a program that
/// gets one wrong gets a wrong answer with nothing said.  What can be checked
/// is the sequence the manual says must not be written, and that is what this
/// does - which is the whole reason it exists, because the two hazards the
/// compiler already avoids were avoided on the strength of reading the manual
/// and nothing in this tree could tell whether the avoidance worked.
static void checkPipelineGap(Machine &M, Op O) {
  if (!M.PipelineEffects)
    return;

  const char *Why = nullptr;
  switch (M.LastWrite.What) {
  case PipelineWrite::Nothing:
    break;
  case PipelineWrite::ContextPointer:
    // "An instruction, which calculates a physical GPR operand address via the
    // CP register, is mostly not capable of using a new CP value, which is to
    // be updated by an immediately preceding instruction."
    if (M.UsedGPR)
      Why = "a general purpose register is read or written by the instruction "
            "after the one that wrote CP, so it is the old context that is "
            "addressed; at least one instruction has to come between them "
            "(C167CR Derivatives User's Manual V3.1, section 4.2, Context "
            "Pointer Updating)";
    break;
  case PipelineWrite::DataPage:
    // "An instruction, which calculates a physical operand address via a
    // particular DPPn register, is mostly not capable of using a new DPPn
    // register value, which is to be updated by an immediately preceding
    // instruction."
    if (M.UsedDPPMask & (1u << M.LastWrite.Which))
      Why = "a long or indirect address goes through the data page pointer "
            "that the instruction before this one wrote, so it is the old page "
            "that is addressed; at least one instruction has to come between "
            "them (C167CR Derivatives User's Manual V3.1, section 4.2, Data "
            "Page Pointer Updating)";
    break;
  case PipelineWrite::StackPointer:
    // "None of the RET, RETI, RETS, RETP or POP instructions is capable of
    // correctly using a new SP register value, which is to be updated by an
    // immediately preceding instruction."
    if (O == Op::RET || O == Op::RETI || O == Op::RETS || O == Op::RETP ||
        O == Op::POP)
      Why = "this pops the system stack immediately after SP was written, so "
            "it is the old stack pointer that is used; at least one "
            "instruction has to come between them (C167CR Derivatives User's "
            "Manual V3.1, section 4.2, Explicit Stack Pointer Updating)";
    break;
  }

  if (Why) {
    M.Stop = StopReason::BadSequence;
    M.StopDetail = Why;
    return;
  }

  M.LastWrite = M.ThisWrite;
  M.ThisWrite = PipelineWrite();
}

/// What accepting an interrupt costs, in states.
///
/// It does exactly what TRAP does, so it is charged what TRAP is charged.  The
/// part's real response time is longer - arbitration, and the pipeline being
/// thrown away - but both depend on what was in flight rather than on the
/// program, in the same way as the penalties section 7.3 leaves out.  So this
/// is a lower bound, which is what everything else in States already is.
static constexpr unsigned InterruptEntryStates = 4;

/// The interrupt control registers of the sources this simulator has
/// peripherals for, and the vector each reaches.  Everything else in the
/// interrupt table is still a command line injection.
///
/// The register itself is storage, which is what an unmodelled peripheral
/// register already was; what makes these different is that something sets the
/// request flag in them and that arbitration reads them.  So the layout below
/// is the manual's - xxIR at bit 7, xxIE at 6, GLVL at 5-4 and ILVL at 3-0 -
/// and a program configures one exactly as it would on the part.
struct PeripheralSource {
  uint32_t IC;
  unsigned Vector;
};
static constexpr PeripheralSource PeripheralSources[] = {
    {0xFF60, 34}, // T2IC, GPT1 timer 2
    {0xFF62, 35}, // T3IC, GPT1 timer 3
    {0xFF64, 36}, // T4IC, GPT1 timer 4
};

/// What a PEC transfer costs, in states.
///
/// "In cycle 3 a PEC transfer 'instruction' is injected into the decode stage
/// of the pipeline, suspending instruction N + 1 ... Cycle 4 completes the
/// injected PEC transfer and resumes the execution of instruction N + 1"
/// (C167CR Derivatives User's Manual V3.1, section 5.6).  So it costs what an
/// instruction costs, which is the same lower bound InterruptEntryStates is:
/// the response time the manual quotes includes arbitration and the pipeline,
/// and both depend on what was in flight rather than on the program.
static constexpr unsigned PECTransferStates = 2;

/// The eight channel control registers, PECC0 at FEC0H and one every two bytes
/// after it (Table 5-4), and the source and destination pointers, which are
/// not registers at all: "these pointers do not reside in specific SFRs, but
/// are mapped into the internal RAM ... just below the bit-addressable area"
/// (Figure 5-2), so SRCPx is at FCE0H + 4x and DSTPx two bytes above it.
///
/// Both are ordinary storage here, which is what they are on the part.  What
/// makes the PEC a peripheral rather than sixteen more words of RAM is that
/// something reads them when a request wins arbitration, which is below.
enum { PECCBase = 0xFEC0, PECPointerBase = 0xFCE0 };

/// One request in the running, whichever kind of source raised it.
///
/// A peripheral has a group level and an injected source does not, so an
/// injected one arbitrates as group 0, the lowest.  That is the honest
/// reading: the group level is a field of a control register, and a source
/// declared on the command line has no control register to put one in.
struct Candidate {
  unsigned Level = 0;
  unsigned Group = 0;
  unsigned Vector = 0;
  InterruptSource *Injected = nullptr;
  uint32_t IC = 0;

  /// "Defines the internal order for simultaneous requests of the same
  /// priority.  3: Highest group priority" - so a higher group wins a tie, and
  /// what is left after that is the order these were collected in, which
  /// stands in for the node number the part breaks the last tie with.
  bool beats(const Candidate &O) const {
    return Level != O.Level ? Level > O.Level : Group > O.Group;
  }
};

/// Service a request through its PEC channel, or return false where the
/// channel has run out and a handler should run instead.
///
/// The transfer itself is one byte or word "between two locations in segment 0
/// (data pages 3 ... 0)", so each pointer is a physical address in that
/// segment rather than a near address through a data page pointer - which is
/// the opposite of what every other address a program writes goes through, and
/// is why nothing here calls mapData.
///
/// What COUNT does is Table 5-5's, which has four rows and needs all four: FFH
/// is continuous and is not decremented, FEH to 02H decrement, 01H decrements
/// to zero and leaves the request flag set - "which triggers another request",
/// and is how a program is told the block is done - and 00H is not serviced
/// here at all.
static bool performPEC(Machine &M, const Candidate &C) {
  unsigned Channel = ((C.Level & 1) << 2) | C.Group;
  uint32_t PECC = PECCBase + 2 * Channel;
  uint16_t Control = M.read16(PECC);
  unsigned Count = Control & 0xFF;
  if (Count == 0)
    return false;

  bool Byte = (Control >> 8) & 1;
  // "0 0: Pointers are not modified.  0 1: Increment DSTPx by 1 or 2 (BWT).
  // 1 0: Increment SRCPx by 1 or 2 (BWT).  1 1: Reserved.  Do not use this
  // combination.  (changed to '10' by hardware)" - so three behaves as two.
  // The register is left holding what was written: the manual says the
  // combination is changed but not when, and inventing an answer to that would
  // be a claim about a read back that nothing here can check.
  unsigned Inc = (Control >> 9) & 3;

  uint32_t SrcAddr = PECPointerBase + 4 * Channel;
  uint32_t DstAddr = SrcAddr + 2;
  uint16_t Src = M.read16(SrcAddr);
  uint16_t Dst = M.read16(DstAddr);

  if (Byte)
    M.write8(Dst, M.read8(Src));
  else
    M.write16(Dst, M.read16(Src));

  uint16_t Step = Byte ? 1 : 2;
  if (Inc == 1)
    M.write16(DstAddr, uint16_t(Dst + Step));
  else if (Inc >= 2)
    M.write16(SrcAddr, uint16_t(Src + Step));

  bool LeaveRequest = false;
  if (Count != 0xFF) {
    --Count;
    M.write16(PECC, uint16_t((Control & ~0xFFu) | Count));
    LeaveRequest = Count == 0;
  }

  // "It is cleared automatically ... upon a PEC service.  In the case of PEC
  // service the Interrupt Request flag remains set, if the COUNT field ...
  // decrements to zero.  This allows a normal CPU interrupt to respond to a
  // completed PEC block transfer."
  if (!LeaveRequest) {
    if (C.Injected)
      C.Injected->Pending = false;
    else
      M.write16(C.IC, M.read16(C.IC) & ~uint16_t(1u << 7));
  }
  return true;
}

bool Machine::serviceInterrupts() {
  // The peripherals run on the clock whether or not anything is listening, so
  // they are advanced before the question of whether an interrupt can be taken
  // is asked at all.
  if (TimersOn) {
    advanceTimers();
    if (Stop != StopReason::Running)
      return false;
  }

  if (Interrupts.empty() && !TimersOn)
    return false;

  // Raise whatever is due.  A source that is still pending when its period
  // comes round again does not stack up a second request: one request flag
  // cannot hold two, and the part loses the repetition rather than
  // remembering it.
  for (InterruptSource &S : Interrupts) {
    if (States < S.At)
      continue;
    S.Pending = true;
    if (!S.Period) {
      S.At = InterruptSource::Never;
      continue;
    }
    do
      S.At += S.Period;
    while (S.At <= States);
  }

  // An ATOMIC or an EXTend covers the instructions after it, and locking
  // interrupts out is the whole point of ATOMIC.  The request stays pending
  // and is taken once the sequence has run out.
  if (ExtendCount > 0 || Extend != ExtendKind::None)
    return false;
  // Both of these are read from PSW as it stood one instruction ago, because
  // "the current interrupt prioritization round does not consider" a change
  // the instruction that has just retired made to either.  That one
  // instruction of delay is why the compare-and-exchange sequence needs a NOP
  // between clearing IEN and the read it protects.
  uint16_t Arb = PipelineEffects ? IntPSW : PSW;
  if (!((Arb >> PSW_IEN) & 1))
    return false;

  // Arbitration: the highest priority pending request wins, and a tie goes to
  // the higher group level.  Both kinds of source are in it together - an
  // injected request and a timer's are the same thing by the time they reach
  // the CPU, which is the point of the peripheral half existing at all.
  Candidate Winner;
  bool Any = false;
  auto consider = [&](const Candidate &C) {
    if (!Any || C.beats(Winner)) {
      Winner = C;
      Any = true;
    }
  };
  for (InterruptSource &S : Interrupts)
    if (S.Pending) {
      Candidate C;
      C.Level = S.Level;
      C.Vector = S.Vector;
      C.Injected = &S;
      consider(C);
    }
  for (const PeripheralSource &P : PeripheralSources) {
    uint16_t IC = read16(P.IC);
    if (!((IC >> 7) & 1) || !((IC >> 6) & 1))
      continue;
    Candidate C;
    C.Level = IC & 0xF;
    C.Group = (IC >> 4) & 3;
    C.Vector = P.Vector;
    C.IC = P.IC;
    consider(C);
  }
  if (!Any)
    return false;

  // "On receipt of the arbitration interrupt request winner, the CPU accepts
  // an action request if the requesting source has a higher priority than the
  // current CPU priority level.  If the requesting source has a lower (or
  // equal) interrupt priority than the current CPU task, it remains pending."
  // (C166S V2 Architecture Overview Handbook, section 7.2.)  So the comparison
  // is strict, and losing it is not losing the request.
  if (Winner.Level <= ((Arb >> PSWILVLShift) & 0xF))
    return false;

  // A request on level 15 or 14 goes to the Peripheral Event Controller
  // rather than to a handler, unless the channel it names has run out.
  //
  // "Interrupt requests that are programmed to priority levels 15 or 14 (i.e.
  // ILVL = 111XB) will be serviced by the PEC, unless the COUNT field of the
  // associated PECC register contains zero.  In this case the request will
  // instead be serviced by normal interrupt processing" - and "the associated
  // PEC channel number is derived from the respective ILVL (LSB) and GLVL", so
  // level 15 selects channels 7 to 4 and level 14 channels 3 to 0, with the
  // group level choosing within the four.
  //
  // The arbitration above needs no change for this.  "Simultaneous requests
  // for PEC channels are prioritized according to the PEC channel number,
  // where channel 0 has lowest and channel 8 has highest priority" - and the
  // channel number is built from exactly the two fields the round already
  // compared, in the same order, so the winner by level and group is the
  // winner by channel.
  if (Winner.Level >= 14 && performPEC(*this, Winner)) {
    ++PECTransfers;
    States += PECTransferStates;
    return true;
  }

  // "It is cleared automatically upon entry into the interrupt service
  // routine" - so the request flag goes down here, and a handler that wants
  // another one does not have to clear anything itself.
  if (Winner.Injected)
    Winner.Injected->Pending = false;
  else
    write16(Winner.IC, read16(Winner.IC) & ~uint16_t(1u << 7));
  enterVector(*this, Winner.Vector);
  if (Stop != StopReason::Running)
    return true;
  // The CPU now runs at the accepted source's priority, which is what stops a
  // handler being interrupted by its own level or below.  IEN is left alone:
  // on this core the priority level is the whole of the nesting rule, and a
  // handler that wants no interrupts at all raises the level rather than
  // clearing the enable.  RETI puts back the pushed PSW, and with it both.
  setCPUPriority(Winner.Level);
  // Entering a handler is the hardware's doing rather than a software write to
  // PSW, so the level it sets is arbitrated on at once; letting it lag would
  // let a second request of the same level in before the handler's first
  // instruction.
  IntPSW = PSW;
  ++InterruptsTaken;
  States += InterruptEntryStates;
  // Nothing was decoded, so nothing can be covered by an extend and nothing
  // wrote PSW as an instruction would.  The handler starts clean.
  PrevWrotePSW = false;
  return true;
}

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

  // Between instructions is where the core accepts a request, so this is where
  // one is offered.  Entering a handler is not executing an instruction: it
  // costs states but not steps, and the instruction that would have run this
  // step runs after the handler returns.  It is offered after the exit check
  // rather than before, so that a program which has finished stays finished
  // and a periodic source cannot keep it alive.
  if (serviceInterrupts())
    return Stop == StopReason::Running;

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

  // What the flags and the SFR space looked like going in, so that the
  // penalties section 7.3 charges against the *following* instruction can be
  // charged to this one.
  bool WasPSWWrite = PrevWrotePSW;
  uint16_t FallThroughIP = IP, CSPBefore = CSP;
  // Whether PSW is *written*, which is what section 7.3 charges for - not
  // whether its value changed.  "sub r1, #1" on 4 leaves every flag as it
  // found it and still costs the branch after it a state.  setFlag() is the
  // one place a flag is written, so asking there cannot drift from what the
  // simulator actually models.
  WrotePSW = false;

  // What arbitration will see after this instruction has run, which is PSW as
  // it stands before it does.
  uint16_t PSWBefore = PSW;
  UsedGPR = false;
  UsedDPPMask = 0;

  executeOne(*this, MI, O, PC);

  IntPSW = PSWBefore;
  // Not an early return: the instruction ran, so it is charged for below the
  // same way one that stopped the machine for any other reason is.
  checkPipelineGap(*this, O);

  // A branch was taken if control did not fall through to the next
  // instruction.  That is what separates the two figures Table 11 gives for
  // the branches, and it is the same test for every one of them.
  bool Taken = IP != FallThroughIP || CSP != CSPBefore;
  unsigned Cost = baseStateTime(O, Taken);

  // A repeated coprocessor instruction is fetched once and executed as many
  // times as its count says, so it costs that many.  Charging it as one would
  // make --count-states report a filter as free, which is the opposite of the
  // thing that measurement exists to show.  The repetitions do not refetch,
  // so this is the same lower bound the rest of this is: two states each.
  if (const CoForm *F = coFormFor(O))
    Cost *= coRepeatCount(*this, MI, coRepeatOperand(F->Shape));

  // Section 7.3 adds to that in a handful of cases.  Two of them are charged
  // here, both about PSW, because they are the ones this can decide exactly:
  // a conditional branch pays a state when the instruction before it wrote
  // PSW, and reading PSW as an operand pays two.
  //
  // The rest are not modelled, and each for a reason rather than an
  // oversight.  Operand reads from the internal program memory, and every
  // figure quoted in ALE cycle times, describe running from RAM or through
  // the external bus controller: they depend on the bus mode and the
  // programmed waitstates, which are a fact about a board and not about a
  // program.  The indirect-IRAM-read-after-an-auto-increment penalty needs
  // the addressing mode of the previous instruction rather than its effect,
  // and the two SFR ones need to know that this instruction's operand is an
  // SFR rather than that it touched one - the register bank is memory here,
  // so a GPR access looks the same from underneath.
  //
  // So this is a lower bound on the time, exact for straight-line code in
  // Flash and optimistic by a state here and there elsewhere.
  if (WasPSWWrite && isConditionalBranch(O))
    Cost += 1;
  if (WasPSWWrite && readsPSWAsOperand(O))
    Cost += 2;
  States += Cost;

  PrevWrotePSW = WrotePSW;

  retireExtend();
  checkUserStack();
  return Stop == StopReason::Running;
}

void Machine::checkUserStack() {
  if (!HasUserStackLimit || Stop != StopReason::Running)
    return;
  uint16_t SP0 = getWordReg(0);
  // Arm on the first sight of a sane value, and remember which register bank it
  // was in.  R0 is zero out of reset and stays there until the startup code
  // loads it from __user_stack_top, and those instructions are not an
  // overflow.
  if (!UserStackArmed) {
    if (SP0 < UserStackLimit)
      return;
    UserStackArmed = true;
    UserStackCP = CP;
    return;
  }
  // "R0" is whichever sixteen words CP is pointing at, so in a handler with a
  // register bank of its own it is a different register that happens to have
  // the same name - and one the prologue only loads with the stack pointer if
  // the handler needs it, so it usually holds whatever was left in that bank.
  // Zero, on the first entry to a bank nothing has used.  That is not an
  // overflow, and the way to not report it as one is to check only in the bank
  // the ABI stack pointer belongs to.
  //
  // The cost is that a banked handler which does use the stack is not watched.
  // It is a handler: it runs on the same stack, so what it spends shows up in
  // the checks of everything it interrupted.
  if (CP != UserStackCP)
    return;
  if (SP0 < UserStackLow)
    UserStackLow = SP0;
  if (SP0 >= UserStackLimit || !StopOnUserStackOverflow)
    return;
  Stop = StopReason::StackFault;
  StopDetail = (Twine("ABI stack overflow: R0 is ") + addrStr(SP0) +
                ", below __user_stack_limit at " + addrStr(UserStackLimit) +
                ".  Nothing on the part checks this - see -mstack-check and "
                "llvm/lib/Target/C166/startup/README.txt")
                   .str();
}

namespace {

/// Where a special function register lives, from the short address it
/// encodes: the ordinary space runs from FE00H, two bytes per short address.
/// IDX0 and IDX1 are reached this way rather than modelled as fields, because
/// on the part they are special function registers and a program sets them by
/// writing to them.
uint32_t sfrAddress(const MCRegisterInfo &MRI, MCRegister R) {
  return 0xFE00u + 2 * MRI.getEncodingValue(R);
}

/// The coprocessor's four offset registers, which are in the extended space
/// at F000H and are the same four on every part that has the unit - see the
/// ST10F269 data sheet's register table, which agrees with the XC164CM's.
/// Storage here, like everything else the simulator does not give behaviour
/// to, which is all a program setting one and the unit reading it needs.
enum { CoQX0 = 0xF000, CoQX1 = 0xF002, CoQR0 = 0xF004, CoQR1 = 0xF006 };

/// The internal dual-port RAM, which is 2 KByte from F600H on every part this
/// simulator runs.  It matters because it is the only memory IDX0 and IDX1
/// reach: PM0036 section 2.1 says "the GPR pointer gives access to the entire
/// memory space, whereas IDXi are limited to the internal Dual-Port RAM,
/// except for the CoMOV instruction".  Nothing about the encoding says so, so
/// an IDX pointed at ordinary static data assembles, links, and on the part
/// reads whatever the unit sees instead - which is why this is checked here
/// rather than left to be discovered on silicon.  __dpram is what puts an
/// array where these can reach it.
enum { DPRamStart = 0xF600, DPRamEnd = 0xFDFF };

/// Whether an IDX pointer may be used for this access, stopping the machine
/// with what is wrong if not.  The pointer steps, so this is asked on every
/// repetition rather than once: walking off the end of the dual-port RAM is
/// the failure worth catching.
///
/// The manual also says IDXi holds even values only, bit 0 always reading as
/// zero.  That is the register masking what is written rather than refusing
/// it, and nothing here models the masking, so an odd pointer is not checked:
/// asserting an error the part does not raise would be worse than saying
/// nothing.
bool checkIdxPointer(Machine &M, uint16_t Idx) {
  if (Idx < DPRamStart || Idx > DPRamEnd - 1) {
    M.Stop = StopReason::BadAccess;
    M.StopDetail = "an IDX pointer outside the dual-port RAM, which is the only "
                   "memory it reaches - see __dpram";
    return false;
  }
  return true;
}

/// What a pointer does to itself after the access, from the update code the
/// operand carries.  The codes are PM0036 Table 31's, and the same seven
/// serve both pointer kinds - only which pair of offset registers they name
/// differs, QX for an IDX and QR for a general purpose register.
int32_t coPointerStep(Machine &M, unsigned Update, bool IsIdx) {
  switch (Update) {
  case 1:
    return 0; // [Rw], which leaves the pointer alone
  case 2:
    return 2; // [Rw+], a word forward
  case 3:
    return -2; // [Rw-]
  case 4:
    return int16_t(M.read16(IsIdx ? CoQX0 : CoQR0));
  case 5:
    return -int32_t(int16_t(M.read16(IsIdx ? CoQX0 : CoQR0)));
  case 6:
    return int16_t(M.read16(IsIdx ? CoQX1 : CoQR1));
  case 7:
    return -int32_t(int16_t(M.read16(IsIdx ? CoQX1 : CoQR1)));
  default:
    return 0;
  }
}

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
    return M.gprAddress(2 * W);
  return M.regFieldAddress(MRI.getEncodingValue(R));
}

/// The two things a sequence covered by ATOMIC or an EXTend must not contain.
///
/// The hardware keeps one instruction counter and it keeps counting whatever
/// runs next.  An instruction that changes the flow carries the rest of the
/// count off to wherever it goes, so the protection lands on instructions
/// nobody meant to protect and stops covering the ones that needed it; a
/// second extend overwrites the count the first one was still using.  The
/// manual says not to do either.  This says so out loud when it happens,
/// which turns a rule the compiler has to remember into one it is held to.
static bool breaksExtendSequence(Op O) {
  switch (O) {
  case Op::JMPA:
  case Op::JMPAcc:
  case Op::JMPR:
  case Op::JMPRcc:
  case Op::JMPI:
  case Op::JMPS:
  case Op::JB:
  case Op::JNB:
  case Op::JBC:
  case Op::JNBS:
  case Op::CALLA:
  case Op::CALLI:
  case Op::CALLS:
  case Op::CALLR:
  case Op::PCALL:
  case Op::RETP:
  case Op::TRAP:
  case Op::RET:
  case Op::RETI:
  case Op::RETS:
  case Op::EXTSi:
  case Op::EXTSr:
  case Op::EXTSRi:
  case Op::EXTSRr:
  case Op::EXTPi:
  case Op::EXTPr:
  case Op::EXTPRi:
  case Op::EXTPRr:
  case Op::EXTR:
  case Op::ATOMIC:
    return true;
  default:
    return false;
  }
}

/// One of the 89 repeatable coprocessor forms, run as many times as its
/// repeat field says.
///
/// The field is five bits: zero is the plain form and runs once, one means
/// take the count from MRW - the manual gives that as (MRW[12:0]) + 1 - and
/// anything else is the count itself.  The pointers step between repetitions,
/// which is the whole point of repeating: each pass reads the next word.
void executeCoRepeatable(Machine &M, const MCInst &MI, Op O) {
  const CoForm *F = coFormFor(O);
  if (!F) {
    M.Stop = StopReason::Unsupported;
    M.StopDetail = "a coprocessor form with no entry in CoForms";
    return;
  }
  const MCRegisterInfo &MRI = *decoder().MRI;

  // Where each shape keeps its operands.  A pointer is two operands, the
  // register and the update code; the repeat count is always last.
  unsigned IdxOp = ~0u, PtrOp = ~0u, RegOp = ~0u, CoRegOp = ~0u;
  unsigned RepOp = coRepeatOperand(F->Shape);
  switch (F->Shape) {
  case Sh::RP:
    RegOp = 0; PtrOp = 1;
    break;
  case Sh::XP:
    IdxOp = 0; PtrOp = 2;
    break;
  case Sh::P:
  case Sh::Q:
    PtrOp = 0;
    break;
  case Sh::R:
    RegOp = 0;
    break;
  case Sh::X:
    IdxOp = 0;
    break;
  case Sh::SP:
    PtrOp = 0; CoRegOp = 2;
    break;
  }

  uint64_t Count = coRepeatCount(M, MI, RepOp);

  // The pointers are read once and written back once, because a repetition
  // steps the value this instruction holds rather than re-reading a register
  // a previous repetition wrote.
  uint32_t IdxAddr = 0, PtrAddr = 0;
  uint16_t IdxVal = 0, PtrVal = 0;
  int32_t IdxStep = 0, PtrStep = 0;
  if (IdxOp != ~0u) {
    IdxAddr = sfrAddress(MRI, MI.getOperand(IdxOp).getReg());
    IdxVal = M.read16(IdxAddr);
    IdxStep = coPointerStep(M, MI.getOperand(IdxOp + 1).getImm(), true);
  }
  if (PtrOp != ~0u) {
    unsigned N = wordRegIndex(MRI, MI.getOperand(PtrOp).getReg());
    if (N == ~0u) {
      M.Stop = StopReason::Unsupported;
      M.StopDetail = "a coprocessor pointer that is not a word register";
      return;
    }
    PtrAddr = N;
    PtrVal = M.getWordReg(N);
    PtrStep = coPointerStep(M, MI.getOperand(PtrOp + 1).getImm(), false);
  }

  for (uint64_t I = 0; I != Count && M.Stop == StopReason::Running; ++I) {
    // op1 and op2, in the manual's numbering.  Which of them is a memory word
    // and which a register is the shape's business.
    uint16_t Op1 = 0, Op2 = 0;
    // CoMOV is the one form the restriction does not apply to, which is what
    // the manual says and what makes it the instruction for moving a buffer
    // into the dual-port RAM in the first place.
    if (IdxOp != ~0u && F->Kind != K::MOV && !checkIdxPointer(M, IdxVal))
      return;
    switch (F->Shape) {
    case Sh::RP:
      Op1 = M.getWordReg(wordRegIndex(MRI, MI.getOperand(RegOp).getReg()));
      Op2 = M.read16(M.mapData(PtrVal));
      break;
    case Sh::XP:
      Op1 = M.read16(M.mapData(IdxVal));
      Op2 = M.read16(M.mapData(PtrVal));
      break;
    case Sh::P:
    case Sh::Q:
      Op1 = M.read16(M.mapData(PtrVal));
      break;
    case Sh::R:
      Op1 = M.getWordReg(wordRegIndex(MRI, MI.getOperand(RegOp).getReg()));
      break;
    case Sh::X:
      Op1 = M.read16(M.mapData(IdxVal));
      break;
    case Sh::SP:
      break;
    }

    switch (F->Kind) {
    case K::ADD:
    case K::SUB:
    case K::MIN:
    case K::MAX: {
      // "(op2)\\(op1)": op2 is the high word.
      int64_t T = int64_t(int32_t((uint32_t(Op2) << 16) | Op1));
      if (F->Flags & Dbl)
        T *= 2;
      if (F->Kind == K::ADD)
        M.setACC(M.ACC + T);
      else if (F->Kind == K::SUB)
        M.setACC((F->Flags & Rev) ? T - M.ACC : M.ACC - T);
      else if (F->Kind == K::MIN)
        M.setACC(std::min(M.ACC, T));
      else
        M.setACC(std::max(M.ACC, T));
      break;
    }
    case K::MAC: {
      int64_t P;
      switch (F->Sign) {
      case Sg::UU:
        P = int64_t(uint32_t(Op1) * uint32_t(Op2));
        break;
      case Sg::SU:
        P = int64_t(int32_t(int16_t(Op1)) * int32_t(uint16_t(Op2)));
        break;
      case Sg::US:
        P = int64_t(int32_t(uint16_t(Op1)) * int32_t(int16_t(Op2)));
        break;
      default:
        P = int64_t(int32_t(int16_t(Op1)) * int32_t(int16_t(Op2)));
        // Only the signed by signed forms are doubled by MP; the others say
        // "the result is never affected by the MP mode flag".
        if ((M.MCW >> 10) & 1)
          P <<= 1;
        break;
      }
      int64_t R = (F->Flags & Rev)    ? P - M.ACC
                  : (F->Flags & Neg) ? M.ACC - P
                                     : M.ACC + P;
      if (F->Flags & Rnd) {
        M.setACC(R + 0x8000);
        M.setACC(M.ACC & ~INT64_C(0xFFFF));
      } else {
        M.setACC(R);
      }
      // "M" moves the delay line: the word just read through IDXi is written
      // back one step behind where it came from, so that the next pass over
      // the same buffer sees it shifted.
      if (F->Flags & Move)
        M.write16(M.mapData(uint16_t(IdxVal - IdxStep)), Op1);
      break;
    }
    case K::MOV:
      M.write16(M.mapData(IdxVal), Op2);
      break;
    case K::NOP:
      break;
    case K::STORE: {
      StringRef Name = MRI.getName(MI.getOperand(CoRegOp).getReg());
      uint16_t V;
      if (Name.equals_insensitive("mal"))
        V = uint16_t(uint64_t(M.ACC) & 0xFFFF);
      else if (Name.equals_insensitive("mah"))
        V = uint16_t((uint64_t(M.ACC) >> 16) & 0xFFFF);
      else if (Name.equals_insensitive("mcw"))
        V = M.MCW;
      else {
        M.Stop = StopReason::Unsupported;
        M.StopDetail = "costore from a MAC register that is not modelled";
        return;
      }
      M.write16(M.mapData(PtrVal), V);
      break;
    }
    case K::SHL:
    case K::SHR:
    case K::ASHR: {
      // The count is the low five bits of the operand, and the accumulator is
      // forty bits wide.  MSW's carry is not modelled here, as it is not
      // anywhere else in this file.
      unsigned N = Op1 & 0x1F;
      uint64_t Bits = uint64_t(M.ACC) & 0xFFFFFFFFFFull;
      if (F->Kind == K::SHL)
        Bits <<= N;
      else if (F->Kind == K::SHR)
        Bits >>= N;
      else
        Bits = uint64_t(M.ACC >> std::min(N, 39u));
      M.setACC(int64_t(Bits));
      if (F->Flags & Rnd) {
        M.setACC(M.ACC + 0x8000);
        M.setACC(M.ACC & ~INT64_C(0xFFFF));
      }
      break;
    }
    }

    IdxVal = uint16_t(IdxVal + IdxStep);
    PtrVal = uint16_t(PtrVal + PtrStep);
  }

  if (IdxOp != ~0u)
    M.write16(IdxAddr, IdxVal);
  if (PtrOp != ~0u)
    M.setWordReg(PtrAddr, PtrVal);
}

void executeOne(Machine &M, const MCInst &MI, Op O, uint32_t PC) {
  Decoder &D = decoder();
  const MCRegisterInfo &MRI = *D.MRI;
  Executor E(M);

  // A non-zero count means this instruction is one an ATOMIC or an EXTend is
  // covering, and there are two kinds it must not be.
  if (M.ExtendCount > 0 && breaksExtendSequence(O)) {
    M.Stop = StopReason::BadSequence;
    M.StopDetail =
        (Twine("an ATOMIC or EXTend sequence reaches the instruction at ") +
         addrStr(PC) + ", which changes the flow or extends again")
            .str();
    return;
  }

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
      return M.gprAddress(B);
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
  // -- the MAC unit ------------------------------------------------------
  //
  // Only what a multiply-accumulate needs.  The semantics are the C166S V2
  // User's Manual's, from the detailed description of each instruction:
  //
  //   CoLOAD Rwn, Rwm   ACC <- sign extended (Rwm || Rwn), Rwn the low word
  //   CoMUL  Rwn, Rwm   ACC <- the signed 32 bit product, sign extended
  //   CoMAC  Rwn, Rwm   ACC <- ACC + that product
  //   CoMAC- Rwn, Rwm   ACC <- ACC - that product
  //   CoMACu Rwn, Rwm   the same two with an unsigned product, zero extended
  //   CoSTORE Rwn, creg the named MAC register into Rwn
  //
  // A form written ", rnd" does the same and then adds 00 0000 8000H and
  // clears MAL, which rounds the accumulator to its high word.  CoMUL is the
  // one the compiler selects; the rounding accumulates are still refused
  // rather than guessed at, which is what the default below does with
  // anything not listed.
  //
  // Each of the arithmetic ones one-bit left shifts the product first when
  // MCW.MP is set.  MCW resets to zero, so that does not happen unless a
  // program asks for it, and nothing here asks.
  case Op::CoLOAD_rr: {
    int64_t V = int64_t(int32_t((uint32_t(W(1)) << 16) | W(0)));
    M.setACC(V);
    break;
  }
  case Op::CoMAX_rr:
  case Op::CoMIN_rr: {
    // The operand is the two registers concatenated and sign extended into 40
    // bits, exactly as CoLOAD builds one, and the comparison is signed.  The
    // manual says the MS bit of MCW does not affect either of these, so
    // saturation cannot change the answer and nothing here consults it.
    int64_t V = int64_t(int32_t((uint32_t(W(1)) << 16) | W(0)));
    bool Take = O == Op::CoMAX_rr ? V > M.ACC : V < M.ACC;
    if (Take)
      M.setACC(V);
    break;
  }
  case Op::CoMUL_rr:
  case Op::CoMULu_rr:
  case Op::CoMUL_rr_rnd:
  case Op::CoMULu_rr_rnd:
  case Op::CoMAC_rr:
  case Op::CoMACu_rr:
  case Op::CoMACN_rr:
  case Op::CoMACuN_rr: {
    // The unsigned forms multiply the words as they stand and the signed ones
    // sign extend them first, which is the whole of the difference; the
    // product is 32 bits either way and the accumulator is 40.
    bool Unsigned = O == Op::CoMACu_rr || O == Op::CoMACuN_rr ||
                    O == Op::CoMULu_rr || O == Op::CoMULu_rr_rnd;
    int64_t P = Unsigned
                    ? int64_t(uint32_t(W(0)) * uint32_t(W(1)))
                    : int64_t(int32_t(int16_t(W(0))) * int32_t(int16_t(W(1))));
    if ((M.MCW >> 10) & 1)
      P <<= 1;
    bool Negate = O == Op::CoMACN_rr || O == Op::CoMACuN_rr;
    bool Round = O == Op::CoMUL_rr_rnd || O == Op::CoMULu_rr_rnd;
    bool Replace = O == Op::CoMUL_rr || O == Op::CoMULu_rr || Round;
    int64_t R = Replace ? P : (Negate ? M.ACC - P : M.ACC + P);
    if (Round) {
      // Add half of the high word's least significant bit and drop what is
      // below it, which is what leaves MAH holding the rounded answer.
      M.setACC(R + 0x8000);
      M.setACC(M.ACC & ~0xFFFFll);
      break;
    }
    M.setACC(R);
    break;
  }
  case Op::CoSTORE_sr: {
    // The second operand names a MAC register; MAL and MAH are the two words
    // of the accumulator.
    StringRef Name = decoder().MRI->getName(Reg(1));
    uint16_t V;
    if (Name.equals_insensitive("mal"))
      V = uint16_t(uint64_t(M.ACC) & 0xFFFF);
    else if (Name.equals_insensitive("mah"))
      V = uint16_t((uint64_t(M.ACC) >> 16) & 0xFFFF);
    else if (Name.equals_insensitive("mcw"))
      V = M.MCW;
    else {
      M.Stop = StopReason::Unsupported;
      M.StopDetail = "costore from a MAC register that is not modelled";
      return;
    }
    SetW(0, V);
    break;
  }
// The sixty multiply-accumulate forms, which differ only in the flags the
// table below gives them.  One case per line so that the set is readable
// as the list it is.
#define COREP_MAC_CASES \
  case Op::CoMACMN_xp: \
  case Op::CoMACMR_xp: \
  case Op::CoMACMR_xp_rnd: \
  case Op::CoMACMRsu_xp: \
  case Op::CoMACMRsu_xp_rnd: \
  case Op::CoMACMRu_xp: \
  case Op::CoMACMRu_xp_rnd: \
  case Op::CoMACMRus_xp: \
  case Op::CoMACMRus_xp_rnd: \
  case Op::CoMACM_xp: \
  case Op::CoMACM_xp_rnd: \
  case Op::CoMACMsuN_xp: \
  case Op::CoMACMsu_xp: \
  case Op::CoMACMsu_xp_rnd: \
  case Op::CoMACMuN_xp: \
  case Op::CoMACMu_xp: \
  case Op::CoMACMu_xp_rnd: \
  case Op::CoMACMusN_xp: \
  case Op::CoMACMus_xp: \
  case Op::CoMACMus_xp_rnd: \
  case Op::CoMACN_rp: \
  case Op::CoMACN_xp: \
  case Op::CoMACR_rp: \
  case Op::CoMACR_rp_rnd: \
  case Op::CoMACR_xp: \
  case Op::CoMACR_xp_rnd: \
  case Op::CoMACRsu_rp: \
  case Op::CoMACRsu_rp_rnd: \
  case Op::CoMACRsu_xp: \
  case Op::CoMACRsu_xp_rnd: \
  case Op::CoMACRu_rp: \
  case Op::CoMACRu_rp_rnd: \
  case Op::CoMACRu_xp: \
  case Op::CoMACRu_xp_rnd: \
  case Op::CoMACRus_rp: \
  case Op::CoMACRus_rp_rnd: \
  case Op::CoMACRus_xp: \
  case Op::CoMACRus_xp_rnd: \
  case Op::CoMAC_rp: \
  case Op::CoMAC_rp_rnd: \
  case Op::CoMAC_xp: \
  case Op::CoMAC_xp_rnd: \
  case Op::CoMACsuN_rp: \
  case Op::CoMACsuN_xp: \
  case Op::CoMACsu_rp: \
  case Op::CoMACsu_rp_rnd: \
  case Op::CoMACsu_xp: \
  case Op::CoMACsu_xp_rnd: \
  case Op::CoMACuN_rp: \
  case Op::CoMACuN_xp: \
  case Op::CoMACu_rp: \
  case Op::CoMACu_rp_rnd: \
  case Op::CoMACu_xp: \
  case Op::CoMACu_xp_rnd: \
  case Op::CoMACusN_rp: \
  case Op::CoMACusN_xp: \
  case Op::CoMACus_rp: \
  case Op::CoMACus_rp_rnd: \
  case Op::CoMACus_xp: \
  case Op::CoMACus_xp_rnd:

  // -- the coprocessor's repeatable forms ---------------------------------
  //
  // The 89 the ST10 Family Programming Manual's Rep column marks yes, which
  // are the ones written with a repeat prefix and so the ones a filter is
  // made of.  What they have in common is that at least one operand arrives
  // through a pointer that steps, which is what makes repeating them do
  // anything: the plain register forms above cannot, because a repetition
  // would read the same two registers again.
  //
  // Semantics are that manual's, from the Operation box of each instruction's
  // detailed description.  The pieces:
  //
  //   "(op2)\\(op1)"   op2 is the high word and op1 the low, making a 32 bit
  //                   value that is sign extended into the 40 bit ACC
  //   CoADD/CoSUB     ACC +/- that; "2" doubles it; "R" reverses the
  //                   subtraction to tmp - ACC
  //   CoMIN/CoMAX     ACC <- min/max(ACC, that)
  //   CoMAC           tmp <- op1 * op2, then ACC + tmp; "-" negates the
  //                   product and "R" negates the accumulator instead
  //   CoMACM          the same, and additionally writes the word read through
  //                   IDXi back one step behind it, which is what moves a
  //                   delay line along while the taps are being summed
  //   CoMOV           ((IDXi)) <- ((Rwm)), a memory to memory word move
  //   CoNOP           nothing but the pointer steps
  //   CoSTORE         ((Rwn)) <- the named MAC register
  //   CoSHL/SHR/ASHR  shift ACC by the low five bits of the operand
  //
  // Signedness follows the mnemonic: none is signed by signed, "u" unsigned
  // by unsigned with the product zero extended, and "su" and "us" are op1 and
  // op2 respectively - the manual says "the two signed and unsigned 16-bit
  // source operands op1 and op2, respectively" of CoMACsu and the mirror of
  // CoMACus.  MP doubling applies to the signed by signed forms alone; the
  // others say "the result is never affected by the MP mode flag".
  case Op::CoADD_rp:
  case Op::CoADD_xp:
  case Op::CoADD2_rp:
  case Op::CoADD2_xp:
  case Op::CoSUB_rp:
  case Op::CoSUB_xp:
  case Op::CoSUB2_rp:
  case Op::CoSUB2_xp:
  case Op::CoSUBR_rp:
  case Op::CoSUBR_xp:
  case Op::CoSUB2R_rp:
  case Op::CoSUB2R_xp:
  case Op::CoMIN_rp:
  case Op::CoMIN_xp:
  case Op::CoMAX_rp:
  case Op::CoMAX_xp:
  case Op::CoMOV_xp:
  case Op::CoNOP_q:
  case Op::CoNOP_x:
  case Op::CoNOP_xp:
  case Op::CoSTORE_sp:
  case Op::CoSHL_r:
  case Op::CoSHL_p:
  case Op::CoSHR_r:
  case Op::CoSHR_p:
  case Op::CoASHR_r:
  case Op::CoASHR_r_rnd:
  case Op::CoASHR_p:
  case Op::CoASHR_p_rnd:
  COREP_MAC_CASES
    executeCoRepeatable(M, MI, O);
    break;

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
    // A trap saves what accepting an interrupt saves and reaches the table the
    // same way, which is why the two share enterVector().  What it does not do
    // is raise the CPU priority: a software trap has no priority to raise it
    // to.
    enterVector(M, Imm(0));
    break;
  case Op::RETI:
    // The interrupted state is IP, then CSP while segmentation is on, then
    // PSW.  Segmentation is enabled out of reset, which is the only mode this
    // simulator runs in.
    M.IP = M.pop();
    M.CSP = M.pop();
    M.PSW = M.pop();
    break;
  // -- the rest of the instruction set -----------------------------------
  // Forms nothing here generates, so that what can be written by hand can
  // also be run.

  // The memory destination ALU forms: the address is first and the register
  // second, which is the operand order they are written in.
  case Op::ADD16regm:
    Store16At(Imm(0), E.doAdd(Load16At(Imm(0)), R8(1), false, false));
    break;
  case Op::ADDC16regm:
    Store16At(Imm(0), E.doAdd(Load16At(Imm(0)), R8(1), false, true));
    break;
  case Op::SUB16regm:
    Store16At(Imm(0), E.doSub(Load16At(Imm(0)), R8(1), false, false));
    break;
  case Op::SUBC16regm:
    Store16At(Imm(0), E.doSub(Load16At(Imm(0)), R8(1), false, true));
    break;
  case Op::AND16regm:
    Store16At(Imm(0), E.doLogic(Load16At(Imm(0)), R8(1), false, '&'));
    break;
  case Op::OR16regm:
    Store16At(Imm(0), E.doLogic(Load16At(Imm(0)), R8(1), false, '|'));
    break;
  case Op::XOR16regm:
    Store16At(Imm(0), E.doLogic(Load16At(Imm(0)), R8(1), false, '^'));
    break;
  case Op::CMP16regm:
    E.doSub(Load16At(Imm(0)), R8(1), false, false);
    break;
  case Op::ADDB8regm:
    Store8At(Imm(0), E.doAdd(Load8At(Imm(0)), RB8(1), true, false));
    break;
  case Op::ADDCB8regm:
    Store8At(Imm(0), E.doAdd(Load8At(Imm(0)), RB8(1), true, true));
    break;
  case Op::SUBB8regm:
    Store8At(Imm(0), E.doSub(Load8At(Imm(0)), RB8(1), true, false));
    break;
  case Op::SUBCB8regm:
    Store8At(Imm(0), E.doSub(Load8At(Imm(0)), RB8(1), true, true));
    break;
  case Op::ANDB8regm:
    Store8At(Imm(0), E.doLogic(Load8At(Imm(0)), RB8(1), true, '&'));
    break;
  case Op::ORB8regm:
    Store8At(Imm(0), E.doLogic(Load8At(Imm(0)), RB8(1), true, '|'));
    break;
  case Op::XORB8regm:
    Store8At(Imm(0), E.doLogic(Load8At(Imm(0)), RB8(1), true, '^'));
    break;
  case Op::CMPB8regm:
    E.doSub(Load8At(Imm(0)), RB8(1), true, false);
    break;

  // The byte add and subtract with carry.
  case Op::ADDCB8rr:
    SetB(0, E.doAdd(B(0), LastB(), true, true));
    break;
  case Op::ADDCB8ri3:
    SetB(0, E.doAdd(B(0), LastImm(), true, true));
    break;
  case Op::ADDCB8regi:
    WB8(0, E.doAdd(RB8(0), Imm(1), true, true));
    break;
  case Op::ADDCB8rega:
    WB8(0, E.doAdd(RB8(0), Load8At(Imm(1)), true, true));
    break;
  case Op::SUBCB8rr:
    SetB(0, E.doSub(B(0), LastB(), true, true));
    break;
  case Op::SUBCB8ri3:
    SetB(0, E.doSub(B(0), LastImm(), true, true));
    break;
  case Op::SUBCB8regi:
    WB8(0, E.doSub(RB8(0), Imm(1), true, true));
    break;
  case Op::SUBCB8rega:
    WB8(0, E.doSub(RB8(0), Load8At(Imm(1)), true, true));
    break;
  case Op::CMP16rega:
    E.doSub(R8(0), Load16At(Imm(1)), false, false);
    break;
  case Op::CMPB8rega:
    E.doSub(RB8(0), Load8At(Imm(1)), true, false);
    break;

  // The indirect source forms.  The pointer is the last operand, and the "pi"
  // ones step it past the word they read.
  case Op::ADD16rp:
  case Op::ADD16rppi:
    SetW(0, E.doAdd(W(0), Load16At(W(2)), false, false));
    if (O == Op::ADD16rppi)
      SetW(2, uint16_t(W(2) + 2));
    break;
  case Op::ADDC16rp:
  case Op::ADDC16rppi:
    SetW(0, E.doAdd(W(0), Load16At(W(2)), false, true));
    if (O == Op::ADDC16rppi)
      SetW(2, uint16_t(W(2) + 2));
    break;
  case Op::SUB16rp:
  case Op::SUB16rppi:
    SetW(0, E.doSub(W(0), Load16At(W(2)), false, false));
    if (O == Op::SUB16rppi)
      SetW(2, uint16_t(W(2) + 2));
    break;
  case Op::SUBC16rp:
  case Op::SUBC16rppi:
    SetW(0, E.doSub(W(0), Load16At(W(2)), false, true));
    if (O == Op::SUBC16rppi)
      SetW(2, uint16_t(W(2) + 2));
    break;
  case Op::AND16rp:
  case Op::AND16rppi:
    SetW(0, E.doLogic(W(0), Load16At(W(2)), false, '&'));
    if (O == Op::AND16rppi)
      SetW(2, uint16_t(W(2) + 2));
    break;
  case Op::OR16rp:
  case Op::OR16rppi:
    SetW(0, E.doLogic(W(0), Load16At(W(2)), false, '|'));
    if (O == Op::OR16rppi)
      SetW(2, uint16_t(W(2) + 2));
    break;
  case Op::XOR16rp:
  case Op::XOR16rppi:
    SetW(0, E.doLogic(W(0), Load16At(W(2)), false, '^'));
    if (O == Op::XOR16rppi)
      SetW(2, uint16_t(W(2) + 2));
    break;
  case Op::CMP16rp:
  case Op::CMP16rppi:
    E.doSub(W(0), Load16At(W(1)), false, false);
    if (O == Op::CMP16rppi)
      SetW(1, uint16_t(W(1) + 2));
    break;
  case Op::CMPB8rp:
  case Op::CMPB8rppi:
    E.doSub(B(0), Load8At(W(1)), true, false);
    if (O == Op::CMPB8rppi)
      SetW(1, uint16_t(W(1) + 1));
    break;

  // The pre-decrementing store steps the pointer back before writing, which
  // is the other way round from the post-incrementing load.
  case Op::MOV16prd: {
    uint16_t A = uint16_t(W(1) - 2);
    Store16At(A, W(2));
    SetW(0, A);
    break;
  }
  case Op::MOVB8prd: {
    uint16_t A = uint16_t(W(1) - 1);
    Store8At(A, B(2));
    SetW(0, A);
    break;
  }

  // The 32 by 16 divide, which takes MDH:MDL rather than MDL alone.  A
  // quotient that does not fit in sixteen bits overflows, which the hardware
  // reports in V rather than trapping.
  case Op::DIVLr:
  case Op::DIVLUr: {
    uint16_t D = W(0);
    if (D == 0) {
      M.setFlag(PSW_V, true);
      break;
    }
    uint32_t N = (uint32_t(M.MDH) << 16) | M.MDL;
    uint32_t Q, R;
    if (O == Op::DIVLUr) {
      Q = N / D;
      R = N % D;
      M.setFlag(PSW_V, Q > 0xFFFF);
    } else {
      int32_t SN = int32_t(N);
      int16_t SD = int16_t(D);
      int32_t SQ = SN / SD;
      Q = uint32_t(SQ);
      R = uint32_t(SN % SD);
      M.setFlag(PSW_V, SQ > 32767 || SQ < -32768);
    }
    M.MDL = uint16_t(Q);
    M.MDH = uint16_t(R);
    break;
  }

  // PRIOR counts how far the leftmost set bit is from the top, which is the
  // count of leading zeroes.  A source of zero leaves zero behind.
  case Op::PRIORrr: {
    uint16_t V = W(1);
    unsigned N = 0;
    if (V != 0)
      while (((V << N) & 0x8000) == 0)
        ++N;
    SetW(0, uint16_t(N));
    E.doLogic(V, 0, false, '|');
    break;
  }

  // Compare and step, which is a loop's test and its increment in one.  The
  // comparison sees the value as it was.
  case Op::CMPI116ri4:
  case Op::CMPI216ri4:
  case Op::CMPD116ri4:
  case Op::CMPD216ri4: {
    uint16_t V = W(0);
    E.doSub(V, Imm(1), false, false);
    int Step = (O == Op::CMPI116ri4)   ? 1
               : (O == Op::CMPI216ri4) ? 2
               : (O == Op::CMPD116ri4) ? -1
                                       : -2;
    SetW(0, uint16_t(V + Step));
    break;
  }
  case Op::CMPI116regi:
  case Op::CMPI216regi:
  case Op::CMPD116regi:
  case Op::CMPD216regi:
  case Op::CMPI116rega:
  case Op::CMPI216rega:
  case Op::CMPD116rega:
  case Op::CMPD216rega: {
    bool FromMem = O == Op::CMPI116rega || O == Op::CMPI216rega ||
                   O == Op::CMPD116rega || O == Op::CMPD216rega;
    uint16_t V = R8(0);
    E.doSub(V, FromMem ? Load16At(Imm(1)) : Imm(1), false, false);
    int Step = (O == Op::CMPI116regi || O == Op::CMPI116rega)   ? 1
               : (O == Op::CMPI216regi || O == Op::CMPI216rega) ? 2
               : (O == Op::CMPD116regi || O == Op::CMPD116rega) ? -1
                                                                : -2;
    W8(0, uint16_t(V + Step));
    break;
  }

  // SCXT saves what a register holds and puts something else there, which is
  // how a register bank or a data page pointer is switched and restored.
  case Op::SCXTregi:
    M.push(R8(0));
    W8(0, Imm(1));
    break;
  case Op::SCXTrega:
    M.push(R8(0));
    W8(0, Load16At(Imm(1)));
    break;

  // The remaining call and return forms.  PCALL pushes a register before the
  // return address, so RETP takes them back in the other order.
  case Op::CALLR: {
    int8_t Rel = int8_t(Imm(0));
    M.push(M.IP);
    M.IP = uint16_t(M.IP + 2 * Rel);
    break;
  }
  case Op::PCALL:
    M.push(R8(0));
    M.push(M.IP);
    M.IP = uint16_t(Imm(1));
    break;
  case Op::RETP: {
    uint16_t Ret = M.pop();
    W8(0, M.pop());
    M.IP = Ret;
    break;
  }

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
    // Locking interrupts out is the whole of what this does; the count is what
    // serviceInterrupts() looks at, so setting it is setting the lock.
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
