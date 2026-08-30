//===-- C166Disassembler.cpp - Disassembler for C166 ----------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the C166Disassembler class.
//
// The opcode byte alone decides whether an instruction is one or two words, so
// decoding just means trying the single word table first and the double word
// table second: no opcode appears in both.
//
//===----------------------------------------------------------------------===//

#include "MCTargetDesc/C166MCTargetDesc.h"
#include "TargetInfo/C166TargetInfo.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCDecoder.h"
#include "llvm/MC/MCDecoderOps.h"
#include "llvm/MC/MCDisassembler/MCDisassembler.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCRegisterInfo.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Endian.h"

using namespace llvm;
using namespace llvm::MCD;

#define DEBUG_TYPE "c166-disassembler"

using DecodeStatus = MCDisassembler::DecodeStatus;

namespace {

class C166Disassembler : public MCDisassembler {
public:
  C166Disassembler(const MCSubtargetInfo &STI, MCContext &Ctx)
      : MCDisassembler(STI, Ctx) {}

  DecodeStatus getInstruction(MCInst &MI, uint64_t &Size,
                              ArrayRef<uint8_t> Bytes, uint64_t Address,
                              raw_ostream &CStream) const override;
};

} // end anonymous namespace

static const MCPhysReg GR16DecoderTable[] = {
    C166::R0,  C166::R1,  C166::R2,  C166::R3, C166::R4,  C166::R5,
    C166::R6,  C166::R7,  C166::R8,  C166::R9, C166::R10, C166::R11,
    C166::R12, C166::R13, C166::R14, C166::R15};

static const MCPhysReg GR8DecoderTable[] = {
    C166::RL0, C166::RH0, C166::RL1, C166::RH1, C166::RL2, C166::RH2,
    C166::RL3, C166::RH3, C166::RL4, C166::RH4, C166::RL5, C166::RH5,
    C166::RL6, C166::RH6, C166::RL7, C166::RH7};

static DecodeStatus DecodeGR16RegisterClass(MCInst &MI, uint64_t RegNo,
                                            uint64_t Address,
                                            const MCDisassembler *Decoder) {
  if (RegNo >= std::size(GR16DecoderTable))
    return MCDisassembler::Fail;

  MI.addOperand(MCOperand::createReg(GR16DecoderTable[RegNo]));
  return MCDisassembler::Success;
}

static DecodeStatus DecodeGR8RegisterClass(MCInst &MI, uint64_t RegNo,
                                           uint64_t Address,
                                           const MCDisassembler *Decoder) {
  if (RegNo >= std::size(GR8DecoderTable))
    return MCDisassembler::Fail;

  MI.addOperand(MCOperand::createReg(GR8DecoderTable[RegNo]));
  return MCDisassembler::Success;
}

/// All sixteen condition code encodings are defined by the architecture.
static DecodeStatus decodeCondCode(MCInst &MI, uint64_t Imm, uint64_t Address,
                                   const MCDisassembler *Decoder) {
  MI.addOperand(MCOperand::createImm(Imm & 0xf));
  return MCDisassembler::Success;
}

/// [Rw + #data16], packed by the encoder as (disp << 4) | base.
static DecodeStatus decodeMemRIOperand(MCInst &MI, uint64_t Imm,
                                       uint64_t Address,
                                       const MCDisassembler *Decoder) {
  if (DecodeGR16RegisterClass(MI, Imm & 0xf, Address, Decoder) ==
      MCDisassembler::Fail)
    return MCDisassembler::Fail;

  MI.addOperand(
      MCOperand::createImm(static_cast<int16_t>((Imm >> 4) & 0xffff)));
  return MCDisassembler::Success;
}

/// An instruction range is encoded as 0 to 3 and written as 1 to 4.
static DecodeStatus decodeMemROperand(MCInst &MI, uint64_t Imm,
                                      uint64_t Address,
                                      const MCDisassembler *Decoder) {
  return DecodeGR16RegisterClass(MI, Imm & 0xf, Address, Decoder);
}

static DecodeStatus decodeBitOffOperand(MCInst &MI, uint64_t Imm,
                                        uint64_t Address,
                                        const MCDisassembler *Decoder) {
  MI.addOperand(MCOperand::createImm(Imm & 0xff));
  return MCDisassembler::Success;
}

/// A bit address arrives packed as (bitpos << 8) | bitoff; see
/// C166MCCodeEmitter::getBitAddrOpValue().
static DecodeStatus decodeBitAddrOperand(MCInst &MI, uint64_t Imm,
                                         uint64_t Address,
                                         const MCDisassembler *Decoder) {
  MI.addOperand(MCOperand::createImm(Imm & 0xff));
  MI.addOperand(MCOperand::createImm((Imm >> 8) & 0xf));
  return MCDisassembler::Success;
}

/// A short address, as the register the part being disassembled for has there.
///
/// The register file holds every derivative's map at once and the same short
/// address names different registers on different parts, so the subtarget is
/// what picks; without it a listing for one part would be read as another.
/// Only the registers this backend models can be named, so the rest of the
/// space does not decode.
static DecodeStatus decodeSFRShortAddress(MCInst &MI, uint64_t Imm,
                                          const MCDisassembler *Decoder) {
  const MCRegisterClass &SFRs = getC166MCRegisterClass(C166::SFRRegClassID);
  const MCRegisterInfo *MRI = Decoder->getContext().getRegisterInfo();
  for (MCPhysReg Reg : SFRs) {
    if (MRI->getEncodingValue(Reg) != Imm)
      continue;
    if (!C166::isSFRInSelectedMap(Reg, Decoder->getSubtargetInfo()))
      continue;
    MI.addOperand(MCOperand::createReg(Reg));
    return MCDisassembler::Success;
  }
  return MCDisassembler::Fail;
}

