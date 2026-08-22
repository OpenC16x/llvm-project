//===-- C166UnwindRules.h - C166 call frame information ---------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// DWARF describes a frame with one canonical frame address and, for each
// register, a rule saying where the caller's copy went.  One of those registers
// is the return address, and that is where a C166 does not fit: the return
// address is not in a register and it is not in this frame at all.  It is on
// the hardware stack, a second stack addressed by SP that the compiler never
// touches - CALL pushes onto it, RET pops off it, and everything the ABI cares
// about is on the other stack, the one in R0 that the CFA is measured from.  So
// there is no offset from the CFA that finds the return address, and the rules
// are expressions instead.
//
// What is on the hardware stack at function entry depends on how the function
// was entered:
//
//   near        IP                 2 bytes, and the caller's CSP is our CSP
//   far         IP, CSP            4 bytes, entered with CALLS
//   interrupt   IP, CSP, PSW       6 bytes, put there by the hardware
//
// with the lowest address first, since the stack grows down.  A return address
// is 24 bits - a segment and an offset within it - so for a near function it is
// assembled from the current CSP and the word on the stack, and otherwise from
// two words on the stack.
//
// The rules are in terms of SP, so they change whenever a prologue pushes
// anything onto the hardware stack, which is why they are built with a depth.
//
// The near case at depth zero goes in the CIE, because it is what almost every
// function looks like; only the other shapes say anything in their own frame.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_C166_MCTARGETDESC_C166UNWINDRULES_H
#define LLVM_LIB_TARGET_C166_MCTARGETDESC_C166UNWINDRULES_H

#include "llvm/ADT/SmallVector.h"

namespace llvm {

class MCCFIInstruction;
class MCRegisterInfo;

namespace C166 {

/// How a function was entered, which is what decides the shape of the record
/// the hardware stack holds.
enum class EntryKind {
  /// Entered with a near CALL, which pushed the instruction pointer alone.
  Near,
  /// Entered with CALLS, which pushed the code segment pointer as well.
  Far,
  /// Entered by the hardware, which pushed the program status word too.
  Interrupt,
};

/// The rules that say where the return address is and how to get back to the
/// caller's hardware stack pointer, for a frame entered as \p Kind with
/// \p Depth bytes pushed onto that stack since.
void getUnwindRules(SmallVectorImpl<MCCFIInstruction> &Out,
                    const MCRegisterInfo &MRI, EntryKind Kind, unsigned Depth);

/// Whether \p Kind at \p Depth is what the CIE already says, in which case a
/// function has nothing of its own to add.
inline bool isDefaultUnwind(EntryKind Kind, unsigned Depth) {
  return Kind == EntryKind::Near && Depth == 0;
}

} // namespace C166
} // namespace llvm

#endif
