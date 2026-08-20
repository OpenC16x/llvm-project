//===-- C166MCAsmInfo.h - C166 asm properties -------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_C166_MCTARGETDESC_C166MCASMINFO_H
#define LLVM_LIB_TARGET_C166_MCTARGETDESC_C166MCASMINFO_H

#include "llvm/MC/MCAsmInfoELF.h"

namespace llvm {
class Triple;

class C166MCAsmInfo : public MCAsmInfoELF {
  void anchor() override;

public:
  explicit C166MCAsmInfo(const Triple &TT, const MCTargetOptions &Options);
};

} // namespace llvm

#endif
