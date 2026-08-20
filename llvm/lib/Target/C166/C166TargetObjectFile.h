//===-- C166TargetObjectFile.h - C166 Object Info ---------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_C166_C166TARGETOBJECTFILE_H
#define LLVM_LIB_TARGET_C166_C166TARGETOBJECTFILE_H

#include "llvm/CodeGen/TargetLoweringObjectFileImpl.h"
#include "llvm/MC/MCSectionELF.h"

namespace llvm {

/// Puts objects declared in the far address space into their own sections, so
/// that a linker script can place them outside segment 0 where reaching them
/// with a near pointer would not work anyway.
class C166TargetObjectFile : public TargetLoweringObjectFileELF {
  using Base = TargetLoweringObjectFileELF;

public:
  void Initialize(MCContext &Ctx, const TargetMachine &TM) override;

  MCSection *SelectSectionForGlobal(const GlobalObject *GO, SectionKind Kind,
                                    const TargetMachine &TM) const override;

private:
  MCSectionELF *FarDataSection = nullptr;
  MCSectionELF *FarBSSSection = nullptr;
  MCSectionELF *FarRodataSection = nullptr;
  MCSectionELF *FarTextSection = nullptr;
};

} // end namespace llvm

#endif // LLVM_LIB_TARGET_C166_C166TARGETOBJECTFILE_H
