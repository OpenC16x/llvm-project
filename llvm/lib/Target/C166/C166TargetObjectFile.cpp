//===-- C166TargetObjectFile.cpp - C166 Object Files ----------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "C166TargetObjectFile.h"
#include "C166.h"
#include "llvm/BinaryFormat/Dwarf.h"
#include "llvm/BinaryFormat/ELF.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/GlobalObject.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/MC/MCContext.h"

using namespace llvm;

void C166TargetObjectFile::Initialize(MCContext &Ctx, const TargetMachine &TM) {
  Base::Initialize(Ctx, TM);

  // The exception tables hold two different kinds of address and the default
  // encoding cannot tell them apart.  DW_EH_PE_absptr means "a target pointer"
  // and is emitted at getCodePointerSize(), which here is four bytes, because
  // a code address on this part is the 24 bit CSP:IP and debug information
  // wants the physical address rather than the offset.  A data pointer is two
  // bytes: uintptr_t is sixteen bits, and the type information objects the
  // type table points at are themselves built out of two byte pointers.
  //
  // So a four byte type table entry is not merely wasteful.  Whatever reads it
  // reads into a uintptr_t, and half of what was written does not fit.  The
  // entries are named explicitly as two byte values instead, which is the size
  // the rest of the C++ ABI already uses on this target.
  TTypeEncoding = dwarf::DW_EH_PE_udata2;

  // The personality routine and the language specific data area are reached
  // from the frame description entry, and both are addresses the unwinder
  // resolves rather than values a program loads into a pointer, so they keep
  // the full physical address that a segmented part needs them to have.
  PersonalityEncoding = dwarf::DW_EH_PE_absptr;
  LSDAEncoding = dwarf::DW_EH_PE_absptr;

  FarDataSection = Ctx.getELFSection(".fardata", ELF::SHT_PROGBITS,
                                     ELF::SHF_ALLOC | ELF::SHF_WRITE);
  FarBSSSection = Ctx.getELFSection(".farbss", ELF::SHT_NOBITS,
                                    ELF::SHF_ALLOC | ELF::SHF_WRITE);
  FarRodataSection =
      Ctx.getELFSection(".farrodata", ELF::SHT_PROGBITS, ELF::SHF_ALLOC);
  FarTextSection = Ctx.getELFSection(".fartext", ELF::SHT_PROGBITS,
                                     ELF::SHF_ALLOC | ELF::SHF_EXECINSTR);

  // The dual-port RAM, which is small and which the linker script places under
  // the stack.  Two sections rather than one because the delay line a filter
  // walks is zero initialised, and a NOBITS section is the difference between
  // that costing nothing in the image and costing its whole length.
  DPRamDataSection = Ctx.getELFSection(".dpramdata", ELF::SHT_PROGBITS,
                                       ELF::SHF_ALLOC | ELF::SHF_WRITE);
  DPRamBSSSection = Ctx.getELFSection(".dprambss", ELF::SHT_NOBITS,
                                      ELF::SHF_ALLOC | ELF::SHF_WRITE);

  // The bit-addressable RAM, which is 128 words and the only memory a bit
  // instruction can name.  Split the same way and for the same reason.
  BitDataSection = Ctx.getELFSection(".bitdata", ELF::SHT_PROGBITS,
                                     ELF::SHF_ALLOC | ELF::SHF_WRITE);
  BitBSSSection = Ctx.getELFSection(".bitbss", ELF::SHT_NOBITS,
                                    ELF::SHF_ALLOC | ELF::SHF_WRITE);
}

MCSection *C166TargetObjectFile::SelectSectionForGlobal(
    const GlobalObject *GO, SectionKind Kind, const TargetMachine &TM) const {
  // A section the user asked for always wins.
  if (GO->hasSection())
    return Base::SelectSectionForGlobal(GO, Kind, TM);

  // __dpram puts an object in the dual-port RAM, because that is the only
  // memory the multiply-accumulate unit's IDX0 and IDX1 can address - PM0036
  // section 2.1, with CoMOV the one exception.  A read-only object marked this
  // way still goes in the writable section: being in the dual-port RAM means
  // being in RAM, so it has to be copied out of the image either way, and
  // there is nowhere else for it to be.
  if (const auto *GV = dyn_cast<GlobalVariable>(GO);
      GV && GV->hasAttribute("c166-dpram"))
    return Kind.isBSS() ? DPRamBSSSection : DPRamDataSection;

  // __bitaddr puts an object in the bit-addressable RAM, which is FD00H to
  // FDFEH and nothing else: a bit instruction takes an 8 bit word number of
  // that space rather than an address, so an object anywhere else has no bit
  // address for one to name.
  if (const auto *GV = dyn_cast<GlobalVariable>(GO);
      GV && GV->hasAttribute("c166-bitaddr"))
    return Kind.isBSS() ? BitBSSSection : BitDataSection;

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
