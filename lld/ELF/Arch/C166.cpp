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

#include "InputSection.h"
#include "OutputSections.h"
#include "Symbols.h"
#include "Target.h"
#include "llvm/BinaryFormat/ELF.h"
#include "llvm/ADT/StringExtras.h"
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
  void relocateAlloc(InputSection &sec, uint8_t *buf) const override;
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
  case R_C166_SEG8: {
    // The segment is the top eight bits of a 24 bit address.  Anything above
    // that is not addressable at all.
    checkUInt(ctx, loc, val, 24, rel);
    // A far access names its object's segment as an immediate, and the
    // compiler folds two of those into one EXTend whenever both reach the
    // same object: "seg(g)" and "seg(g + 2)" are the same segment unless g
    // straddles a segment boundary.  Nothing in an object file rules that
    // out, and by the time the fold has happened there is no second segment
    // left to disagree with the first - the wrong one would simply be used,
    // and the access would land at the right offset in the wrong segment.
    //
    // So it is ruled out here, where the placement is finally known.  This is
    // a little stronger than the fold strictly needs: an object that crosses
    // a boundary is rejected even in code that happened not to be folded and
    // would have worked.  The alternative is to carry the fold's assumption
    // into the object file as a relocation of its own, which is a larger
    // thing to add than the case is worth - an object placed across a segment
    // boundary is not something a working program does on purpose.
    if (rel.sym) {
      uint64_t size = rel.sym->getSize();
      if (size > 1) {
        uint64_t base = rel.sym->getVA(ctx);
        if ((base >> 16) != ((base + size - 1) >> 16))
          Err(ctx) << getErrorLoc(ctx, loc) << "far object '" << rel.sym->getName()
                   << "' crosses a segment boundary: it is placed at 0x"
                   << utohexstr(base) << " and is " << Twine(size)
                   << " bytes, so it ends in segment 0x"
                   << utohexstr((base + size - 1) >> 16) << " rather than 0x"
                   << utohexstr(base >> 16)
                   << ". A far access names one segment for the whole object, "
                      "so place it inside a single 64 KByte segment.";
      }
    }
    *loc = (val >> 16) & 0xff;
    break;
  }
  case R_C166_SOF16:
    // The offset within the segment named by an EXTS.  It fills the word, so
    // there is nothing to check.
    write16le(loc, val & 0xffff);
    break;
  case R_C166_CADDR16:
    // The same field, for a branch or call that does not leave the segment.
    // Whether it should have is checked in relocateAlloc below, which is where
    // the address of the instruction itself is known.
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

// There is no linker relaxation here, and it is not an omission.
//
// A branch or call whose target lands close after layout could be the two byte
// relative form rather than the four byte absolute one, and lld has the
// machinery for that: relaxOnce, finalizeRelax and RelaxAux delete bytes from a
// section and move the symbols and relocations that follow.  What that
// machinery needs from the object file is a relocation for every branch it
// might have to fix up, because deleting bytes moves every target after the
// deletion.
//
// This assembler does not leave one.  An intra-section branch is resolved at
// assembly time and its displacement is baked into the bytes, so:
//
//     42: 3d 04    jmpr cc_NE, 0x4c   <- no relocation
//     48: ea 00 00 00  jmpa cc_UC, 0
//               4a: R_C166_CADDR16 __pll_lock_failed
//     4c: ...
//
// shrinking the JMPA at 48 moves the instruction at 4c to 4a, and the JMPR at
// 42 still says "forward four words" and lands in the middle of it.  This was
// written, and that is what it did.
//
// Making it work means the assembler leaving a relocation for every branch
// instead of resolving it - which also means it can no longer pick the short
// form itself, since it would no longer know the distance, and would have to
// emit the long form everywhere and rely on the linker to shrink it back.
// That is a coherent design and it is how RISC-V works, but it is the opposite
// of what this assembler does today.
//
// Measured before writing any of it, over the differential programs: after
// crt0 stopped spelling its local branches JMPA, what was left for a linker to
// win was four to eleven sites per program, eight to twenty two bytes.  That
// is not enough to pay for inverting how branches are assembled.

// JMPA and CALLA take their segment from CSP, so they reach the segment the
// instruction is in and no other.  Nothing in the instruction says which one
// that is, and a target somewhere else is not diagnosed by anything else: the
// sixteen bits that fit are written, the branch goes to that offset in the
// wrong segment, and the program runs somewhere it was never meant to.
//
// It is a real way to get there rather than a theoretical one.  A function
// placed in the PSRAM at E0'0000H - which is what that memory is for, and what
// a routine that rewrites the Flash has to do - calls an ordinary function in
// the Flash at C0'xxxxH with a CALLA, and the call lands in the PSRAM instead.
// So the only fix is that such a call must not be near, and the only place that
// can be seen is here.
//
// The relocation exists to carry that question this far; everything else about
// it is R_C166_SOF16.
void C166::relocateAlloc(InputSection &sec, uint8_t *buf) const {
  const uint64_t secAddr = sec.getOutputSection()->addr + sec.outSecOff;
  for (const Relocation &rel : sec.relocs()) {
    uint8_t *loc = buf + rel.offset;
    const uint64_t p = secAddr + rel.offset;
    const uint64_t val = SignExtend64(sec.getRelocTargetVA(ctx, rel, p), 32);

    if (rel.type == R_C166_CADDR16 && (val >> 16) != (p >> 16))
      Err(ctx) << getErrorLoc(ctx, loc) << "branch or call to "
               << (rel.sym ? rel.sym->getName() : StringRef("target"))
               << " leaves the segment it is in: the target is at "
               << utohexstr(val, /*LowerCase=*/true) << " and the instruction "
               << "at " << utohexstr(p, /*LowerCase=*/true)
               << ", and this form of branch cannot change segments - a call "
                  "out of one segment into another has to be a far one";

    if (rel.expr != R_RELAX_HINT)
      relocate(loc, rel, val);
  }
}

void elf::setC166TargetInfo(Ctx &ctx) { ctx.target.reset(new C166(ctx)); }
