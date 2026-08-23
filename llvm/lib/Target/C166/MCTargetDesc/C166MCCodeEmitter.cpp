//===-- C166MCCodeEmitter.cpp - Convert C166 code to machine code ---------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the C166MCCodeEmitter class.
//
// A C166 instruction is one or two 16 bit words, emitted least significant
// byte first, so the encoding computed by TableGen is simply written out as
// Size bytes.  Any 16 or 8 bit field that turns out to hold a symbol reference
// becomes a relocation against byte 2 of the instruction, which is where every
// such field lives.
//
//===----------------------------------------------------------------------===//

#include "MCTargetDesc/C166FixupKinds.h"
#include "MCTargetDesc/C166MCTargetDesc.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/MC/MCCodeEmitter.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/MC/MCFixup.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCInstrInfo.h"
#include "llvm/MC/MCRegisterInfo.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/Support/EndianStream.h"
#include "llvm/Support/ErrorHandling.h"

using namespace llvm;

#define DEBUG_TYPE "mccodeemitter"

namespace {

/// Byte offset of the second word of an instruction, which is where every
/// relocatable field of a C166 instruction sits.
constexpr unsigned SecondWordOffset = 2;

class C166MCCodeEmitter : public MCCodeEmitter {
  MCContext &Ctx;
  const MCInstrInfo &MCII;

  /// TableGen'erated function for getting the binary encoding for an
  /// instruction.
  uint64_t getBinaryCodeForInstr(const MCInst &MI,
                                 SmallVectorImpl<MCFixup> &Fixups,
                                 const MCSubtargetInfo &STI) const;

  /// Returns the binary encoding of a plain register or immediate operand.
  unsigned getMachineOpValue(const MCInst &MI, const MCOperand &MO,
                             SmallVectorImpl<MCFixup> &Fixups,
                             const MCSubtargetInfo &STI) const;

  /// [Rw + #data16] addressing, packed as (disp << 4) | base.
  unsigned getMemRIOpValue(const MCInst &MI, unsigned OpNo,
                           SmallVectorImpl<MCFixup> &Fixups,
                           const MCSubtargetInfo &STI) const;

  /// A 16 bit address ("mem" or "caddr").
  unsigned getCAddrOpValue(const MCInst &MI, unsigned OpNo,
                           SmallVectorImpl<MCFixup> &Fixups,
                           const MCSubtargetInfo &STI) const;

  unsigned getAddr16OpValue(const MCInst &MI, unsigned OpNo,
                            SmallVectorImpl<MCFixup> &Fixups,
                            const MCSubtargetInfo &STI) const;

  /// A 16 bit immediate (#data16).
  unsigned getData16OpValue(const MCInst &MI, unsigned OpNo,
                            SmallVectorImpl<MCFixup> &Fixups,
                            const MCSubtargetInfo &STI) const;

  /// An 8 bit immediate (#data8).
  unsigned getData8OpValue(const MCInst &MI, unsigned OpNo,
                           SmallVectorImpl<MCFixup> &Fixups,
                           const MCSubtargetInfo &STI) const;

  /// The 8 bit segment of an EXTS/EXTSR, in the second word.
  unsigned getSeg8OpValue(const MCInst &MI, unsigned OpNo,
                          SmallVectorImpl<MCFixup> &Fixups,
                          const MCSubtargetInfo &STI) const;

  /// The 8 bit segment of a JMPS/CALLS, which is the second byte of the
  /// first word rather than part of the second.
  unsigned getJumpSeg8OpValue(const MCInst &MI, unsigned OpNo,
                              SmallVectorImpl<MCFixup> &Fixups,
                              const MCSubtargetInfo &STI) const;

  /// The 10 bit page of an EXTP/EXTPR.  It lives in the low ten bits of the
  /// second word, so a word sized fixup covers it.
  unsigned getPag10OpValue(const MCInst &MI, unsigned OpNo,
                           SmallVectorImpl<MCFixup> &Fixups,
                           const MCSubtargetInfo &STI) const;

  /// The signed 8 bit word displacement of a relative branch.
  unsigned getRel8OpValue(const MCInst &MI, unsigned OpNo,
                          SmallVectorImpl<MCFixup> &Fixups,
                          const MCSubtargetInfo &STI) const;

  /// The same in a two byte instruction, where it is the second byte.
  unsigned getShortRel8OpValue(const MCInst &MI, unsigned OpNo,
                               SmallVectorImpl<MCFixup> &Fixups,
                               const MCSubtargetInfo &STI) const;

