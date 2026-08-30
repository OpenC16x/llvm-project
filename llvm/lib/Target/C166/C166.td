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
                       "Enable the multiply-accumulate unit">;

// One feature and not two, which is worth saying because the ST10's DSP looks
// at first like a second unit.  It is not: the ST10 Family Programming Manual
// (PM0036) documents the same unit this backend already describes.  Checked
// mechanically rather than by eye - all 53 coprocessor mnemonics carry the same
// function code in both, and CoSTORE's six CoReg codes are the same six.  The
// one disagreement is CoMACMR, which that manual prints as F9 for both the
// plain form and the rounding one; every other pair there differs in the low
// bit, so F8/F9 is the reading that fits and is what this file has.
//
// Which parts have it is a property of the part rather than of the family -
// PM0036 says to "refer to device datasheets for information about which ST10
// devices include the MAC" - so it is a feature to be asked for rather than
// something a -mcpu= implies: "-mcpu=st10 -mattr=+mac".
//
// What that manual does not settle is the peripheral map below.  It documents
// an instruction set, not where an ST10 keeps its special function registers,
// so -mcpu=st10 still gets the shared set alone.

// Which special function registers exist, and under which names.  The short
// address a register carries is its encoding, and the same short address names
// different registers on different derivatives - so this is a property of the
// part rather than of the family and has to be chosen.  What every map here
// agrees about is always available; these two say what to add to it.
//
// A part whose map has not been read is better served by the shared set alone
// than by another part's, so neither of these is a default.
def FeatureSFRXC164
    : SubtargetFeature<"sfr-xc164", "HasSFRXC164", "true",
                       "The XC164CM's special function registers">;

def FeatureSFRC167
    : SubtargetFeature<"sfr-c167", "HasSFRC167", "true",
                       "The C167's special function registers">;

//===----------------------------------------------------------------------===//
// C166 supported processors
//===----------------------------------------------------------------------===//

class Proc<string Name, list<SubtargetFeature> Features>
    : Processor<Name, NoItineraries, Features>;

// Only two of these name a peripheral map, because only two have been read.
// "st10" is a C167 derivative and very likely shares its map, but likely is
// not the standard the rest of this file is held to, so it gets the shared set
// until somebody reads an ST10 user manual.  The ST10 Family Programming
// Manual has been read - see FeatureMAC above - and it is the wrong book for
// this: it gives the instruction set and not the register addresses.  "c166"
// is the first generation, whose peripherals are its own.
//
// "generic" is the second generation core rather than the oldest one the
// family ever had.  Every part still sold, every part -mmcu= knows, and the
// linker script and startup code in this tree are all second generation; a
// default that could not reach a far object would refuse to build its own
// runtime.  So the first generation is what -mcpu=c166 asks for by name, which
// is what naming a part that old ought to mean.
def : Proc<"generic", [FeatureExtInstr]>;
def : Proc<"c166",    []>;
def : Proc<"c167",    [FeatureExtInstr, FeatureSFRC167]>;
def : Proc<"st10",    [FeatureExtInstr]>;
def : Proc<"xc16x",   [FeatureExtInstr, FeatureMAC, FeatureSFRXC164]>;

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
