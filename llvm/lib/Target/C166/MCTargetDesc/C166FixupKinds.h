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
  // instruction following the one holding it.  There are two of these because
  // the distance from the byte to the end of the instruction differs: the bit
  // test branches are four bytes with the displacement in the third, and JMPR
  // is two bytes with it in the second.
  fixup_c166_rel8w = FirstTargetFixupKind,
  fixup_c166_rel8w_short,

  /// The target of a branch or call that stays in the current segment.  This
  /// is the same 16 bit field FK_Data_2 would leave, and is a kind of its own
  /// only so that the linker knows the field is a code address reached without
  /// changing segments and can check that it is in the segment we are in.
  fixup_c166_caddr,

  LastTargetFixupKind,
  NumTargetFixupKinds = LastTargetFixupKind - FirstTargetFixupKind
};

} // end namespace C166
} // end namespace llvm

#endif
