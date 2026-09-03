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
  case R_C166_BITOFF8: {
    // Not an address: the 8 bit word number of the bit-addressable space.  The
    // mapping has two windows counted from two bases, and an address in
    // neither is not a value that has been truncated - there is no bit
    // instruction that reaches it at all, so say that rather than write a byte
    // that names some other word.
    uint64_t addr = val & 0xffffffff;
    int off = -1;
    if (addr <= 0xFFFF && !(addr & 1)) {
      if (addr >= 0xFD00 && addr <= 0xFDFE)
        off = int((addr - 0xFD00) / 2);
      else if (addr >= 0xFF00 && addr <= 0xFFDE)
        off = int(0x80 + (addr - 0xFF00) / 2);
    }
    if (off < 0) {
      Err(ctx) << getErrorLoc(ctx, loc) << "bit variable "
               << (rel.sym ? rel.sym->getName() : StringRef("target"))
               << " is at " << utohexstr(addr, /*LowerCase=*/true)
               << ", which is not in the bit-addressable space: a bit "
                  "instruction reaches an even address in fd00-fdfe or "
                  "ff00-ffde and nothing else";
      return;
    }
    *loc = uint8_t(off);
    break;
  }
  default:
    Err(ctx) << getErrorLoc(ctx, loc) << "unrecognized relocation " << rel.type;
  }
}

// There is no linker relaxation here, and it is not an omission.  Three
// shrinks are available in principle once the layout is fixed, and each is
// declined for a different reason: one on measurement, and two because they
// are not things a linker can do at all.  A fourth, which needs no linker,
// is declined on measurement next door - C166's README.txt has it under
// "Why the calls are not relaxed either".
//
// JMPA and CALLA, four bytes and absolute, could be JMPR and CALLR, two bytes
// and relative, whenever the target turns out to be within the signed 8 bit
// word displacement.  lld has the machinery: relaxOnce, finalizeRelax and
// RelaxAux delete bytes from a section and move the symbols and relocations
// that follow.  What that machinery needs from the object file is a relocation
// for every branch it might have to fix up, because deleting bytes moves every
// target after the deletion.
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
// of what this assembler does today, and it puts the branches the assembler
// already shortens at stake against the ones only the linker can.
//
// Both sides of that were measured over utils/C166Sim/corpus, which is drivers
// over LLVM's own libc rather than programs written to exercise this backend -
// relax.sh there re-derives the table, and relax.py says how each column is
// counted.  At a fixed point, so counting the shrinks that only become
// possible once earlier ones have moved things closer:
//
//     image        text   long   win   win%   short  stake  stake%
//     numbers -O2   8118    71     8   0.10     323    646    7.96
//     numbers -Os   5602    30    10   0.18     223    446    7.96
//     parsing -O2   8894   166    20   0.22     362    724    8.14
//     parsing -Os  10382   155    20   0.19     481    962    9.27
//     sorting -O2  20877   193    64   0.31     479    958    4.59
//     sorting -Os  17982   188    68   0.38     436    872    4.85
//     strings -O2   5038    47     8   0.16     174    348    6.91
//     strings -Os   3756    52    10   0.27     191    382   10.17
//
// So the linker would win 8 to 68 bytes, a tenth to four tenths of one per
// cent, and to get at them the assembler would have to put 348 to 962 bytes -
// five to ten per cent - back into the linker's hands.  The linker's first job
// under that scheme is to return to where the assembler already is, and the
// surplus beyond it is a third of one per cent at best.  The reason the
// surplus is so small is in the "long" column rather than in the machinery:
// the compiler has already taken the near branches, and what is left is
// genuinely far.  Across the eight images above there are 902 of these, of
// which 104 have a target within 254 bytes - a JMPR's forward reach - and the
// median target is 829 bytes away.
//
// CALLS, a far call naming its segment, could be a CALLA or a CALLR when the
// callee turns out to be in the calling segment - which is not a rare case
// here, since c166.ld puts .fartext in the same segment as .text on a part
// whose program memory is one segment.  But the far call sequence is a
// contract between the two ends and not an encoding: CALLS pushes CSP and IP
// and the callee returns with RETS, which pops both.  Turning the call near
// means turning the callee's RETS into a RET, and the linker cannot know
// whether some other caller - in another object, in another segment, through a
// pointer - still needs the far form.  This is the same reason the compiler
// cannot mix the two, which C166's README.txt gives at greater length.
//
// An EXTS ahead of an access to an object that landed inside the window the
// data page pointers cover is dead, and could go.  Except that the window is
// not in the object file: DPP0 to DPP3 are written by startup code, and this
// toolchain's own crt0 writes pages 300H, 301H, 302H and 3 rather than the
// reset 0, 1, 2, 3 - so the near window here is the first 48 KByte of segment
// C0H plus the RAM, and a linker that assumed the reset mapping would delete
// an EXTS the program needs.  The corpus agrees about the size of the prize
// even if the question were answerable: three EXTS per program name a segment
// as an immediate at all, and all three name E0H, which no DPP covers.  Every
// other EXTS in those images takes its segment from a register, which is a run
// time value and nothing a linker could fold.
//
// The measurements are worth repeating rather than trusting:
// llvm/utils/C166Sim/corpus/relax.sh prints both tables, and moves when the
// compiler does.

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
