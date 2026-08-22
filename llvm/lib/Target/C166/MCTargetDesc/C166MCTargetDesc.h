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

/// The 8 bit short address of the named register, or -1 if the name is not
/// one.
int64_t getSFRShortAddress(const MCRegisterInfo &MRI, StringRef Name);

/// The address the named register is mapped at, or -1 if the name is not one.
int64_t getSFRAddress(const MCRegisterInfo &MRI, StringRef Name);

/// The name of the register mapped at \p Addr, or empty if nothing is.
StringRef getSFRName(const MCRegisterInfo &MRI, uint64_t Addr);

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
