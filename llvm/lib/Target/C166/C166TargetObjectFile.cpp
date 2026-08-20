//===-- C166TargetObjectFile.cpp - C166 Object Files ----------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "C166TargetObjectFile.h"
#include "C166.h"
#include "llvm/BinaryFormat/ELF.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/GlobalObject.h"
#include "llvm/MC/MCContext.h"

using namespace llvm;

void C166TargetObjectFile::Initialize(MCContext &Ctx, const TargetMachine &TM) {
  Base::Initialize(Ctx, TM);

  FarDataSection = Ctx.getELFSection(".fardata", ELF::SHT_PROGBITS,
                                     ELF::SHF_ALLOC | ELF::SHF_WRITE);
  FarBSSSection = Ctx.getELFSection(".farbss", ELF::SHT_NOBITS,
                                    ELF::SHF_ALLOC | ELF::SHF_WRITE);
  FarRodataSection =
      Ctx.getELFSection(".farrodata", ELF::SHT_PROGBITS, ELF::SHF_ALLOC);
  FarTextSection = Ctx.getELFSection(".fartext", ELF::SHT_PROGBITS,
                                     ELF::SHF_ALLOC | ELF::SHF_EXECINSTR);
}

MCSection *C166TargetObjectFile::SelectSectionForGlobal(
    const GlobalObject *GO, SectionKind Kind, const TargetMachine &TM) const {
  // A section the user asked for always wins.
  if (GO->hasSection())
    return Base::SelectSectionForGlobal(GO, Kind, TM);

  // A far function is entered with CALLS and left with RETS, which is only
  // worth the extra stack word if it can end up in another segment.
  if (const auto *F = dyn_cast<Function>(GO); F && F->hasFnAttribute("far"))
    return FarTextSection;

  // Likewise an object the compiler reaches with a far pointer: it needs to be
  // somewhere the linker can place outside segment 0, or the far addressing
  // buys nothing.
  if (GO->getAddressSpace() != C166AS::Far)
    return Base::SelectSectionForGlobal(GO, Kind, TM);

  if (Kind.isBSS())
    return FarBSSSection;
  if (Kind.isReadOnly())
    return FarRodataSection;
  return FarDataSection;
}
