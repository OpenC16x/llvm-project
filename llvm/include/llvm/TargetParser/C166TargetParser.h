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
/// The memories below are named for what they are rather than for where they
/// are, because where they are is usually the core's answer rather than the
/// part's.  A C167 puts its program memory at the bottom of segment 0 or 1 and
/// an XC16x puts it at C0'0000H; both have the internal RAM window at
/// 00'F600H, which one manual calls the dual-port RAM and the other the IRAM.
/// Those addresses live in the linker script, which is per core.
///
/// The extension RAM is the exception, and the ST10 is what made it one: two
/// parts with the same core and the same 256 KByte of Flash put it in
/// different places, so a row can say where its own is.  A zero origin means
/// the core's answer, which is what every part read before the ST10 gives.
struct Part {
  StringRef Name;
  /// The instruction set, which is what -mcpu= names.  It also picks the
  /// shape of the memory map, since that is a property of the core.
  StringRef Core;
  /// The on-chip program memory, Flash or ROM.
  unsigned ProgramSize;
  /// RAM outside the internal RAM window: the XC16x's data SRAM at 00'C000H,
  /// a C167's extension RAM.  Zero on the derivatives that have none.
  unsigned XRAMSize;
  /// Where that RAM starts, or zero to take the core's answer.  This is the
  /// one memory whose address is not the core's, and the ST10 is what made it
  /// necessary: an ST10F269 has 10 KByte of it at 00'C000H where a C167 has 2
  /// KByte at 00'E000H, and both are ST10s to -mcpu=.
  unsigned XRAMOrigin;
  /// A second extension RAM, in its own right rather than adjoining the first.
  /// An ST10F272 puts 8 or 16 KByte at 09'0000H, which no data page pointer
  /// this tree programs reaches, so it is a far memory and a region of its
  /// own.  Zero on every part that has one extension RAM or none.
  unsigned XRAM2Size;
  /// Where that second RAM starts.  Meaningless, and zero, when XRAM2Size is.
  unsigned XRAM2Origin;
  /// The internal RAM at 00'F600H, which every part in the family has there.
  unsigned IRAMSize;
  /// The program SRAM at E0'0000H, which no near address reaches.  XC16x
  /// only; zero elsewhere.
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
