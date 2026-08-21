//===- C166.cpp -----------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// The Infineon C166 is a 16 bit microcontroller family.  Code and data are
// reached with 16 bit near addresses within one segment, and the full 16 MByte
// bus is reached by naming a segment or a page separately -- which is what the
// four relocations that are not plain data are for.  Everything is little
// endian and every instruction is two or four bytes.
//
// There is no dynamic linking here and no published psABI: the relocation
// numbers come from LLVM's own C166 backend, and ELFRelocs/C166.def is where
// they are written down.
//
//===----------------------------------------------------------------------===//

#include "Symbols.h"
#include "Target.h"
#include "llvm/BinaryFormat/ELF.h"
#include "llvm/Support/Endian.h"

using namespace llvm;
using namespace llvm::object;
using namespace llvm::support::endian;
using namespace llvm::ELF;
using namespace lld;
using namespace lld::elf;

namespace {
class C166 final : public TargetInfo {
public:
  C166(Ctx &);
  RelExpr getRelExpr(RelType type, const Symbol &s,
                     const uint8_t *loc) const override;
  void relocate(uint8_t *loc, const Relocation &rel,
                uint64_t val) const override;
};
} // namespace

C166::C166(Ctx &ctx) : TargetInfo(ctx) {
  // "jmpr cc_UC, -1", which branches to itself.  Padding is not meant to be
  // reached, and on a part with no operating system underneath it, hanging
  // where the mistake happened is more use than sliding into whatever comes
  // next.
  trapInstr = {0x0d, 0xff, 0x0d, 0xff};
}

RelExpr C166::getRelExpr(RelType type, const Symbol &s,
                         const uint8_t *loc) const {
  switch (type) {
  case R_C166_PCREL8:
  case R_C166_PCREL16:
  case R_C166_PCREL32:
  case R_C166_PCREL8W:
    return R_PC;
  case R_C166_NONE:
    return R_NONE;
  default:
    return R_ABS;
  }
}

void C166::relocate(uint8_t *loc, const Relocation &rel, uint64_t val) const {
  switch (rel.type) {
  case R_C166_ABS8:
  case R_C166_PCREL8:
    checkIntUInt(ctx, loc, val, 8, rel);
    *loc = val;
    break;
  case R_C166_ABS16:
  case R_C166_PCREL16:
    checkIntUInt(ctx, loc, val, 16, rel);
    write16le(loc, val);
    break;
  case R_C166_ABS32:
  case R_C166_PCREL32:
    checkIntUInt(ctx, loc, val, 32, rel);
    write32le(loc, val);
    break;
  case R_C166_ABS64:
    write64le(loc, val);
    break;
  case R_C166_PCREL8W: {
    // The field counts words from the instruction after the branch, and sits
    // in the branch's second word, so val is two bytes short of the distance
    // the hardware measures.
    checkAlignment(ctx, loc, val, 2, rel);
    int64_t offset = (static_cast<int64_t>(val) >> 1) - 1;
    checkInt(ctx, loc, offset, 8, rel);
    *loc = offset & 0xff;
    break;
  }
  case R_C166_SEG8:
    // The segment is the top eight bits of a 24 bit address.  Anything above
    // that is not addressable at all.
    checkUInt(ctx, loc, val, 24, rel);
    *loc = (val >> 16) & 0xff;
    break;
  case R_C166_SOF16:
    // The offset within the segment named by an EXTS.  It fills the word, so
    // there is nothing to check.
    write16le(loc, val & 0xffff);
    break;
  case R_C166_PAG10:
    // The page is bits 23 to 14 of the address, in the low ten bits of the
    // word; the six above it belong to the instruction.
    checkUInt(ctx, loc, val, 24, rel);
    write16le(loc, (read16le(loc) & 0xfc00) | ((val >> 14) & 0x3ff));
    break;
  case R_C166_POF14:
    // The offset within the page, in the low fourteen bits of the word.
    write16le(loc, (read16le(loc) & 0xc000) | (val & 0x3fff));
    break;
  default:
    Err(ctx) << getErrorLoc(ctx, loc) << "unrecognized relocation " << rel.type;
  }
}

void elf::setC166TargetInfo(Ctx &ctx) { ctx.target.reset(new C166(ctx)); }
