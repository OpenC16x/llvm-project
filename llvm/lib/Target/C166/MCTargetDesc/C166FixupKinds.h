//===-- C166FixupKinds.h - C166 Specific Fixup Entries ----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_C166_MCTARGETDESC_C166FIXUPKINDS_H
#define LLVM_LIB_TARGET_C166_MCTARGETDESC_C166FIXUPKINDS_H

#include "llvm/MC/MCFixup.h"

namespace llvm {
namespace C166 {

// This table must be kept in the same order as the Infos array in
// C166AsmBackend::getFixupKindInfo().
enum Fixups {
  // The relative branch displacement: a signed 8 bit count of words from the
  // instruction following the one holding it.
  fixup_c166_rel8w = FirstTargetFixupKind,

  LastTargetFixupKind,
  NumTargetFixupKinds = LastTargetFixupKind - FirstTargetFixupKind
};

} // end namespace C166
} // end namespace llvm

#endif
