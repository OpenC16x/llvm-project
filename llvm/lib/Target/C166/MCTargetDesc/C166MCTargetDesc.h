//===-- C166MCTargetDesc.h - C166 Target Descriptions -----------*- C++ -*-===//
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

#ifndef LLVM_LIB_TARGET_C166_MCTARGETDESC_C166MCTARGETDESC_H
#define LLVM_LIB_TARGET_C166_MCTARGETDESC_C166MCTARGETDESC_H

#include "llvm/ADT/StringRef.h"
#include "llvm/Support/DataTypes.h"
#include <memory>

namespace llvm {
class MCAsmBackend;
class MCCodeEmitter;
class MCContext;
class MCInstrInfo;
class MCObjectTargetWriter;
class MCRegisterInfo;
class MCSubtargetInfo;
class MCRegister;
class MCTargetOptions;
class Target;

MCCodeEmitter *createC166MCCodeEmitter(const MCInstrInfo &MCII, MCContext &Ctx);

MCAsmBackend *createC166MCAsmBackend(const Target &T,
                                     const MCSubtargetInfo &STI,
                                     const MCRegisterInfo &MRI,
                                     const MCTargetOptions &Options);

std::unique_ptr<MCObjectTargetWriter> createC166ELFObjectWriter(uint8_t OSABI);

namespace C166 {

// The special function registers are memory mapped, and both the assembler and
// the disassembler are expected to know where.  The register file is where
// that is written down: each one carries its 8 bit short address as its
// hardware encoding, which is both what the "reg" field of PUSH and POP
// encodes and, through the mapping below, the address every other instruction
// reaches it by.  Deriving one from the other rather than keeping a second
// table is what stops the assembler and the disassembler drifting apart.

/// Whether \p Reg is a special function register the part being assembled for
/// actually has.
///
/// The short address a register carries is its encoding, and the same short
/// address names different registers on different derivatives, so the register
/// file holds every map at once and this is what picks between them.  Both
/// ends need the same answer: the assembler so that a name from another part's
/// map is refused rather than quietly encoded, and the disassembler so that a
/// short address comes back as the register this part has there.
///
/// Anything outside the two per-part groups is shared by every map here and is
/// always available.
bool isSFRInSelectedMap(MCRegister Reg, const MCSubtargetInfo &STI);

/// Where the special function register with short address \p Short is mapped.
/// The two halves of the space are laid out from different bases.
constexpr uint16_t getSFRAddressForShort(uint8_t Short) {
  return Short < 0x80 ? 0xFE00 + 2 * Short : 0xFF00 + 2 * (Short - 0x80);
}

/// Whether an SFR at short address \p Short is bit addressable, in which case
/// its bitoff is the short address again.  Only FF00H to FFDEH is.
constexpr bool isSFRBitAddressable(uint8_t Short) {
  return Short >= 0x80 && getSFRAddressForShort(Short) <= 0xFFDE;
}

/// The bitoff naming the word at absolute address \p Addr, or -1 if no bit
/// instruction can reach it.  Two windows are bit-addressable: internal RAM
/// from FD00H to FDFEH and the special function registers from FF00H to FFDEH.
/// Both are word addresses, so an odd one names no bit-addressable word at
/// all.  The extended registers are left out on purpose: reaching one needs an
/// EXTR prefix, which is not the same instruction any more.
constexpr int getBitOffForAddress(uint16_t Addr) {
  if (Addr & 1)
    return -1;
  if (Addr >= 0xFD00 && Addr <= 0xFDFE)
    return (Addr - 0xFD00) / 2;
  if (Addr >= 0xFF00 && Addr <= 0xFFDE)
    return 0x80 + (Addr - 0xFF00) / 2;
  return -1;
}

/// Where the extended special function register with short address \p Short is
/// mapped.  The extended space holds a second set of registers at the same
/// short addresses, from different bases.
constexpr uint16_t getESFRAddressForShort(uint8_t Short) {
  return Short < 0x80 ? 0xF000 + 2 * Short : 0xF100 + 2 * (Short - 0x80);
}

/// The 8 bit short address of the named register, or -1 if the name is not
/// one.  Extended registers are not named here: a "reg" field cannot tell one
/// from the register with the same short address in the ordinary space.
int64_t getSFRShortAddress(const MCRegisterInfo &MRI, StringRef Name);

/// The address the named register is mapped at, or -1 if the name is not one.
/// This does cover the extended registers, which are reachable by address.
int64_t getSFRAddress(const MCRegisterInfo &MRI, StringRef Name);

/// The address \p Reg is mapped at, or -1 if it is not a memory mapped
/// register.  This is the same question getSFRAddress answers, asked with the
/// register rather than its name, which is what the code generator has: it
/// reaches a special function register through an absolute address, and the
/// address has to come from the same place the assembler's comes from.
///
/// MAS is the one register in the coprocessor's set that answers -1.  It is
/// the saturated view of the accumulator's high word and has no address of its
/// own - CoSTORE names it by a code - so there is no move that can reach it.
int64_t getSFRAddressForReg(const MCRegisterInfo &MRI, MCRegister Reg);

/// The name of the register mapped at \p Addr, or empty if nothing is.
/// The name of the special function register mapped at \p Addr, or empty.
///
/// \p STI says which derivative's map to look in, because the same address
/// can be two different registers.  Passing none looks in all of them, which
/// is right where nothing is being assembled for a particular part.
StringRef getSFRName(const MCRegisterInfo &MRI, uint64_t Addr,
                     const MCSubtargetInfo *STI = nullptr);

} // end namespace C166

} // namespace llvm

// Defines symbolic names for C166 registers.
#define GET_REGINFO_ENUM
#include "C166GenRegisterInfo.inc"

// Defines symbolic names for the C166 instructions.
#define GET_INSTRINFO_ENUM
#define GET_INSTRINFO_MC_HELPER_DECLS
#include "C166GenInstrInfo.inc"

#define GET_SUBTARGETINFO_ENUM
#include "C166GenSubtargetInfo.inc"

#endif
