//===-- C166TargetParser.cpp - Parse C166 part names ----------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/TargetParser/C166TargetParser.h"

using namespace llvm;

static constexpr C166::Part Parts[] = {
#define C166_PART_AT(NAME, CORE, PROGRAM, XRAM, XRAMORG, XRAM2, XRAM2ORG,      \
                     IRAM, PSRAM, ROMMED)                                      \
  {NAME, CORE, PROGRAM, XRAM, XRAMORG, XRAM2, XRAM2ORG, IRAM, PSRAM, ROMMED},
#include "llvm/TargetParser/C166TargetParser.def"
};

ArrayRef<C166::Part> C166::getAllParts() { return Parts; }

const C166::Part *C166::getPart(StringRef Name) {
  for (const Part &P : Parts)
    if (P.Name.equals_insensitive(Name))
      return &P;
  return nullptr;
}
