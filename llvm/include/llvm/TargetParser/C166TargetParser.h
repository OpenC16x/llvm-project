//===-- C166TargetParser.h - Parse C166 part names --------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Looking up what memory a C166 part has, so that a program can be linked for
// it without anyone editing a memory map by hand.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TARGETPARSER_C166TARGETPARSER_H
#define LLVM_TARGETPARSER_C166TARGETPARSER_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Compiler.h"

namespace llvm {
namespace C166 {

/// One part, as a derivative table gives it.  The sizes are in bytes and are
/// the whole of each memory; where a program is put inside them is the linker
/// script's business rather than this table's.
struct Part {
  StringRef Name;
  /// The instruction set, which is what -mcpu= names.
  StringRef Core;
  /// The on-chip program memory at C0'0000H.
  unsigned ProgramSize;
  /// The data SRAM at 00'C000H.  Zero on the derivatives that have none.
  unsigned DSRAMSize;
  /// The dual-port RAM at 00'F600H.
  unsigned DPRAMSize;
  /// The program SRAM at E0'0000H, which no near address reaches.
  unsigned PSRAMSize;
  /// Whether the program memory is mask ROM rather than Flash.  It is linked
  /// the same way either way; what changes is whether a program can write it.
  bool IsROM;
};

/// Every part the table knows, in the order it lists them.
LLVM_ABI ArrayRef<Part> getAllParts();

/// The part called \p Name, or nullptr.  The comparison is case insensitive,
/// because a part number is written both ways on the same data sheet.
LLVM_ABI const Part *getPart(StringRef Name);

} // namespace C166
} // namespace llvm

#endif // LLVM_TARGETPARSER_C166TARGETPARSER_H
