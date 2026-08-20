//===-- C166InstrFormats.td - C166 Instruction Formats -----*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// C166 instructions come in a 2 byte "short" and a 4 byte "long" flavour.  The
// backend currently only emits assembly, so the formats below carry the
// instruction size but no encoding.
//
//===----------------------------------------------------------------------===//

class C166Inst<dag outs, dag ins, int size, string asmstr, list<dag> pattern>
    : Instruction {
  let Namespace = "C166";

  dag OutOperandList = outs;
  dag InOperandList = ins;

  let AsmString = asmstr;
  let Pattern = pattern;
  let Size = size;
}

// Two byte instruction: register/register, register/#data3, register/#data4,
// register indirect, ...
class InstS<dag outs, dag ins, string asmstr, list<dag> pattern>
    : C166Inst<outs, ins, 2, asmstr, pattern>;

// Four byte instruction: anything carrying a 16 bit immediate, displacement or
// address.
class InstL<dag outs, dag ins, string asmstr, list<dag> pattern>
    : C166Inst<outs, ins, 4, asmstr, pattern>;

// A pseudo instruction that is expanded before or during assembly printing.
class Pseudo<dag outs, dag ins, string asmstr, list<dag> pattern>
    : C166Inst<outs, ins, 0, asmstr, pattern> {
  let isPseudo = 1;
  let isCodeGenOnly = 1;
}
