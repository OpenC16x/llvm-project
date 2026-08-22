//===-- Unwind.h - Walk the stack with the program's own CFI ----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// A backtrace, produced by running the call frame information the compiler
// emitted against the machine as it stands.  That makes it a check on that
// information and not only a convenience: the two stacks a C166 has mean the
// return address is not at any offset from the canonical frame address, so the
// rule that finds it is a DWARF expression, and an expression that is wrong is
// not something reading the assembly would show.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_UTILS_C166SIM_UNWIND_H
#define LLVM_UTILS_C166SIM_UNWIND_H

#include "llvm/ADT/StringRef.h"
#include "llvm/Object/ObjectFile.h"
#include "llvm/Support/raw_ostream.h"
#include <cstdint>
#include <string>
#include <vector>

namespace c166sim {

class Machine;

/// One frame of a backtrace.
struct Frame {
  /// The 24 bit code address, which is CSP:IP.
  uint32_t PC = 0;
  /// What the symbol table calls the function containing it, if anything.
  std::string Function;
  /// Where the line table puts it, if there is one.
  std::string File;
  uint32_t Line = 0;
  /// Why the walk stopped here, if it did.
  std::string Error;
};

/// Walk the stack from where \p M currently is.  \p Obj is the executable it
/// was loaded from, which is where the call frame information and the symbols
/// come from.  Stops after \p Limit frames.
std::vector<Frame> backtrace(Machine &M, const llvm::object::ObjectFile &Obj,
                             unsigned Limit = 64);

/// Print a backtrace the way a debugger does.
void printBacktrace(llvm::raw_ostream &OS, const std::vector<Frame> &Frames);

} // namespace c166sim

#endif
