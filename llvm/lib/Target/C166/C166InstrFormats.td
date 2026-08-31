//===-- C166InstrFormats.td - C166 Instruction Formats -----*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// C166 instructions are either one or two 16 bit words.  Byte 0 always holds
// the opcode and the remaining bytes hold the operands, so the encoding is
// simply the 2 or 4 bytes of Inst emitted least significant byte first.
//
// The C166 Family Instruction Set Manual writes an encoding as a sequence of
// bytes; the notation used there maps onto Inst as follows.
//
//   listing     meaning                                bits
//   ----------  -------------------------------------  ------------------
//   00H..FFH    opcode byte                            Inst{7-0}
//   n           4 bit GPR number (Rwn / Rbn)           usually Inst{15-12}
//   m           4 bit GPR number (Rwm / Rbm)           usually Inst{11-8}
//   c           4 bit condition code                   Inst{15-12}
//   #           4 bit immediate (#data4)               Inst{15-12}
//   :..ii       2 bit GPR number (Rwi), i.e. R0-R3     Inst{9-8}
//   :.###       3 bit immediate (#data3)               Inst{10-8}
//   RR          8 bit register address; a GPR Rn is    Inst{15-8}
//               addressed as F0H + n
//   ## ##       16 bit immediate, low byte first       Inst{31-16}
//   MM MM       16 bit address, low byte first         Inst{31-16}
//   rr          8 bit signed word offset (rel)         Inst{15-8}
//
// Note that a few instructions deviate from the usual nibble order - MOVBZ and
// MOVBS encode their operands as "mn", and DIV repeats the register number in
// both nibbles - so the layout is spelled out per instruction rather than
// inherited from a shared format class.
//
//===----------------------------------------------------------------------===//

class C166Inst<dag outs, dag ins, int size, string asmstr, list<dag> pattern>
    : Instruction {
  field bits<32> Inst;

  // Bits that the disassembler must not reject when they hold an unexpected
  // value.  Nothing uses this yet.
  field bits<32> SoftFail = 0;

  let Namespace = "C166";

  dag OutOperandList = outs;
  dag InOperandList = ins;

  let AsmString = asmstr;
  let Pattern = pattern;
  let Size = size;

  // Whether the last operand is a coprocessor repeat count.  It cannot be
  // told from the operand alone - an immediate in the last position is
  // ordinary elsewhere - and the printer has to know, because the count is
  // written in front of the mnemonic rather than where its operand sits.
  bit IsCoRepeat = 0;
  let TSFlags{0} = IsCoRepeat;

  // Whether the first operand is the coprocessor shifter's immediate count.
  // Its range is checked after the match rather than by its operand class,
  // because three forms of every shift share a mnemonic and the matcher
  // reports whichever candidate it tried last - so a count out of range was
  // being blamed on the pointer form, which is not what was written.
  bit IsCoShiftImm = 0;
  let TSFlags{1} = IsCoShiftImm;
}

// Two byte instruction: register/register, register/#data4, register
// indirect, ...
class InstS<bits<8> opcode, dag outs, dag ins, string asmstr,
            list<dag> pattern>
    : C166Inst<outs, ins, 2, asmstr, pattern> {
  let Inst{7-0} = opcode;
  let Inst{31-16} = 0;
}

// Four byte instruction: anything carrying a 16 bit immediate, displacement or
// address.
class InstL<bits<8> opcode, dag outs, dag ins, string asmstr,
            list<dag> pattern>
    : C166Inst<outs, ins, 4, asmstr, pattern> {
  let Inst{7-0} = opcode;
}

// A pseudo instruction that is expanded before assembly printing and therefore
// never reaches the encoder.
class Pseudo<dag outs, dag ins, string asmstr, list<dag> pattern>
    : C166Inst<outs, ins, 0, asmstr, pattern> {
  let Inst{31-0} = 0;
  let isPseudo = 1;
  let isCodeGenOnly = 1;
}
