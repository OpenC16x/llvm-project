//===-- C166AsmBackend.cpp - C166 Assembler Backend -----------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "MCTargetDesc/C166FixupKinds.h"
#include "MCTargetDesc/C166MCAsmInfo.h"
#include "MCTargetDesc/C166MCTargetDesc.h"
#include "llvm/BinaryFormat/ELF.h"
#include "llvm/MC/MCAsmBackend.h"
#include "llvm/MC/MCAssembler.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCELFObjectWriter.h"
#include "llvm/MC/MCFixup.h"
#include "llvm/MC/MCObjectWriter.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/MC/MCTargetOptions.h"
#include "llvm/MC/MCValue.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;

namespace {

class C166AsmBackend : public MCAsmBackend {
  uint8_t OSABI;

public:
  C166AsmBackend(const MCSubtargetInfo &STI, uint8_t OSABI)
      : MCAsmBackend(llvm::endianness::little), OSABI(OSABI) {}

  ~C166AsmBackend() override = default;

  MCFixupKindInfo getFixupKindInfo(MCFixupKind Kind) const override {
    // This table must be kept in the same order as the enum in
    // C166FixupKinds.h.
    const static MCFixupKindInfo Infos[C166::NumTargetFixupKinds] = {
        // name                offset bits flags
        {"fixup_c166_rel8w", 0, 8, 0},
    };
    static_assert(std::size(Infos) == C166::NumTargetFixupKinds,
                  "Not all fixup kinds added to Infos array");

    if (Kind < FirstTargetFixupKind)
      return MCAsmBackend::getFixupKindInfo(Kind);
    return Infos[Kind - FirstTargetFixupKind];
  }

  void applyFixup(const MCFragment &F, const MCFixup &Fixup,
                  const MCValue &Target, uint8_t *Data, uint64_t Value,
                  bool IsResolved) override;

  std::unique_ptr<MCObjectTargetWriter>
  createObjectTargetWriter() const override {
    return createC166ELFObjectWriter(OSABI);
  }

  bool writeNopData(raw_ostream &OS, uint64_t Count,
                    const MCSubtargetInfo *STI) const override;
};

void C166AsmBackend::applyFixup(const MCFragment &F, const MCFixup &Fixup,
                                const MCValue &Target, uint8_t *Data,
                                uint64_t Value, bool IsResolved) {
  maybeAddReloc(F, Fixup, Target, Value, IsResolved);

  // A seg/sof/pag/pof operator over something the assembler can already work
  // out, such as an absolute address, is folded here; when it names a symbol
  // the relocation above carries the whole address and the linker does the
  // splitting instead.
  Value = C166::applySpecifier(Target.getSpecifier(), Value);

  // A relative branch counts words from the instruction after the one it sits
  // in, while the fixup was measured to the displacement byte itself, which is
  // two bytes into the instruction.  Halving turns bytes into words and the
  // decrement moves the origin on to the next instruction.
  if (Fixup.getKind() == C166::fixup_c166_rel8w) {
    // When the target is not known yet the relocation above carries the whole
    // distance and the linker does this instead; Value has been zeroed and
    // must stay that way.
    if (!IsResolved)
      return;
    if (Value & 1)
      getContext().reportError(Fixup.getLoc(),
                               "branch target must be 2-byte aligned");
    int64_t Offset = (static_cast<int64_t>(Value) >> 1) - 1;
    if (Offset < -128 || Offset > 127)
      getContext().reportError(Fixup.getLoc(), "branch target out of range");
    Value = static_cast<uint64_t>(Offset) & 0xff;
  }

  if (!Value)
    return; // Doesn't change encoding.

  MCFixupKindInfo Info = getFixupKindInfo(Fixup.getKind());
  unsigned NumBytes = alignTo(Info.TargetSize, 8) / 8;
  assert(Fixup.getOffset() + NumBytes <= F.getSize() && "Invalid fixup offset");

  // C166 is little endian and every fixup lands on a whole number of bytes.
  for (unsigned I = 0; I != NumBytes; ++I)
    Data[I] |= uint8_t((Value >> (I * 8)) & 0xff);
}

bool C166AsmBackend::writeNopData(raw_ostream &OS, uint64_t Count,
                                  const MCSubtargetInfo *STI) const {
  // Instructions are always an even number of bytes, so an odd amount of
  // padding cannot be expressed as NOPs.
  if ((Count % 2) != 0)
    return false;

  // NOP is CC 00.
  for (uint64_t I = 0, E = Count / 2; I != E; ++I)
    OS.write("\xCC\x00", 2);

  return true;
}

} // end anonymous namespace

MCAsmBackend *llvm::createC166MCAsmBackend(const Target &T,
                                           const MCSubtargetInfo &STI,
                                           const MCRegisterInfo &MRI,
                                           const MCTargetOptions &Options) {
  return new C166AsmBackend(STI, ELF::ELFOSABI_STANDALONE);
}
