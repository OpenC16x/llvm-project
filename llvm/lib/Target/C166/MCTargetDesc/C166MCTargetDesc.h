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
