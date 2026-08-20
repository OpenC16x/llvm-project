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

  /// Common helper: a field that is either a constant or a relocation against
  /// the second word of the instruction.
  unsigned encodeRelocatable(const MCOperand &MO, MCFixupKind Kind,
                             SmallVectorImpl<MCFixup> &Fixups) const;

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

unsigned C166MCCodeEmitter::encodeRelocatable(
    const MCOperand &MO, MCFixupKind Kind,
    SmallVectorImpl<MCFixup> &Fixups) const {
  if (MO.isImm())
    return static_cast<unsigned>(MO.getImm());

  assert(MO.isExpr() && "Unexpected operand kind in a relocatable field");
  Fixups.push_back(MCFixup::create(SecondWordOffset, MO.getExpr(), Kind,
                                   /*PCRel=*/false));
  return 0;
}

unsigned C166MCCodeEmitter::getMachineOpValue(const MCInst &MI,
                                              const MCOperand &MO,
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

#include "C166GenMCCodeEmitter.inc"

} // end anonymous namespace

MCCodeEmitter *llvm::createC166MCCodeEmitter(const MCInstrInfo &MCII,
                                             MCContext &Ctx) {
  return new C166MCCodeEmitter(Ctx, MCII);
}
