//===--- C166.cpp - Implement C166 target feature support -----------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements C166 TargetInfo objects.
//
//===----------------------------------------------------------------------===//

#include "C166.h"
#include "clang/Basic/MacroBuilder.h"

using namespace clang;
using namespace clang::targets;

// The general purpose registers, plus the byte halves of the first eight and
// the special function registers the backend models.  These are the names an
// asm statement can put in a clobber list.
const char *const C166TargetInfo::GCCRegNames[] = {
    "r0",  "r1",  "r2",  "r3",  "r4",  "r5",  "r6",  "r7",  "r8",  "r9",
    "r10", "r11", "r12", "r13", "r14", "r15", "rl0", "rh0", "rl1", "rh1",
    "rl2", "rh2", "rl3", "rh3", "rl4", "rh4", "rl5", "rh5", "rl6", "rh6",
    "rl7", "rh7", "psw", "mdl", "mdh", "mdc", "sp",  "cp"};

ArrayRef<const char *> C166TargetInfo::getGCCRegNames() const {
  return llvm::ArrayRef(GCCRegNames);
}

void C166TargetInfo::getTargetDefines(const LangOptions &Opts,
                                      MacroBuilder &Builder) const {
  Builder.defineMacro("C166");
  Builder.defineMacro("__C166__");
  Builder.defineMacro("__c166__");
  // So that code can ask whether the multiply-accumulate coprocessor is there
  // rather than guessing from the part number.
  if (HasMAC)
    Builder.defineMacro("__C166_MAC__");

  // Far data lives in address space 1.  Spell it the way the vendor
  // toolchains do; the name is in the reserved identifier space, so it cannot
  // collide with anything the user wrote.
  Builder.defineMacro("__far", "__attribute__((address_space(1)))");
}
