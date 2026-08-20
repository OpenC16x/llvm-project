//===-- C166MCAsmInfo.h - C166 asm properties -------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_C166_MCTARGETDESC_C166MCASMINFO_H
#define LLVM_LIB_TARGET_C166_MCTARGETDESC_C166MCASMINFO_H

#include "llvm/ADT/StringRef.h"
#include "llvm/MC/MCAsmInfoELF.h"

namespace llvm {
class Triple;

class C166MCAsmInfo : public MCAsmInfoELF {
  void anchor() override;

public:
  explicit C166MCAsmInfo(const Triple &TT, const MCTargetOptions &Options);

  void printSpecifierExpr(raw_ostream &OS,
                          const MCSpecifierExpr &Expr) const override;
};

namespace C166 {
/// Operators that pick one field out of a 24 bit address.  A segmented
/// address is either a segment plus a 16 bit offset, as EXTS, JMPS and CALLS
/// want it, or a page plus a 14 bit offset, as EXTP wants it.
enum Specifier : uint16_t {
  S_None = 0,
  S_SEG, ///< seg(x): bits 23-16, the segment number.
  S_SOF, ///< sof(x): bits 15-0, the offset within that segment.
  S_PAG, ///< pag(x): bits 23-14, the page number.
  S_POF, ///< pof(x): bits 13-0, the offset within that page.
};

/// Map "seg" and friends onto their specifier, or S_None if Name is not one.
Specifier parseSpecifier(StringRef Name);

/// The spelling of a specifier, for printing it back out.
StringRef getSpecifierName(uint16_t S);

/// Reduce a fully resolved 24 bit address to the field the specifier names.
uint64_t applySpecifier(uint16_t S, uint64_t Value);
} // namespace C166

} // namespace llvm

#endif
