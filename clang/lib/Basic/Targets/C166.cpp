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
// asm statement can put in a clobber list, and the names it can pin a value
// to with "register ... __asm__(name)" or a "{name}" constraint.
//
// The coprocessor's are here because there is no other way to reach them.
// IDX0 and IDX1 are the pointers its indirect forms run on and the
// instruction encodes which, so there is nothing for a register class
// constraint to choose between - naming the one meant is the only thing that
// makes sense.  The accumulator and the rest are here so that an asm
// statement can say it destroys them, which matters as soon as anything else
// is using the unit.
//
// The list is flat, as a GCC register list is: it does not know which part is
// selected.  Naming QX0 on a part with no coprocessor is therefore accepted
// here and refused by the assembler, which is where the map is known.
const char *const C166TargetInfo::GCCRegNames[] = {
    "r0",   "r1",   "r2",   "r3",   "r4",   "r5",   "r6",   "r7",
    "r8",   "r9",   "r10",  "r11",  "r12",  "r13",  "r14",  "r15",
    "rl0",  "rh0",  "rl1",  "rh1",  "rl2",  "rh2",  "rl3",  "rh3",
    "rl4",  "rh4",  "rl5",  "rh5",  "rl6",  "rh6",  "rl7",  "rh7",
    "psw",  "mdl",  "mdh",  "mdc",  "sp",   "cp",
    // The multiply-accumulate unit: its two pointers, its four offset
    // registers, and the registers CoSTORE names.
    "idx0", "idx1", "qx0",  "qx1",  "qr0",  "qr1",
    "mal",  "mah",  "mas",  "mcw",  "msw",  "mrw"};

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
  // And whether ATOMIC and the EXTend instructions are, which is what decides
  // whether a far object can be reached at all.  The runtime's far block moves
  // ask this rather than being built for a part that cannot run them.
  if (HasExtInstr)
    Builder.defineMacro("__C166_EXT_INSTR__");

  // The internal dual-port RAM, which is the only memory the coprocessor's
  // IDX pointers reach.  A placement rather than an address space: it is a
  // near address in page 3 like the rest of the RAM, so a pointer into it is
  // an ordinary pointer.  The name is in the reserved identifier space, so it
  // cannot collide with anything the user wrote.
  Builder.defineMacro("__dpram", "__attribute__((c166_dpram))");

  // The bit-addressable RAM, where setting or testing one bit of a variable is
  // a single indivisible instruction.  A placement like __dpram and for the
  // same reason: the address is an ordinary near one, and what matters is
  // which memory the object is in.
  Builder.defineMacro("__bitaddr", "__attribute__((c166_bitaddr))");

  // Far data lives in address space 1.  Spell it the way the vendor
  // toolchains do; the name is in the reserved identifier space, so it cannot
  // collide with anything the user wrote.
  Builder.defineMacro("__far", "__attribute__((address_space(1)))");
}