  /// A bit address, packed as (bitpos << 8) | bitoff so that one value can
  /// feed both fields of the instruction.
  unsigned getBitAddrOpValue(const MCInst &MI, unsigned OpNo,
                             SmallVectorImpl<MCFixup> &Fixups,
                             const MCSubtargetInfo &STI) const;

  /// The 8 bit "reg" field of a word instruction.
  unsigned getReg8OpValue(const MCInst &MI, unsigned OpNo,
                          SmallVectorImpl<MCFixup> &Fixups,
                          const MCSubtargetInfo &STI) const;

  /// The same field in a byte instruction, where F0H + n is a byte register.
  unsigned getReg8bOpValue(const MCInst &MI, unsigned OpNo,
                           SmallVectorImpl<MCFixup> &Fixups,
                           const MCSubtargetInfo &STI) const;

  /// A MAC unit pointer: the register number in the low four bits and what
  /// happens to the pointer afterwards in the three above, which the
  /// instruction then splits between two of its bytes.
  unsigned getCoPtrOpValue(const MCInst &MI, unsigned OpNo,
                           SmallVectorImpl<MCFixup> &Fixups,
                           const MCSubtargetInfo &STI) const;

  /// The same for IDX0 and IDX1, as one nibble: which of the two, then what
  /// happens to it.
  unsigned getCoIdxOpValue(const MCInst &MI, unsigned OpNo,
                           SmallVectorImpl<MCFixup> &Fixups,
                           const MCSubtargetInfo &STI) const;

  /// The five bit code CoSTORE names a MAC register by.
  unsigned getCoRegOpValue(const MCInst &MI, unsigned OpNo,
                           SmallVectorImpl<MCFixup> &Fixups,
                           const MCSubtargetInfo &STI) const;

  /// An ATOMIC/EXTend instruction range, written as 1 to 4 and encoded as
  /// 0 to 3.
  unsigned getIrang2OpValue(const MCInst &MI, unsigned OpNo,
                            SmallVectorImpl<MCFixup> &Fixups,
                            const MCSubtargetInfo &STI) const;

  /// Common helper: a field that is either a constant or a relocation at
  /// byte Offset of the instruction.
  unsigned encodeRelocatable(const MCOperand &MO, MCFixupKind Kind,
                             SmallVectorImpl<MCFixup> &Fixups,
                             uint32_t Offset = SecondWordOffset) const;

public:
  C166MCCodeEmitter(MCContext &Ctx, const MCInstrInfo &MCII)
      : Ctx(Ctx), MCII(MCII) {}

