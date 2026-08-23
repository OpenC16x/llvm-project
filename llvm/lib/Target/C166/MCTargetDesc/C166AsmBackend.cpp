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
        // name                      offset bits flags
        {"fixup_c166_rel8w", 0, 8, 0},
        {"fixup_c166_rel8w_short", 0, 8, 0},
        {"fixup_c166_caddr", 0, 16, 0},
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

  // JMPR reaches 127 words either way, and whether the target is that close
  // is not known until the layout is.  So the short form is what gets emitted
  // and the assembler grows it into the long one where it has to.
  bool mayNeedRelaxation(unsigned Opcode, ArrayRef<MCOperand> Operands,
                         const MCSubtargetInfo &STI) const override;
  bool fixupNeedsRelaxationAdvanced(const MCFragment &, const MCFixup &,
                                    const MCValue &, uint64_t Value,
                                    bool Resolved) const override;
  void relaxInstruction(MCInst &Inst,
                        const MCSubtargetInfo &STI) const override;
};

/// The long form of a relative jump, or 0 if there is not one.
static unsigned getRelaxedOpcode(unsigned Opcode) {
  switch (Opcode) {
  case C166::JMPR:
    return C166::JMPA;
  case C166::JMPRcc:
    return C166::JMPAcc;
  default:
    return 0;
  }
}

bool C166AsmBackend::mayNeedRelaxation(unsigned Opcode,
                                       ArrayRef<MCOperand> Operands,
                                       const MCSubtargetInfo &STI) const {
  if (!getRelaxedOpcode(Opcode))
    return false;
  // A displacement written as a number is a distance, and the long form takes
  // an address, so there would be nothing to turn it into.  Only a reference
  // to a label can be relaxed.
  return !Operands.empty() && Operands[0].isExpr();
}

bool C166AsmBackend::fixupNeedsRelaxationAdvanced(const MCFragment &,
                                                  const MCFixup &Fixup,
                                                  const MCValue &,
                                                  uint64_t Value,
                                                  bool Resolved) const {
  if (Fixup.getKind() != C166::fixup_c166_rel8w_short)
    return false;
  // Somewhere else entirely, so the distance is whatever the linker makes it.
  if (!Resolved)
    return true;
  int64_t Distance = static_cast<int64_t>(Value) - 1;
  int64_t Offset = Distance >> 1;
  return (Distance & 1) || Offset < -128 || Offset > 127;
}

void C166AsmBackend::relaxInstruction(MCInst &Inst,
                                      const MCSubtargetInfo &STI) const {
  // The two forms take their operands in the same order, so this is only a
  // change of opcode: a relative target becomes an absolute one, and the code
  // emitter writes the other relocation for it.
  unsigned Relaxed = getRelaxedOpcode(Inst.getOpcode());
  assert(Relaxed && "relaxInstruction() on something with no long form");
  Inst.setOpcode(Relaxed);
}

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
  // in, while the fixup was measured to the displacement byte itself.  How far
  // that byte is from the end of the instruction is what the two kinds differ
  // by: two bytes in a four byte instruction, one in a two byte one.
  if (Fixup.getKind() == C166::fixup_c166_rel8w ||
      Fixup.getKind() == C166::fixup_c166_rel8w_short) {
    // When the target is not known yet the relocation above carries the whole
    // distance and the linker does this instead; Value has been zeroed and
    // must stay that way.
    if (!IsResolved)
      return;
    int64_t ToEnd = Fixup.getKind() == C166::fixup_c166_rel8w ? 2 : 1;
    int64_t Distance = static_cast<int64_t>(Value) - ToEnd;
    if (Distance & 1)
      getContext().reportError(Fixup.getLoc(),
                               "branch target must be 2-byte aligned");
    int64_t Offset = Distance >> 1;
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
