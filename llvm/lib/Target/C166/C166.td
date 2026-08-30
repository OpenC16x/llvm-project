//===-- C166.td - Describe the C166 Target Machine ---------*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
// This is the top level entry point for the Infineon C166 target.
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
// Target-independent interfaces
//===----------------------------------------------------------------------===//

include "llvm/Target/Target.td"

//===----------------------------------------------------------------------===//
// Subtarget Features
//===----------------------------------------------------------------------===//

def FeatureExtInstr
    : SubtargetFeature<"ext-instr", "HasExtInstr", "true",
                       "Enable the EXTR/EXTP/EXTS addressing extensions">;

// ATOMIC and the EXTend instructions arrived with the second generation of the
// family.  The first generation - the SAB 80C166 and 83C166 - does not decode
// them, so a program built for one of those parts has to reach a far object
// some other way and make a read-modify-write indivisible some other way.
// Both of those are what FeatureExtInstr being absent means.

def FeatureMAC
    : SubtargetFeature<"mac", "HasMAC", "true",
                       "Enable the C166S v2 multiply-accumulate unit">;

//===----------------------------------------------------------------------===//
// C166 supported processors
//===----------------------------------------------------------------------===//

class Proc<string Name, list<SubtargetFeature> Features>
    : Processor<Name, NoItineraries, Features>;

// "generic" is the second generation core rather than the oldest one the
// family ever had.  Every part still sold, every part -mmcu= knows, and the
// linker script and startup code in this tree are all second generation; a
// default that could not reach a far object would refuse to build its own
// runtime.  So the first generation is what -mcpu=c166 asks for by name, which
// is what naming a part that old ought to mean.
def : Proc<"generic", [FeatureExtInstr]>;
def : Proc<"c166",    []>;
def : Proc<"c167",    [FeatureExtInstr]>;
def : Proc<"st10",    [FeatureExtInstr]>;
def : Proc<"xc16x",   [FeatureExtInstr, FeatureMAC]>;

//===----------------------------------------------------------------------===//
// Register File Description
//===----------------------------------------------------------------------===//

include "C166RegisterInfo.td"

//===----------------------------------------------------------------------===//
// Calling Convention Description
//===----------------------------------------------------------------------===//

include "C166CallingConv.td"

//===----------------------------------------------------------------------===//
// Instruction Descriptions
//===----------------------------------------------------------------------===//

include "C166InstrInfo.td"
include "C166InstrMAC.td"

defm : RemapAllTargetPseudoPointerOperands<GR16>;

def C166InstrInfo : InstrInfo;

//===----------------------------------------------------------------------===//
// Assembly Printer
//===----------------------------------------------------------------------===//

def C166AsmWriter : AsmWriter {
  string AsmWriterClassName = "InstPrinter";
}

//===----------------------------------------------------------------------===//
// Assembly Parser
//===----------------------------------------------------------------------===//

def C166AsmParser : AsmParser {
  let ShouldEmitMatchRegisterName = 1;
}

def C166AsmParserVariant : AsmParserVariant {
  // '#' introduces an immediate, not a comment.
  let CommentDelimiter = ";";
  let RegisterPrefix = "";
}

//===----------------------------------------------------------------------===//
// Target Declaration
//===----------------------------------------------------------------------===//

def C166 : Target {
  let InstructionSet = C166InstrInfo;
  let AssemblyWriters = [C166AsmWriter];
  let AssemblyParsers = [C166AsmParser];
  let AssemblyParserVariants = [C166AsmParserVariant];
}