  void encodeInstruction(const MCInst &MI, SmallVectorImpl<char> &CB,
                         SmallVectorImpl<MCFixup> &Fixups,
                         const MCSubtargetInfo &STI) const override;
};

void C166MCCodeEmitter::encodeInstruction(const MCInst &MI,
                                          SmallVectorImpl<char> &CB,
                                          SmallVectorImpl<MCFixup> &Fixups,
                                          const MCSubtargetInfo &STI) const {
  const MCInstrDesc &Desc = MCII.get(MI.getOpcode());
  unsigned Size = Desc.getSize();
  assert((Size == 2 || Size == 4) && "C166 instructions are 2 or 4 bytes");

  uint64_t Bits = getBinaryCodeForInstr(MI, Fixups, STI);

  for (unsigned I = 0; I != Size; ++I)
    support::endian::write<uint8_t>(CB, (Bits >> (I * 8)) & 0xff,
                                    llvm::endianness::little);
}

unsigned C166MCCodeEmitter::encodeRelocatable(const MCOperand &MO,
                                              MCFixupKind Kind,
                                              SmallVectorImpl<MCFixup> &Fixups,
                                              uint32_t Offset) const {
  if (MO.isImm())
    return static_cast<unsigned>(MO.getImm());

  assert(MO.isExpr() && "Unexpected operand kind in a relocatable field");
  Fixups.push_back(
      MCFixup::create(Offset, MO.getExpr(), Kind, /*PCRel=*/false));
  return 0;
}

unsigned
C166MCCodeEmitter::getMachineOpValue(const MCInst &MI, const MCOperand &MO,
                                     SmallVectorImpl<MCFixup> &Fixups,
                                     const MCSubtargetInfo &STI) const {
  if (MO.isReg())
    return Ctx.getRegisterInfo()->getEncodingValue(MO.getReg());

  if (MO.isImm())
    return static_cast<unsigned>(MO.getImm());

  // A bare expression in an otherwise plain field can still appear through
  // inline assembly; relocate the second word.
  return encodeRelocatable(MO, FK_Data_2, Fixups);
}

unsigned C166MCCodeEmitter::getMemRIOpValue(const MCInst &MI, unsigned OpNo,
                                            SmallVectorImpl<MCFixup> &Fixups,
                                            const MCSubtargetInfo &STI) const {
  const MCOperand &Base = MI.getOperand(OpNo);
  const MCOperand &Disp = MI.getOperand(OpNo + 1);

  assert(Base.isReg() && "Expected a base register");
  unsigned Value = Ctx.getRegisterInfo()->getEncodingValue(Base.getReg()) & 0xf;

  if (Disp.isImm())
    return Value | ((Disp.getImm() & 0xffff) << 4);

  assert(Disp.isExpr() && "Unexpected displacement kind");
  Fixups.push_back(MCFixup::create(SecondWordOffset, Disp.getExpr(), FK_Data_2,
                                   /*PCRel=*/false));
  return Value;
}

unsigned C166MCCodeEmitter::getAddr16OpValue(const MCInst &MI, unsigned OpNo,
                                             SmallVectorImpl<MCFixup> &Fixups,
                                             const MCSubtargetInfo &STI) const {
  return encodeRelocatable(MI.getOperand(OpNo), FK_Data_2, Fixups) & 0xffff;
}

unsigned C166MCCodeEmitter::getCAddrOpValue(const MCInst &MI, unsigned OpNo,
                                            SmallVectorImpl<MCFixup> &Fixups,
                                            const MCSubtargetInfo &STI) const {
  // The same field as any other 16 bit address, under a fixup kind of its own
  // so that the linker is told this one is a branch or call that stays in the
  // segment it is in; see C166FixupKinds.h.
  return encodeRelocatable(MI.getOperand(OpNo), C166::fixup_c166_caddr,
                           Fixups) &
         0xffff;
}

unsigned C166MCCodeEmitter::getData16OpValue(const MCInst &MI, unsigned OpNo,
                                             SmallVectorImpl<MCFixup> &Fixups,
                                             const MCSubtargetInfo &STI) const {
  return encodeRelocatable(MI.getOperand(OpNo), FK_Data_2, Fixups) & 0xffff;
}

unsigned C166MCCodeEmitter::getData8OpValue(const MCInst &MI, unsigned OpNo,
                                            SmallVectorImpl<MCFixup> &Fixups,
                                            const MCSubtargetInfo &STI) const {
  return encodeRelocatable(MI.getOperand(OpNo), FK_Data_1, Fixups) & 0xff;
}

unsigned C166MCCodeEmitter::getSeg8OpValue(const MCInst &MI, unsigned OpNo,
                                           SmallVectorImpl<MCFixup> &Fixups,
                                           const MCSubtargetInfo &STI) const {
  return encodeRelocatable(MI.getOperand(OpNo), FK_Data_1, Fixups) & 0xff;
}

unsigned
C166MCCodeEmitter::getJumpSeg8OpValue(const MCInst &MI, unsigned OpNo,
                                      SmallVectorImpl<MCFixup> &Fixups,
                                      const MCSubtargetInfo &STI) const {
  return encodeRelocatable(MI.getOperand(OpNo), FK_Data_1, Fixups,
                           /*Offset=*/1) &
         0xff;
}

unsigned C166MCCodeEmitter::getPag10OpValue(const MCInst &MI, unsigned OpNo,
                                            SmallVectorImpl<MCFixup> &Fixups,
                                            const MCSubtargetInfo &STI) const {
  return encodeRelocatable(MI.getOperand(OpNo), FK_Data_2, Fixups) & 0x3ff;
}

unsigned C166MCCodeEmitter::getIrang2OpValue(const MCInst &MI, unsigned OpNo,
                                             SmallVectorImpl<MCFixup> &Fixups,
                                             const MCSubtargetInfo &STI) const {
  const MCOperand &MO = MI.getOperand(OpNo);
  assert(MO.isImm() && MO.getImm() >= 1 && MO.getImm() <= 4 &&
         "Instruction range must be a constant in [1, 4]");
  return static_cast<unsigned>(MO.getImm()) - 1;
}

unsigned C166MCCodeEmitter::getReg8OpValue(const MCInst &MI, unsigned OpNo,
                                           SmallVectorImpl<MCFixup> &Fixups,
                                           const MCSubtargetInfo &STI) const {
  MCRegister Reg = MI.getOperand(OpNo).getReg();

  // A general purpose register is addressed as F0H + n in an 8 bit "reg"
  // field, while a special function register already carries its own short
  // address as its encoding.
  unsigned Encoding = Ctx.getRegisterInfo()->getEncodingValue(Reg);
  if (getC166MCRegisterClass(C166::GR16RegClassID).contains(Reg))
    return 0xF0 + Encoding;
  return Encoding;
}

unsigned C166MCCodeEmitter::getReg8bOpValue(const MCInst &MI, unsigned OpNo,
                                            SmallVectorImpl<MCFixup> &Fixups,
                                            const MCSubtargetInfo &STI) const {
  MCRegister Reg = MI.getOperand(OpNo).getReg();

  // In a byte instruction F0H + n names a byte register, so the encoding is
  // the byte register's own number; a special function register carries its
  // short address either way.
  unsigned Encoding = Ctx.getRegisterInfo()->getEncodingValue(Reg);
  if (getC166MCRegisterClass(C166::GR8RegClassID).contains(Reg))
    return 0xF0 + Encoding;
  return Encoding;
}

unsigned C166MCCodeEmitter::getCoPtrOpValue(const MCInst &MI, unsigned OpNo,
                                            SmallVectorImpl<MCFixup> &Fixups,
                                            const MCSubtargetInfo &STI) const {
  unsigned Reg = Ctx.getRegisterInfo()->getEncodingValue(
      MI.getOperand(OpNo).getReg());
  unsigned Update = MI.getOperand(OpNo + 1).getImm();
  return (Update << 4) | Reg;
}

unsigned C166MCCodeEmitter::getCoIdxOpValue(const MCInst &MI, unsigned OpNo,
                                            SmallVectorImpl<MCFixup> &Fixups,
                                            const MCSubtargetInfo &STI) const {
  unsigned Which = MI.getOperand(OpNo).getReg() == C166::IDX1 ? 1 : 0;
  unsigned Update = MI.getOperand(OpNo + 1).getImm();
  return (Which << 3) | Update;
}

unsigned C166MCCodeEmitter::getCoRegOpValue(const MCInst &MI, unsigned OpNo,
                                            SmallVectorImpl<MCFixup> &Fixups,
                                            const MCSubtargetInfo &STI) const {
  // From Table 2-9 of the C166S V2 manual.  The codes are not consecutive, so
  // this is a table rather than an index.
  switch (MI.getOperand(OpNo).getReg()) {
  case C166::MSW:
    return 0x00;
  case C166::MAH:
    return 0x01;
  case C166::MAS:
    return 0x02;
  case C166::MAL:
    return 0x04;
  case C166::MCW:
    return 0x05;
  case C166::MRW:
    return 0x06;
  default:
    llvm_unreachable("not a register CoSTORE can name");
  }
}

unsigned
C166MCCodeEmitter::getBitAddrOpValue(const MCInst &MI, unsigned OpNo,
                                     SmallVectorImpl<MCFixup> &Fixups,
                                     const MCSubtargetInfo &STI) const {
  const MCOperand &Off = MI.getOperand(OpNo);
  const MCOperand &Pos = MI.getOperand(OpNo + 1);
  assert(Off.isImm() && Pos.isImm() && "Bit address must be constant");
  return ((static_cast<unsigned>(Pos.getImm()) & 0xf) << 8) |
         (static_cast<unsigned>(Off.getImm()) & 0xff);
}

unsigned C166MCCodeEmitter::getRel8OpValue(const MCInst &MI, unsigned OpNo,
                                           SmallVectorImpl<MCFixup> &Fixups,
                                           const MCSubtargetInfo &STI) const {
  const MCOperand &MO = MI.getOperand(OpNo);
  if (MO.isImm())
    return static_cast<unsigned>(MO.getImm()) & 0xff;

  // The displacement byte is the third of the instruction; the backend turns
  // the distance to it into a word count from the instruction that follows.
  Fixups.push_back(MCFixup::create(SecondWordOffset, MO.getExpr(),
                                   C166::fixup_c166_rel8w, /*PCRel=*/true));
  return 0;
}

unsigned
C166MCCodeEmitter::getShortRel8OpValue(const MCInst &MI, unsigned OpNo,
                                       SmallVectorImpl<MCFixup> &Fixups,
                                       const MCSubtargetInfo &STI) const {
  const MCOperand &MO = MI.getOperand(OpNo);
  if (MO.isImm())
    return static_cast<unsigned>(MO.getImm()) & 0xff;

  // JMPR is two bytes, so its displacement byte is the second one.
  Fixups.push_back(MCFixup::create(1, MO.getExpr(),
                                   C166::fixup_c166_rel8w_short,
                                   /*PCRel=*/true));
  return 0;
}

#include "C166GenMCCodeEmitter.inc"

} // end anonymous namespace

MCCodeEmitter *llvm::createC166MCCodeEmitter(const MCInstrInfo &MCII,
                                             MCContext &Ctx) {
  return new C166MCCodeEmitter(Ctx, MCII);
}
