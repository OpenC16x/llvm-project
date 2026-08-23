//===-- C166ELFObjectWriter.cpp - C166 ELF Writer -------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "MCTargetDesc/C166FixupKinds.h"
#include "MCTargetDesc/C166MCAsmInfo.h"
#include "MCTargetDesc/C166MCTargetDesc.h"
#include "llvm/ADT/Twine.h"
#include "llvm/BinaryFormat/ELF.h"
#include "llvm/MC/MCELFObjectWriter.h"
#include "llvm/MC/MCFixup.h"
#include "llvm/MC/MCObjectWriter.h"
#include "llvm/MC/MCValue.h"
#include "llvm/Support/ErrorHandling.h"

using namespace llvm;

namespace {

class C166ELFObjectWriter : public MCELFObjectTargetWriter {
public:
  explicit C166ELFObjectWriter(uint8_t OSABI)
      : MCELFObjectTargetWriter(/*Is64Bit_=*/false, OSABI, ELF::EM_C166,
                                /*HasRelocationAddend_=*/true) {}

  ~C166ELFObjectWriter() override = default;

protected:
  unsigned getRelocType(const MCFixup &Fixup, const MCValue &Target,
                        bool IsPCRel) const override {
    // A seg/sof/pag/pof operator asks the linker to split a 24 bit address,
    // so the relocation is decided by the specifier rather than the width of
    // the field it lands in.
    switch (Target.getSpecifier()) {
    case C166::S_None:
      break;
    case C166::S_SEG:
      return ELF::R_C166_SEG8;
    case C166::S_SOF:
      return ELF::R_C166_SOF16;
    case C166::S_PAG:
      return ELF::R_C166_PAG10;
    case C166::S_POF:
      return ELF::R_C166_POF14;
    }

    // Everything else is plain data: a 16 bit near pointer in the second word
    // of an instruction, or a data word.
    //
    // A near reference is 16 bits of a 24 bit address, and which 16 is decided
    // by the hardware rather than by the linker: the DPPs supply the page for
    // a data access and CSP the segment for a branch, and both leave the low
    // 16 bits of the address to the instruction.  So what a near reference
    // wants is the offset within the segment, not the whole address, which is
    // why FK_Data_2 relocates as SOF16.  For anything in segment 0 the two are
    // the same value, so this only starts to matter once something is linked
    // above 00'FFFFH.
    switch (Fixup.getKind()) {
    case C166::fixup_c166_rel8w:
      return ELF::R_C166_PCREL8W;
    case C166::fixup_c166_rel8w_short:
      // JMPR is the only thing that uses this, and it has a long form, so a
      // target the assembler cannot place turns into a JMPA rather than
      // arriving here.  There is deliberately no relocation for it.
      reportError(Fixup.getLoc(),
                  "a relative jump to a target this far away should have been "
                  "relaxed");
      return ELF::R_C166_NONE;
    case C166::fixup_c166_caddr:
      // The same value as SOF16, said differently: this one is a code address
      // that the instruction reaches without leaving the segment, which is
      // what lets the linker check the segment is the one it is in.
      return ELF::R_C166_CADDR16;
    case FK_Data_1:
      return IsPCRel ? ELF::R_C166_PCREL8 : ELF::R_C166_ABS8;
    case FK_Data_2:
      return IsPCRel ? ELF::R_C166_PCREL16 : ELF::R_C166_SOF16;
    case FK_Data_4:
      return IsPCRel ? ELF::R_C166_PCREL32 : ELF::R_C166_ABS32;
    case FK_Data_8:
      if (IsPCRel)
        break;
      return ELF::R_C166_ABS64;
    default:
      break;
    }
    llvm_unreachable("Invalid fixup kind for C166");
  }
};

} // end anonymous namespace

std::unique_ptr<MCObjectTargetWriter>
llvm::createC166ELFObjectWriter(uint8_t OSABI) {
  return std::make_unique<C166ELFObjectWriter>(OSABI);
}