/// The 8 bit "reg" field of PUSH and POP: F0H + n is a general purpose
/// register, anything else is the short address of a special function
/// register.  Only the special function registers the backend models can be
/// named, so the rest of the SFR space does not decode.
/// The ALU's indirect forms carry their pointer in two bits, so it names one
/// of R0 to R3 rather than any word register.
static DecodeStatus decodeMemRPOperand(MCInst &MI, uint64_t Imm,
                                       uint64_t Address,
                                       const MCDisassembler *Decoder) {
  return DecodeGR16RegisterClass(MI, Imm & 0x3, Address, Decoder);
}

static DecodeStatus decodeReg8Operand(MCInst &MI, uint64_t Imm,
                                      uint64_t Address,
                                      const MCDisassembler *Decoder) {
  if (Imm >= 0xF0)
    return DecodeGR16RegisterClass(MI, Imm - 0xF0, Address, Decoder);

  return decodeSFRShortAddress(MI, Imm, Decoder);
}

/// The same field in a byte instruction, where F0H + n is a byte register.
static DecodeStatus decodeReg8bOperand(MCInst &MI, uint64_t Imm,
                                       uint64_t Address,
                                       const MCDisassembler *Decoder) {
  if (Imm >= 0xF0)
    return DecodeGR8RegisterClass(MI, Imm - 0xF0, Address, Decoder);

  return decodeSFRShortAddress(MI, Imm, Decoder);
}

/// A MAC unit pointer comes back as the register and its step, which is how
/// the printer and the parser both hold it.
static DecodeStatus decodeCoPtrOperand(MCInst &MI, uint64_t Imm,
                                       uint64_t Address,
                                       const MCDisassembler *Decoder) {
  unsigned Update = (Imm >> 4) & 0x7;
  // 000 is reserved, so an instruction carrying it is not one of these.
  if (Update == 0)
    return MCDisassembler::Fail;
  if (DecodeGR16RegisterClass(MI, Imm & 0xF, Address, Decoder) ==
      MCDisassembler::Fail)
    return MCDisassembler::Fail;
  MI.addOperand(MCOperand::createImm(Update));
  return MCDisassembler::Success;
}

static DecodeStatus decodeCoIdxOperand(MCInst &MI, uint64_t Imm,
                                       uint64_t Address,
                                       const MCDisassembler *Decoder) {
  unsigned Update = Imm & 0x7;
  if (Update == 0)
    return MCDisassembler::Fail;
  MI.addOperand(MCOperand::createReg((Imm & 0x8) ? C166::IDX1 : C166::IDX0));
  MI.addOperand(MCOperand::createImm(Update));
  return MCDisassembler::Success;
}

static DecodeStatus decodeCoRegOperand(MCInst &MI, uint64_t Imm,
                                       uint64_t Address,
                                       const MCDisassembler *Decoder) {
  MCRegister Reg;
  switch (Imm) {
  case 0x00: Reg = C166::MSW; break;
  case 0x01: Reg = C166::MAH; break;
  case 0x02: Reg = C166::MAS; break;
  case 0x04: Reg = C166::MAL; break;
  case 0x05: Reg = C166::MCW; break;
  case 0x06: Reg = C166::MRW; break;
  default:
    return MCDisassembler::Fail;
  }
  MI.addOperand(MCOperand::createReg(Reg));
  return MCDisassembler::Success;
}

static DecodeStatus decodeIrang2Operand(MCInst &MI, uint64_t Imm,
                                        uint64_t Address,
                                        const MCDisassembler *Decoder) {
  MI.addOperand(MCOperand::createImm((Imm & 0x3) + 1));
  return MCDisassembler::Success;
}

#include "C166GenDisassemblerTables.inc"

DecodeStatus C166Disassembler::getInstruction(MCInst &MI, uint64_t &Size,
                                              ArrayRef<uint8_t> Bytes,
                                              uint64_t Address,
                                              raw_ostream &CStream) const {
  if (Bytes.size() >= 2) {
    uint32_t Insn = support::endian::read16le(Bytes.data());
    DecodeStatus Result =
        decodeInstruction(DecoderTable16, MI, Insn, Address, this, STI);
    if (Result != MCDisassembler::Fail) {
      Size = 2;
      return Result;
    }
    MI.clear();
  }

  if (Bytes.size() >= 4) {
    uint32_t Insn = support::endian::read32le(Bytes.data());
    DecodeStatus Result =
        decodeInstruction(DecoderTable32, MI, Insn, Address, this, STI);
    if (Result != MCDisassembler::Fail) {
      Size = 4;
      return Result;
    }
    MI.clear();
  }

  Size = 0;
  return MCDisassembler::Fail;
}

static MCDisassembler *createC166Disassembler(const Target &T,
                                              const MCSubtargetInfo &STI,
                                              MCContext &Ctx) {
  return new C166Disassembler(STI, Ctx);
}

extern "C" LLVM_ABI LLVM_EXTERNAL_VISIBILITY void
LLVMInitializeC166Disassembler() {
  TargetRegistry::RegisterMCDisassembler(getTheC166Target(),
                                         createC166Disassembler);
}
