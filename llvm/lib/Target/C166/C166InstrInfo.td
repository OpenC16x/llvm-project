//===-- C166InstrInfo.td - C166 Instruction defs -----------*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file describes the C166 instructions in TableGen format.
//
//===----------------------------------------------------------------------===//

include "C166InstrFormats.td"

//===----------------------------------------------------------------------===//
// Type Profiles
//===----------------------------------------------------------------------===//

def SDT_C166Call         : SDTypeProfile<0, -1, [SDTCisVT<0, iPTR>]>;
// A far call names the callee's segment and its offset within that segment
// separately, because that is what CALLS takes.
def SDT_C166CallSeg      : SDTypeProfile<0, -1, [SDTCisVT<0, i16>,
                                                 SDTCisVT<1, i16>]>;
def SDT_C166CallSeqStart : SDCallSeqStart<[SDTCisVT<0, i16>, SDTCisVT<1, i16>]>;
def SDT_C166CallSeqEnd   : SDCallSeqEnd<[SDTCisVT<0, i16>, SDTCisVT<1, i16>]>;
def SDT_C166Wrapper      : SDTypeProfile<1, 1, [SDTCisSameAs<0, 1>,
                                                SDTCisPtrTy<0>]>;
// A compare and its branch or select are kept together in a single node: on
// C166 almost every instruction - MOV included - updates the condition flags,
// so nothing may be scheduled between the comparison and its use.
def SDT_C166BrCC         : SDTypeProfile<0, 4, [SDTCisVT<0, OtherVT>,
                                                SDTCisSameAs<1, 2>,
                                                SDTCisVT<3, i8>]>;
def SDT_C166SelectCC     : SDTypeProfile<1, 5, [SDTCisSameAs<0, 1>,
                                                SDTCisSameAs<1, 2>,
                                                SDTCisSameAs<3, 4>,
                                                SDTCisVT<5, i8>]>;
// A far access carries the two halves of the 24 bit address separately: the
// 16 bit offset that the memory instruction itself uses, and the segment that
// the EXTS in front of it names.
def SDT_C166FarLoad      : SDTypeProfile<1, 2, [SDTCisInt<0>,
                                                SDTCisVT<1, i16>,
                                                SDTCisVT<2, i16>]>;
def SDT_C166FarStore     : SDTypeProfile<0, 3, [SDTCisInt<0>,
                                                SDTCisVT<1, i16>,
                                                SDTCisVT<2, i16>]>;

//===----------------------------------------------------------------------===//
// C166 Specific Node Definitions
//===----------------------------------------------------------------------===//

def C166retglue  : SDNode<"C166ISD::RET_GLUE", SDTNone,
                          [SDNPHasChain, SDNPOptInGlue, SDNPVariadic]>;
def C166retiglue : SDNode<"C166ISD::RETI_GLUE", SDTNone,
                          [SDNPHasChain, SDNPOptInGlue, SDNPVariadic]>;
def C166retsglue : SDNode<"C166ISD::RETS_GLUE", SDTNone,
                          [SDNPHasChain, SDNPOptInGlue, SDNPVariadic]>;
def C166call     : SDNode<"C166ISD::CALL", SDT_C166Call,
                          [SDNPHasChain, SDNPOutGlue, SDNPOptInGlue,
                           SDNPVariadic]>;
def C166callseg  : SDNode<"C166ISD::CALL_SEG", SDT_C166CallSeg,
                          [SDNPHasChain, SDNPOutGlue, SDNPOptInGlue,
                           SDNPVariadic]>;
def C166callseq_start : SDNode<"ISD::CALLSEQ_START", SDT_C166CallSeqStart,
                               [SDNPHasChain, SDNPOutGlue]>;
def C166callseq_end   : SDNode<"ISD::CALLSEQ_END", SDT_C166CallSeqEnd,
                               [SDNPHasChain, SDNPOptInGlue, SDNPOutGlue]>;
def C166Wrapper  : SDNode<"C166ISD::Wrapper", SDT_C166Wrapper>;
def C166brcc     : SDNode<"C166ISD::BR_CC", SDT_C166BrCC, [SDNPHasChain]>;
def C166selectcc : SDNode<"C166ISD::SELECT_CC", SDT_C166SelectCC>;
def C166farload  : SDNode<"C166ISD::FAR_LOAD", SDT_C166FarLoad,
                          [SDNPHasChain, SDNPMayLoad, SDNPMemOperand]>;
def C166farstore : SDNode<"C166ISD::FAR_STORE", SDT_C166FarStore,
                          [SDNPHasChain, SDNPMayStore, SDNPMemOperand]>;
def C166tcret    : SDNode<"C166ISD::TC_RETURN", SDT_C166Call,
                          [SDNPHasChain, SDNPOptInGlue, SDNPVariadic]>;
def C166tcretseg : SDNode<"C166ISD::TC_RETURN_SEG", SDT_C166CallSeg,
                          [SDNPHasChain, SDNPOptInGlue, SDNPVariadic]>;

//===----------------------------------------------------------------------===//
// Operand Definitions
//===----------------------------------------------------------------------===//

// Operand classes for the assembly parser.  The printed syntax is what tells
// the three flavours of 16 bit operand apart: an immediate carries a '#', an
// address does not, and a memory reference is bracketed.
def CCAsmOperand : AsmOperandClass {
  let Name = "CondCode";
  let RenderMethod = "addImmOperands";
}

def AddrAsmOperand : AsmOperandClass {
  let Name = "Addr";
  let RenderMethod = "addImmOperands";
}

def MemRIAsmOperand : AsmOperandClass {
  let Name = "MemRI";
  let RenderMethod = "addMemRIOperands";
}

def MemRAsmOperand : AsmOperandClass {
  let Name = "MemR";
  let RenderMethod = "addMemROperands";
}

def MemRPostIncAsmOperand : AsmOperandClass {
  let Name = "MemRPostInc";
  let RenderMethod = "addMemROperands";
  let DiagnosticString = "expected [Rw+]";
}

// Immediates are range checked so that an out of range value is rejected
// rather than quietly truncated to the width of its field.  A symbol
// reference is always accepted: the relocation sorts it out later.
//
// The three widths form a chain, narrowest last, which is what makes the
// assembler prefer the two byte encoding for a constant that fits in four
// bits: that is what the compiler emits, so it is what a disassembly has to
// assemble back to.  It has to be a chain rather than a list, because only the
// first superclass counts towards the hierarchy the matcher ranks by.
def Data16AsmOperand : AsmOperandClass {
  let Name = "Data16";
  let RenderMethod = "addImmOperands";
  let DiagnosticString = "expected a 16 bit immediate";
}

def Data8AsmOperand : AsmOperandClass {
  let Name = "Data8";
  let RenderMethod = "addImmOperands";
  let SuperClasses = [Data16AsmOperand];
  let DiagnosticString = "expected an 8 bit immediate";
}

def Imm4AsmOperand : AsmOperandClass {
  let Name = "Imm4";
  let RenderMethod = "addImmOperands";
  let SuperClasses = [Data8AsmOperand];
  let DiagnosticString = "expected an immediate in the range [0, 15]";
}

def Imm3AsmOperand : AsmOperandClass {
  let Name = "Imm3";
  let RenderMethod = "addImmOperands";
  let SuperClasses = [Imm4AsmOperand];
  let DiagnosticString = "expected an immediate in the range [0, 7]";
}

def Irang2AsmOperand : AsmOperandClass {
  let Name = "Irang2";
  let RenderMethod = "addImmOperands";
  let DiagnosticString = "expected an instruction range in [1, 4]";
}

def CoPtrAsmOperand : AsmOperandClass {
  let Name = "CoPtr";
  let RenderMethod = "addCoPtrOperands";
  let DiagnosticString = "expected a pointer such as [r2], [r2+] or [r2+qr0]";
}

def CoIdxAsmOperand : AsmOperandClass {
  let Name = "CoIdx";
  let RenderMethod = "addCoIdxOperands";
  let DiagnosticString = "expected [idx0] or [idx1], with an optional step";
}

def Imm5AsmOperand : AsmOperandClass {
  let Name = "Imm5";
  let RenderMethod = "addImmOperands";
  let DiagnosticString = "expected an immediate in the range [0, 31]";
}

def Trap7AsmOperand : AsmOperandClass {
  let Name = "Trap7";
  let RenderMethod = "addImmOperands";
  let DiagnosticString = "expected a trap number in [0, 127]";
}

def Seg8AsmOperand : AsmOperandClass {
  let Name = "Seg8";
  let RenderMethod = "addImmOperands";
  let DiagnosticString = "expected an 8 bit segment number";
}

def BitOffAsmOperand : AsmOperandClass {
  let Name = "BitOff";
  let RenderMethod = "addImmOperands";
  let DiagnosticString = "expected a bit-addressable word";
}

def BitAddrAsmOperand : AsmOperandClass {
  let Name = "BitAddr";
  let RenderMethod = "addBitAddrOperands";
  let DiagnosticString = "expected a bit address of the form <word>.<bit>";
}

def Mask8AsmOperand : AsmOperandClass {
  let Name = "Mask8";
  let RenderMethod = "addImmOperands";
  let DiagnosticString = "expected an 8 bit mask";
}

def Pag10AsmOperand : AsmOperandClass {
  let Name = "Pag10";
  let RenderMethod = "addImmOperands";
  let DiagnosticString = "expected a 10 bit page number";
}

// The 8 bit "reg" field, which addresses a general purpose register as F0H + n
// and a special function register by its short address.  Which kind of general
// purpose register F0H + n means depends on the instruction, so there are two.
def reg8 : RegisterOperand<GR16orSFR> {
  let EncoderMethod = "getReg8OpValue";
  let DecoderMethod = "decodeReg8Operand";
}

def reg8b : RegisterOperand<GR8orSFR> {
  let EncoderMethod = "getReg8bOpValue";
  let DecoderMethod = "decodeReg8bOperand";
}

// The 8 bit "bitoff" word address of the bit-addressable space, and a bit
// address within it.  A bit address is printed <word>.<bit> and carried as two
// operands; the encoder packs them as (bitpos << 8) | bitoff so that one value
// can feed both instruction fields.
def bitoff : Operand<i16> {
  let PrintMethod = "printBitOffOperand";
  let DecoderMethod = "decodeBitOffOperand";
  let ParserMatchClass = BitOffAsmOperand;
}

// The bit position on its own, for the register forms below: it names which
// bit of the word rather than a value to operate with, so it is written bare.
def bitpos : Operand<i16> {
  let PrintMethod = "printBitPosOperand";
}

def bitaddr : Operand<i16> {
  let PrintMethod = "printBitAddrOperand";
  let MIOperandInfo = (ops i16imm, i16imm);
  let EncoderMethod = "getBitAddrOpValue";
  let DecoderMethod = "decodeBitAddrOperand";
  let ParserMatchClass = BitAddrAsmOperand;
}

// The target of a relative branch in a two byte instruction, where the
// displacement sits one byte in rather than two, so it needs its own encoder
// and its own fixup.
def reltarget8 : Operand<OtherVT> {
  let PrintMethod = "printRelTargetOperand";
  let EncoderMethod = "getShortRel8OpValue";
  let ParserMatchClass = AddrAsmOperand;
  let OperandType = "OPERAND_PCREL";
}

// The same in a four byte instruction: an 8 bit signed count of words from the
// instruction that follows.
def reltarget : Operand<OtherVT> {
  let PrintMethod = "printRelTargetOperand";
  let EncoderMethod = "getRel8OpValue";
  let ParserMatchClass = AddrAsmOperand;
  // Marking it PC relative is what gets the instruction's own address handed
  // to the print method, so a disassembly can name the target rather than the
  // distance to it.
  let OperandType = "OPERAND_PCREL";
}

def mask8 : Operand<i8> {
  let ParserMatchClass = Mask8AsmOperand;
}

// A condition code, printed as cc_XX and encoded in a 4 bit field.
def cc : Operand<i8> {
  let PrintMethod = "printCCOperand";
  let DecoderMethod = "decodeCondCode";
  let ParserMatchClass = CCAsmOperand;
}

// [Rw] / [Rw + #data16] addressing.  The encoder packs the two halves as
// (disp << 4) | base so that a single value can feed both instruction fields.
def memri : Operand<i16> {
  let PrintMethod = "printMemRIOperand";
  let MIOperandInfo = (ops GR16, i16imm);
  let EncoderMethod = "getMemRIOpValue";
  let DecoderMethod = "decodeMemRIOperand";
  let ParserMatchClass = MemRIAsmOperand;
}

// [Rw] on its own, which is a two byte instruction rather than the four byte
// [Rw + #data16] with nothing added.
def memr : Operand<i16> {
  let PrintMethod = "printMemROperand";
  let MIOperandInfo = (ops GR16);
  let DecoderMethod = "decodeMemROperand";
  let ParserMatchClass = MemRAsmOperand;
}

// [Rw+], which steps the pointer past what it just read by the width of the
// access: two for a word and one for a byte.
def memrpi : Operand<i16> {
  let PrintMethod = "printMemRPostIncOperand";
  let MIOperandInfo = (ops GR16);
  let DecoderMethod = "decodeMemROperand";
  let ParserMatchClass = MemRPostIncAsmOperand;
}

// A plain 16 bit data address ("mem" in the manual).
def addr16 : Operand<i16> {
  let PrintMethod = "printAddr16Operand";
  let EncoderMethod = "getAddr16OpValue";
  let ParserMatchClass = AddrAsmOperand;
}

// Branch and call destinations ("caddr") are printed bare (no '#' prefix).
//
// These are the near forms, which take their segment from CSP and so can only
// reach the segment they are in.  Nothing in the instruction says which segment
// that is, so the relocation does: it is a kind of its own rather than the
// plain 16 bit one, which is what lets the linker refuse a target that is
// somewhere else.  See C166FixupKinds.h.
def brtarget : Operand<OtherVT> {
  let PrintMethod = "printBrTargetOperand";
  let EncoderMethod = "getCAddrOpValue";
  let ParserMatchClass = AddrAsmOperand;
}

def calltarget : Operand<i16> {
  let PrintMethod = "printCallTargetOperand";
  let EncoderMethod = "getCAddrOpValue";
  let ParserMatchClass = AddrAsmOperand;
}

// The offset half of a far branch or call, whose segment is the jseg8 beside
// it.  Leaving the segment is the whole point of those, so this is the plain
// 16 bit relocation and there is nothing to check.
def farbrtarget : Operand<OtherVT> {
  let PrintMethod = "printBrTargetOperand";
  let EncoderMethod = "getAddr16OpValue";
  let ParserMatchClass = AddrAsmOperand;
}

def farcalltarget : Operand<i16> {
  let PrintMethod = "printCallTargetOperand";
  let EncoderMethod = "getAddr16OpValue";
  let ParserMatchClass = AddrAsmOperand;
}

// 16 and 8 bit immediates (#data16 / #data8).  These get their own operand
// classes so that the encoder can install a relocation when the value turns
// out to be a symbol reference.
def data16 : Operand<i16> {
  let EncoderMethod = "getData16OpValue";
  let ParserMatchClass = Data16AsmOperand;
}

def data8 : Operand<i8> {
  let EncoderMethod = "getData8OpValue";
  let ParserMatchClass = Data8AsmOperand;
}

// The scope of an ATOMIC or EXTend sequence, written as 1 to 4 instructions
// and encoded as 0 to 3.  The written value is what MCOperand carries.
def irang2 : Operand<i8> {
  let EncoderMethod = "getIrang2OpValue";
  let DecoderMethod = "decodeIrang2Operand";
  let ParserMatchClass = Irang2AsmOperand;
}

// The MAC unit's operand forms.  A pointer is two machine operands, the
// register and what the instruction does to it afterwards, because the two end
// up in different places in the encoding: the register number in the second
// byte and the update in the fourth.  The encoder packs them into one value so
// that the instruction can take the halves it needs.
def coptr : Operand<i16> {
  let MIOperandInfo = (ops GR16, i32imm);
  let EncoderMethod = "getCoPtrOpValue";
  let PrintMethod = "printCoPtrOperand";
  let DecoderMethod = "decodeCoPtrOperand";
  let ParserMatchClass = CoPtrAsmOperand;
}

// The same for IDX0 and IDX1, which the instruction encodes as one nibble:
// which of the two, and what happens to it.
def coidx : Operand<i16> {
  let MIOperandInfo = (ops CoIDX, i32imm);
  let EncoderMethod = "getCoIdxOpValue";
  let PrintMethod = "printCoIdxOperand";
  let DecoderMethod = "decodeCoIdxOperand";
  let ParserMatchClass = CoIdxAsmOperand;
}

// A MAC register named the way CoSTORE names it.
def coreg : RegisterOperand<CoREG> {
  let EncoderMethod = "getCoRegOpValue";
  let DecoderMethod = "decodeCoRegOperand";
}

// The shift count of the MAC unit's shifts.
def imm5 : Operand<i8> {
  let ParserMatchClass = Imm5AsmOperand;
}

// A trap number.  It is written as the trap number itself; where it sits in
// the instruction is bits 7 to 1 of the second byte, which the encoding below
// does rather than an encoder method.
def trap7 : Operand<i8> {
  let ParserMatchClass = Trap7AsmOperand;
}

// An 8 bit segment number (#seg8) and a 10 bit page number (#pag10).
// An 8 bit segment number.  The EXTend instructions keep theirs in the second
// word of the instruction; JMPS and CALLS put theirs in the second byte of the
// first word, so the two need their fixups at different offsets.
def seg8 : Operand<i8> {
  let EncoderMethod = "getSeg8OpValue";
  let ParserMatchClass = Seg8AsmOperand;
}

def jseg8 : Operand<i16> {
  let EncoderMethod = "getJumpSeg8OpValue";
  let ParserMatchClass = Seg8AsmOperand;
}

// A 10 bit page number, in the low ten bits of the second word.
def pag10 : Operand<i16> {
  let EncoderMethod = "getPag10OpValue";
  let ParserMatchClass = Pag10AsmOperand;
}

// The arithmetic instructions have a two byte form taking #data3, which is
// zero extended like every short constant and so spans 0 to 7.
def imm3 : Operand<i16>, ImmLeaf<i16, [{
  return Imm >= 0 && Imm < 8;
}]> {
  let ParserMatchClass = Imm3AsmOperand;
}

def imm3b : Operand<i8>, ImmLeaf<i8, [{
  return Imm >= 0 && Imm < 8;
}]> {
  let ParserMatchClass = Imm3AsmOperand;
}

// A short constant is zero extended, so #data4 spans 0 to 15 (manual 6.5).
def imm4b : Operand<i8>, ImmLeaf<i8, [{
  return Imm >= 0 && Imm < 16;
}]> {
  let ParserMatchClass = Imm4AsmOperand;
}

// Shift and rotate counts are #data4.
def imm4 : Operand<i16>, ImmLeaf<i16, [{
  return Imm >= 0 && Imm < 16;
}]> {
  let ParserMatchClass = Imm4AsmOperand;
}

// The mask of an "or" or an "and" that touches exactly one bit, matched as the
// position of that bit so that a bit instruction can name it.  A sixteen bit
// constant arrives here sign extended - bit 15 alone is -32768, not 32768 -
// so both the test and the transform are done on the truncated value.
def BitPosSet : SDNodeXForm<imm, [{
  uint16_t Mask = static_cast<uint16_t>(N->getZExtValue());
  return CurDAG->getTargetConstant(llvm::countr_zero(Mask), SDLoc(N), MVT::i16);
}]>;

def BitPosClr : SDNodeXForm<imm, [{
  uint16_t Mask = static_cast<uint16_t>(~N->getZExtValue());
  return CurDAG->getTargetConstant(llvm::countr_zero(Mask), SDLoc(N), MVT::i16);
}]>;

def bitpos_set : ImmLeaf<i16, [{
  uint16_t Mask = static_cast<uint16_t>(Imm);
  return Mask != 0 && isPowerOf2_32(Mask);
}], BitPosSet>;

def bitpos_clr : ImmLeaf<i16, [{
  uint16_t Mask = static_cast<uint16_t>(~Imm);
  return Mask != 0 && isPowerOf2_32(Mask);
}], BitPosClr>;

//===----------------------------------------------------------------------===//
// Complex Patterns
//===----------------------------------------------------------------------===//

def addrr   : ComplexPattern<iPTR, 1, "SelectAddrR", [], []>;
def addrri  : ComplexPattern<iPTR, 2, "SelectAddrRI", [frameindex], []>;
def addrabs : ComplexPattern<iPTR, 1, "SelectAddrAbs", [], []>;

//===----------------------------------------------------------------------===//
// Pseudo Instructions
//===----------------------------------------------------------------------===//

// ADJCALLSTACKDOWN/UP adjust the ABI stack pointer around calls.
let Defs = [R0, PSW], Uses = [R0] in {
def ADJCALLSTACKDOWN : Pseudo<(outs), (ins i16imm:$amt1, i16imm:$amt2),
                              "#ADJCALLSTACKDOWN $amt1 $amt2",
                              [(C166callseq_start timm:$amt1, timm:$amt2)]>;
def ADJCALLSTACKUP   : Pseudo<(outs), (ins i16imm:$amt1, i16imm:$amt2),
                              "#ADJCALLSTACKUP $amt1 $amt2",
                              [(C166callseq_end timm:$amt1, timm:$amt2)]>;
}

// "Load effective address" of a stack slot.  Expanded by eliminateFrameIndex.
let Defs = [PSW] in
def ADDframe : Pseudo<(outs GR16:$dst), (ins i16imm:$base, i16imm:$offset),
                      "#ADDframe", []>;

// Conditional moves.  The custom inserter turns them into a diamond whose
// head ends in one of the fused compare-and-branch pseudos below.  The
// comparison operands travel with the select so that they cannot be separated
// from the branch that consumes the flags.
class SelectPseudo<RegisterClass VRC, RegisterClass CRC>
    : Pseudo<(outs VRC:$dst),
             (ins VRC:$t, VRC:$f, CRC:$lhs, CRC:$rhs, i8imm:$cond),
             "#Select",
             [(set VRC:$dst, (C166selectcc VRC:$t, VRC:$f, CRC:$lhs,
                                           CRC:$rhs, imm:$cond))]> {
  let usesCustomInserter = 1;
  let Defs = [PSW];
}

// The same again with a constant on the right of the comparison, so that a
// select against one does not have to materialise it into a register first.
class SelectPseudoImm<RegisterClass VRC, RegisterClass CRC, Operand ImmTy,
                      ValueType ImmVT>
    : Pseudo<(outs VRC:$dst),
             (ins VRC:$t, VRC:$f, CRC:$lhs, ImmTy:$rhs, i8imm:$cond),
             "#Select",
             [(set VRC:$dst, (C166selectcc VRC:$t, VRC:$f, CRC:$lhs,
                                           (ImmVT imm:$rhs), imm:$cond))]> {
  let usesCustomInserter = 1;
  let Defs = [PSW];
}

def Select16_16 : SelectPseudo<GR16, GR16>;
def Select16_8  : SelectPseudo<GR16, GR8>;
def Select8_16  : SelectPseudo<GR8, GR16>;
def Select8_8   : SelectPseudo<GR8, GR8>;

def Select16_16i : SelectPseudoImm<GR16, GR16, data16, i16>;
def Select16_8i  : SelectPseudoImm<GR16, GR8, data8, i8>;
def Select8_16i  : SelectPseudoImm<GR8, GR16, data16, i16>;
def Select8_8i   : SelectPseudoImm<GR8, GR8, data8, i8>;

// Far accesses.  The EXTS that names the segment only covers the instruction
// that follows it, so the two have to stay adjacent all the way through the
// register allocator; they are separated by expandPostRAPseudo() once nothing
// can be inserted between them any more.
let mayLoad = 1 in {
def FARLOAD16 : Pseudo<(outs GR16:$dst), (ins GR16:$off, GR16:$seg),
                       "#FARLOAD16",
                       [(set GR16:$dst, (C166farload GR16:$off, GR16:$seg))]>;
def FARLOAD8  : Pseudo<(outs GR8:$dst), (ins GR16:$off, GR16:$seg),
                       "#FARLOAD8",
                       [(set GR8:$dst, (C166farload GR16:$off, GR16:$seg))]>;
}

let mayStore = 1 in {
def FARSTORE16 : Pseudo<(outs), (ins GR16:$src, GR16:$off, GR16:$seg),
                        "#FARSTORE16",
                        [(C166farstore GR16:$src, GR16:$off, GR16:$seg)]>;
def FARSTORE8  : Pseudo<(outs), (ins GR8:$src, GR16:$off, GR16:$seg),
                        "#FARSTORE8",
                        [(C166farstore GR8:$src, GR16:$off, GR16:$seg)]>;
}

// The segment of a far object is settled once the linker has placed it, so
// there is no need to put it in a register first: EXTS takes an immediate.
// The patterns for these are further down, next to the other symbol patterns;
// a segment that is not a symbol simply keeps the register form above.
let mayLoad = 1 in {
def FARLOAD16i : Pseudo<(outs GR16:$dst), (ins GR16:$off, i16imm:$seg),
                        "#FARLOAD16i", []>;
def FARLOAD8i  : Pseudo<(outs GR8:$dst), (ins GR16:$off, i16imm:$seg),
                        "#FARLOAD8i", []>;
}

let mayStore = 1 in {
def FARSTORE16i : Pseudo<(outs), (ins GR16:$src, GR16:$off, i16imm:$seg),
                         "#FARSTORE16i", []>;
def FARSTORE8i  : Pseudo<(outs), (ins GR8:$src, GR16:$off, i16imm:$seg),
                         "#FARSTORE8i", []>;
}

// When the offset is settled at link time too, the access itself can name it:
// EXTS covers a long (mem) address just as it covers an indirect one, so the
// whole address stays out of registers.
let mayLoad = 1 in {
def FARLOAD16a : Pseudo<(outs GR16:$dst), (ins i16imm:$off, i16imm:$seg),
                        "#FARLOAD16a", []>;
def FARLOAD8a  : Pseudo<(outs GR8:$dst), (ins i16imm:$off, i16imm:$seg),
                        "#FARLOAD8a", []>;
}

let mayStore = 1 in {
def FARSTORE16a : Pseudo<(outs), (ins GR16:$src, i16imm:$off, i16imm:$seg),
                         "#FARSTORE16a", []>;
def FARSTORE8a  : Pseudo<(outs), (ins GR8:$src, i16imm:$off, i16imm:$seg),
                         "#FARSTORE8a", []>;
}

// A tail call is a jump to the callee once this function's frame is gone, so
// the callee's RET goes straight back to our caller.  They are pseudos so that
// the prolog/epilog inserter treats them as returns and tears the frame down
// in front of them; expandPostRAPseudo() then turns them into the jumps below.
//
// The jumps need opcodes of their own rather than reusing JMPA and JMPI: their
// target is a symbol, not a basic block, so anything that reads the control
// flow graph must not take them for branches.
let isCall = 1, isTerminator = 1, isReturn = 1, isBarrier = 1,
    Uses = [R0] in {
def TCRETURNa : Pseudo<(outs), (ins calltarget:$dst), "#TCRETURNa", []>;
def TCRETURNi : Pseudo<(outs), (ins GR16:$dst), "#TCRETURNi", []>;
// A far tail call names the target segment, since JMPA would only reach
// inside the one this function happens to have been called into.
def TCRETURNs : Pseudo<(outs), (ins jseg8:$seg, farcalltarget:$dst),
                       "#TCRETURNs", []>;

let isCodeGenOnly = 1 in {
def TAILJMPa : InstL<0xEA, (outs), (ins calltarget:$dst), "jmpa\tcc_UC, $dst",
                     []> {
  bits<16> dst;
  let Inst{15-12} = 0b0000;   // cc_UC
  let Inst{11-8} = 0;
  let Inst{31-16} = dst;
}
def TAILJMPi : InstS<0x9C, (outs), (ins GR16:$dst), "jmpi\tcc_UC, [$dst]",
                     []> {
  bits<4> dst;
  let Inst{15-12} = 0b0000;   // cc_UC
  let Inst{11-8} = dst;
}
def TAILJMPs : InstL<0xFA, (outs), (ins jseg8:$seg, farcalltarget:$dst),
                     "jmps\t$seg, $dst", []> {
  bits<8> seg;
  bits<16> dst;
  let Inst{15-8} = seg;
  let Inst{31-16} = dst;
}
}
}

// Multiply and divide go through the MDL/MDH register pair.  These pseudos are
// expanded by C166InstrInfo::expandPostRAPseudo().
let Defs = [MDL, MDH, MDC, PSW] in {
def MUL16rr   : Pseudo<(outs GR16:$dst), (ins GR16:$src1, GR16:$src2),
                       "#MUL16rr",
                       [(set GR16:$dst, (mul GR16:$src1, GR16:$src2))]>;
def MULHS16rr : Pseudo<(outs GR16:$dst), (ins GR16:$src1, GR16:$src2),
                       "#MULHS16rr",
                       [(set GR16:$dst, (mulhs GR16:$src1, GR16:$src2))]>;
def MULHU16rr : Pseudo<(outs GR16:$dst), (ins GR16:$src1, GR16:$src2),
                       "#MULHU16rr",
                       [(set GR16:$dst, (mulhu GR16:$src1, GR16:$src2))]>;

// One MUL leaves both halves of the product behind, in MDL and MDH, so a
// widening multiply is one instruction and two moves rather than two of each.
// These are what makes that happen: without them mul and mulhs are separate
// nodes and each expands to a MUL of its own.
def SMUL16rrLOHI : Pseudo<(outs GR16:$lo, GR16:$hi),
                          (ins GR16:$src1, GR16:$src2),
                          "#SMUL16rrLOHI",
                          [(set GR16:$lo, GR16:$hi,
                                (smullohi GR16:$src1, GR16:$src2))]>;
def UMUL16rrLOHI : Pseudo<(outs GR16:$lo, GR16:$hi),
                          (ins GR16:$src1, GR16:$src2),
                          "#UMUL16rrLOHI",
                          [(set GR16:$lo, GR16:$hi,
                                (umullohi GR16:$src1, GR16:$src2))]>;
def SDIV16rr  : Pseudo<(outs GR16:$dst), (ins GR16:$src1, GR16:$src2),
                       "#SDIV16rr",
                       [(set GR16:$dst, (sdiv GR16:$src1, GR16:$src2))]>;
def UDIV16rr  : Pseudo<(outs GR16:$dst), (ins GR16:$src1, GR16:$src2),
                       "#UDIV16rr",
                       [(set GR16:$dst, (udiv GR16:$src1, GR16:$src2))]>;
def SREM16rr  : Pseudo<(outs GR16:$dst), (ins GR16:$src1, GR16:$src2),
                       "#SREM16rr",
                       [(set GR16:$dst, (srem GR16:$src1, GR16:$src2))]>;
def UREM16rr  : Pseudo<(outs GR16:$dst), (ins GR16:$src1, GR16:$src2),
                       "#UREM16rr",
                       [(set GR16:$dst, (urem GR16:$src1, GR16:$src2))]>;
}

//===----------------------------------------------------------------------===//
// Move Instructions
//===----------------------------------------------------------------------===//

let isMoveReg = 1, isAsCheapAsAMove = 1, hasSideEffects = 0 in {
// MOV Rwn, Rwm  /  MOVB Rbn, Rbm
def MOV16rr : InstS<0xF0, (outs GR16:$dst), (ins GR16:$src),
                    "mov\t$dst, $src", []> {
  bits<4> dst;
  bits<4> src;
  let Inst{15-12} = dst;
  let Inst{11-8} = src;
}
def MOVB8rr : InstS<0xF1, (outs GR8:$dst), (ins GR8:$src),
                    "movb\t$dst, $src", []> {
  bits<4> dst;
  bits<4> src;
  let Inst{15-12} = dst;
  let Inst{11-8} = src;
}
}

let isMoveImm = 1, isReMaterializable = 1, isAsCheapAsAMove = 1 in {
// MOV Rwn, #data4 / MOVB Rbn, #data4, two bytes with the constant in the high
// nibble ahead of the register.  A short constant is zero extended, so these
// cover 0 to 15.
def MOV16ri4 : InstS<0xE0, (outs GR16:$dst), (ins imm4:$src),
                     "mov\t$dst, $src", [(set GR16:$dst, imm4:$src)]> {
  bits<4> dst;
  bits<4> src;
  let Inst{15-12} = src;
  let Inst{11-8} = dst;
  let AddedComplexity = 10;
}
def MOVB8ri4 : InstS<0xE1, (outs GR8:$dst), (ins imm4b:$src),
                     "movb\t$dst, $src", [(set GR8:$dst, imm4b:$src)]> {
  bits<4> dst;
  bits<4> src;
  let Inst{15-12} = src;
  let Inst{11-8} = dst;
  let AddedComplexity = 10;
}

// MOV reg, #data16  /  MOVB reg, #data8
let isCodeGenOnly = 1 in
def MOV16ri : InstL<0xE6, (outs GR16:$dst), (ins data16:$src),
                    "mov\t$dst, $src", [(set GR16:$dst, imm:$src)]> {
  bits<4> dst;
  bits<16> src;
  let Inst{15-12} = 0b1111;
  let Inst{11-8} = dst;
  let Inst{31-16} = src;
}
let isCodeGenOnly = 1 in
def MOVB8ri : InstL<0xE7, (outs GR8:$dst), (ins data8:$src),
                    "movb\t$dst, $src", [(set GR8:$dst, imm:$src)]> {
  bits<4> dst;
  bits<8> src;
  let Inst{15-12} = 0b1111;
  let Inst{11-8} = dst;
  let Inst{23-16} = src;
  let Inst{31-24} = 0;
}
}

// The two byte [Rw] forms of everything that would otherwise go through the
// four byte [Rw + #data16].  They are preferred by AddedComplexity whenever the
// displacement would have been zero, which in a pointer walking loop is every
// access.  eliminateFrameIndex() switches to them as well when a frame slot
// turns out to sit at offset zero.
let mayLoad = 1 in {
def MOV16rp  : InstS<0xA8, (outs GR16:$dst), (ins memr:$src),
                         "mov\t$dst, $src",
                         [(set GR16:$dst, (load addrr:$src))]> {
  bits<4> dst;
  bits<4> src;
  let Inst{15-12} = dst;
  let Inst{11-8} = src;
  let AddedComplexity = 10;
}
def MOVB8rp  : InstS<0xA9, (outs GR8:$dst), (ins memr:$src),
                         "movb\t$dst, $src",
                         [(set GR8:$dst, (load addrr:$src))]> {
  bits<4> dst;
  bits<4> src;
  let Inst{15-12} = dst;
  let Inst{11-8} = src;
  let AddedComplexity = 10;
}
}

// [Rw+] reads and then steps the pointer, which is what an array walk wants.
// These are selected by hand in C166ISelDAGToDAG.cpp, since the pointer being
// written back makes them two-result instructions that no pattern describes.
let mayLoad = 1, Constraints = "$base = $base_wb" in {
def MOV16rpi  : InstS<0x98, (outs GR16:$dst, GR16:$base_wb),
                      (ins memrpi:$base), "mov\t$dst, $base", []> {
  bits<4> dst;
  bits<4> base;
  let Inst{15-12} = dst;
  let Inst{11-8} = base;
}
def MOVB8rpi  : InstS<0x99, (outs GR8:$dst, GR16:$base_wb),
                      (ins memrpi:$base), "movb\t$dst, $base", []> {
  bits<4> dst;
  bits<4> base;
  let Inst{15-12} = dst;
  let Inst{11-8} = base;
}
}

let mayStore = 1 in {
def MOV16pr : InstS<0xB8, (outs), (ins memr:$dst, GR16:$src),
                          "mov\t$dst, $src",
                          [(store GR16:$src, addrr:$dst)]> {
  bits<4> dst;
  bits<4> src;
  let Inst{15-12} = src;
  let Inst{11-8} = dst;
  let AddedComplexity = 10;
}
def MOVB8pr : InstS<0xB9, (outs), (ins memr:$dst, GR8:$src),
                          "movb\t$dst, $src",
                          [(store GR8:$src, addrr:$dst)]> {
  bits<4> dst;
  bits<4> src;
  let Inst{15-12} = src;
  let Inst{11-8} = dst;
  let AddedComplexity = 10;
}
}

let mayLoad = 1 in {
// MOV Rwn, [Rwm + #data16]
def MOV16rm : InstL<0xD4, (outs GR16:$dst), (ins memri:$src),
                    "mov\t$dst, $src", [(set GR16:$dst, (load addrri:$src))]> {
  bits<4> dst;
  bits<20> src;
  let Inst{15-12} = dst;
  let Inst{11-8} = src{3-0};
  let Inst{31-16} = src{19-4};
}
// MOV reg, mem
let isCodeGenOnly = 1 in
def MOV16ra : InstL<0xF2, (outs GR16:$dst), (ins addr16:$src),
                    "mov\t$dst, $src", [(set GR16:$dst, (load addrabs:$src))]> {
  bits<4> dst;
  bits<16> src;
  let Inst{15-12} = 0b1111;
  let Inst{11-8} = dst;
  let Inst{31-16} = src;
}
// MOVB Rbn, [Rwm + #data16]
def MOVB8rm : InstL<0xF4, (outs GR8:$dst), (ins memri:$src),
                    "movb\t$dst, $src", [(set GR8:$dst, (load addrri:$src))]> {
  bits<4> dst;
  bits<20> src;
  let Inst{15-12} = dst;
  let Inst{11-8} = src{3-0};
  let Inst{31-16} = src{19-4};
}
// MOVB reg, mem
let isCodeGenOnly = 1 in
def MOVB8ra : InstL<0xF3, (outs GR8:$dst), (ins addr16:$src),
                    "movb\t$dst, $src", [(set GR8:$dst, (load addrabs:$src))]> {
  bits<4> dst;
  bits<16> src;
  let Inst{15-12} = 0b1111;
  let Inst{11-8} = dst;
  let Inst{31-16} = src;
}
}

let mayStore = 1 in {
// MOV [Rwm + #data16], Rwn
def MOV16mr : InstL<0xC4, (outs), (ins memri:$dst, GR16:$src),
                    "mov\t$dst, $src", [(store GR16:$src, addrri:$dst)]> {
  bits<20> dst;
  bits<4> src;
  let Inst{15-12} = src;
  let Inst{11-8} = dst{3-0};
  let Inst{31-16} = dst{19-4};
}
// MOV mem, reg
def MOV16ar : InstL<0xF6, (outs), (ins addr16:$dst, GR16:$src),
                    "mov\t$dst, $src", [(store GR16:$src, addrabs:$dst)]> {
  bits<16> dst;
  bits<4> src;
  let Inst{15-12} = 0b1111;
  let Inst{11-8} = src;
  let Inst{31-16} = dst;
}
// MOVB [Rwm + #data16], Rbn
def MOVB8mr : InstL<0xE4, (outs), (ins memri:$dst, GR8:$src),
                    "movb\t$dst, $src", [(store GR8:$src, addrri:$dst)]> {
  bits<20> dst;
  bits<4> src;
  let Inst{15-12} = src;
  let Inst{11-8} = dst{3-0};
  let Inst{31-16} = dst{19-4};
}
// MOVB mem, reg
def MOVB8ar : InstL<0xF7, (outs), (ins addr16:$dst, GR8:$src),
                    "movb\t$dst, $src", [(store GR8:$src, addrabs:$dst)]> {
  bits<16> dst;
  bits<4> src;
  let Inst{15-12} = 0b1111;
  let Inst{11-8} = src;
  let Inst{31-16} = dst;
}
}

// Byte to word conversions.  Note the reversed nibble order: the manual gives
// the format as "mn", so the byte source sits in the high nibble.  Like the
// moves these leave the carry alone, so they do not write PSW as it is
// modelled here.
def MOVBZ16r8 : InstS<0xC0, (outs GR16:$dst), (ins GR8:$src),
                      "movbz\t$dst, $src",
                      [(set GR16:$dst, (zext GR8:$src))]> {
  bits<4> dst;
  bits<4> src;
  let Inst{15-12} = src;
  let Inst{11-8} = dst;
}
def MOVBS16r8 : InstS<0xD0, (outs GR16:$dst), (ins GR8:$src),
                      "movbs\t$dst, $src",
                      [(set GR16:$dst, (sext GR8:$src))]> {
  bits<4> dst;
  bits<4> src;
  let Inst{15-12} = src;
  let Inst{11-8} = dst;
}

//===----------------------------------------------------------------------===//
// Arithmetic and Logical Instructions
//===----------------------------------------------------------------------===//

// C166 ALU instructions are two operand: the destination is also the first
// source.  Supported source flavours are a register, a 16 bit immediate and a
// 16 bit absolute address.  The three opcodes of a group differ only in the
// low nibble (x0 / x6 / x2 for words, x1 / x7 / x3 for bytes).
multiclass ArithW<string mnemonic, bits<8> opRR, bits<8> opRI, bits<8> opRA,
                  SDNode opnode, bit commutable = 0,
                  list<Register> uses = []> {
  let Constraints = "$src1 = $dst", Defs = [PSW], Uses = uses in {
    // The two byte #data3 form, preferred whenever the constant fits.  Its
    // opcode is the register/register one plus 8.
    let AddedComplexity = 10 in
    def 16ri3 : InstS<!add(opRR, 8), (outs GR16:$dst),
                      (ins GR16:$src1, imm3:$src2),
                      !strconcat(mnemonic, "\t$dst, $src2"),
                      [(set GR16:$dst, (opnode GR16:$src1, imm3:$src2))]> {
      bits<4> dst;
      bits<3> src2;
      let Inst{15-12} = dst;
      let Inst{11} = 0;
      let Inst{10-8} = src2;
    }

    let isCommutable = commutable in
    def 16rr : InstS<opRR, (outs GR16:$dst), (ins GR16:$src1, GR16:$src2),
                     !strconcat(mnemonic, "\t$dst, $src2"),
                     [(set GR16:$dst, (opnode GR16:$src1, GR16:$src2))]> {
      bits<4> dst;
      bits<4> src2;
      let Inst{15-12} = dst;
      let Inst{11-8} = src2;
    }
    let isCodeGenOnly = 1 in
    def 16ri : InstL<opRI, (outs GR16:$dst), (ins GR16:$src1, data16:$src2),
                     !strconcat(mnemonic, "\t$dst, $src2"),
                     [(set GR16:$dst, (opnode GR16:$src1, imm:$src2))]> {
      bits<4> dst;
      bits<16> src2;
      let Inst{15-12} = 0b1111;
      let Inst{11-8} = dst;
      let Inst{31-16} = src2;
    }
    let mayLoad = 1, isCodeGenOnly = 1 in
    def 16ra : InstL<opRA, (outs GR16:$dst), (ins GR16:$src1, addr16:$src2),
                     !strconcat(mnemonic, "\t$dst, $src2"),
                     [(set GR16:$dst,
                           (opnode GR16:$src1, (load addrabs:$src2)))]> {
      bits<4> dst;
      bits<16> src2;
      let Inst{15-12} = 0b1111;
      let Inst{11-8} = dst;
      let Inst{31-16} = src2;
    }
  }
}

multiclass ArithB<string mnemonic, bits<8> opRR, bits<8> opRI, bits<8> opRA,
                  SDNode opnode, bit commutable = 0> {
  let Constraints = "$src1 = $dst", Defs = [PSW] in {
    let AddedComplexity = 10 in
    def 8ri3 : InstS<!add(opRR, 8), (outs GR8:$dst),
                     (ins GR8:$src1, imm3b:$src2),
                     !strconcat(mnemonic, "\t$dst, $src2"),
                     [(set GR8:$dst, (opnode GR8:$src1, imm3b:$src2))]> {
      bits<4> dst;
      bits<3> src2;
      let Inst{15-12} = dst;
      let Inst{11} = 0;
      let Inst{10-8} = src2;
    }

    let isCommutable = commutable in
    def 8rr : InstS<opRR, (outs GR8:$dst), (ins GR8:$src1, GR8:$src2),
                    !strconcat(mnemonic, "\t$dst, $src2"),
                    [(set GR8:$dst, (opnode GR8:$src1, GR8:$src2))]> {
      bits<4> dst;
      bits<4> src2;
      let Inst{15-12} = dst;
      let Inst{11-8} = src2;
    }
    let isCodeGenOnly = 1 in
    def 8ri : InstL<opRI, (outs GR8:$dst), (ins GR8:$src1, data8:$src2),
                    !strconcat(mnemonic, "\t$dst, $src2"),
                    [(set GR8:$dst, (opnode GR8:$src1, imm:$src2))]> {
      bits<4> dst;
      bits<8> src2;
      let Inst{15-12} = 0b1111;
      let Inst{11-8} = dst;
      let Inst{23-16} = src2;
      let Inst{31-24} = 0;
    }
    let mayLoad = 1, isCodeGenOnly = 1 in
    def 8ra : InstL<opRA, (outs GR8:$dst), (ins GR8:$src1, addr16:$src2),
                    !strconcat(mnemonic, "\t$dst, $src2"),
                    [(set GR8:$dst, (opnode GR8:$src1, (load addrabs:$src2)))]> {
      bits<4> dst;
      bits<16> src2;
      let Inst{15-12} = 0b1111;
      let Inst{11-8} = dst;
      let Inst{31-16} = src2;
    }
  }
}

// The same two instructions with the whole 8 bit "reg" field, so that a
// special function register can be named where the instruction allows one.
//
// They carry no pattern, and that is the point: reg8's register class holds
// both kinds of register and is not allocatable, so it cannot be what a
// pattern produces without the register allocator being told it may put a
// result in an SFR.  The GPR-only forms above keep the patterns and are marked
// codegen-only, which takes them out of the matcher and the decoder, so
// "add r2, #1234" assembles as one of these and those bytes come back as it.
multiclass ArithWReg<string mnemonic, bits<8> opRI, bits<8> opRA,
                     list<Register> uses = []> {
  let Defs = [PSW], Uses = uses in {
    def 16regi : InstL<opRI, (outs), (ins reg8:$dst, data16:$src),
                       !strconcat(mnemonic, "\t$dst, $src"), []> {
      bits<8> dst;
      bits<16> src;
      let Inst{15-8} = dst;
      let Inst{31-16} = src;
    }
    let mayLoad = 1 in
    def 16rega : InstL<opRA, (outs), (ins reg8:$dst, addr16:$src),
                       !strconcat(mnemonic, "\t$dst, $src"), []> {
      bits<8> dst;
      bits<16> src;
      let Inst{15-8} = dst;
      let Inst{31-16} = src;
    }
  }
}

// The compare and the two loading forms of MOV, widened the same way.  MOV
// mem, reg is deliberately not: with both operands able to be an SFR name,
// "mov mdl, mdh" would match it as well as MOV reg, mem, and the two are
// different encodings of the same thing, so there would be nothing to choose
// between them.
def CMP16regi : InstL<0x46, (outs), (ins reg8:$src1, data16:$src2),
                      "cmp\t$src1, $src2", []> {
  bits<8> src1;
  bits<16> src2;
  let Defs = [PSW];
  let Inst{15-8} = src1;
  let Inst{31-16} = src2;
}

def MOV16regi : InstL<0xE6, (outs), (ins reg8:$dst, data16:$src),
                      "mov\t$dst, $src", []> {
  bits<8> dst;
  bits<16> src;
  let Defs = [PSW];
  let Inst{15-8} = dst;
  let Inst{31-16} = src;
}

let mayLoad = 1 in
def MOV16rega : InstL<0xF2, (outs), (ins reg8:$dst, addr16:$src),
                      "mov\t$dst, $src", []> {
  bits<8> dst;
  bits<16> src;
  let Defs = [PSW];
  let Inst{15-8} = dst;
  let Inst{31-16} = src;
}

// The byte instructions the same way.  They need their own operand because
// F0H + n names a byte register here.
multiclass ArithBReg<string mnemonic, bits<8> opRI, bits<8> opRA> {
  let Defs = [PSW] in {
    def 8regi : InstL<opRI, (outs), (ins reg8b:$dst, data8:$src),
                      !strconcat(mnemonic, "\t$dst, $src"), []> {
      bits<8> dst;
      bits<8> src;
      let Inst{15-8} = dst;
      let Inst{23-16} = src;
      let Inst{31-24} = 0;
    }
    let mayLoad = 1 in
    def 8rega : InstL<opRA, (outs), (ins reg8b:$dst, addr16:$src),
                      !strconcat(mnemonic, "\t$dst, $src"), []> {
      bits<8> dst;
      bits<16> src;
      let Inst{15-8} = dst;
      let Inst{31-16} = src;
    }
  }
}

defm ADDB : ArithBReg<"addb", 0x07, 0x03>;
defm SUBB : ArithBReg<"subb", 0x27, 0x23>;
defm ANDB : ArithBReg<"andb", 0x67, 0x63>;
defm ORB  : ArithBReg<"orb",  0x77, 0x73>;
defm XORB : ArithBReg<"xorb", 0x57, 0x53>;

def CMPB8regi : InstL<0x47, (outs), (ins reg8b:$src1, data8:$src2),
                      "cmpb\t$src1, $src2", []> {
  bits<8> src1;
  bits<8> src2;
  let Defs = [PSW];
  let Inst{15-8} = src1;
  let Inst{23-16} = src2;
  let Inst{31-24} = 0;
}

def MOVB8regi : InstL<0xE7, (outs), (ins reg8b:$dst, data8:$src),
                      "movb\t$dst, $src", []> {
  bits<8> dst;
  bits<8> src;
  let Defs = [PSW];
  let Inst{15-8} = dst;
  let Inst{23-16} = src;
  let Inst{31-24} = 0;
}

let mayLoad = 1 in
def MOVB8rega : InstL<0xF3, (outs), (ins reg8b:$dst, addr16:$src),
                      "movb\t$dst, $src", []> {
  bits<8> dst;
  bits<16> src;
  let Defs = [PSW];
  let Inst{15-8} = dst;
  let Inst{31-16} = src;
}

defm ADD  : ArithWReg<"add",  0x06, 0x02>;
defm SUB  : ArithWReg<"sub",  0x26, 0x22>;
defm AND  : ArithWReg<"and",  0x66, 0x62>;
defm OR   : ArithWReg<"or",   0x76, 0x72>;
defm XOR  : ArithWReg<"xor",  0x56, 0x52>;
defm ADDC : ArithWReg<"addc", 0x16, 0x12, [PSW]>;
defm SUBC : ArithWReg<"subc", 0x36, 0x32, [PSW]>;

defm ADD  : ArithW<"add",  0x00, 0x06, 0x02, add, 1>;
defm SUB  : ArithW<"sub",  0x20, 0x26, 0x22, sub, 0>;
defm AND  : ArithW<"and",  0x60, 0x66, 0x62, and, 1>;
defm OR   : ArithW<"or",   0x70, 0x76, 0x72, or,  1>;
defm XOR  : ArithW<"xor",  0x50, 0x56, 0x52, xor, 1>;

defm ADDB : ArithB<"addb", 0x01, 0x07, 0x03, add, 1>;
defm SUBB : ArithB<"subb", 0x21, 0x27, 0x23, sub, 0>;
defm ANDB : ArithB<"andb", 0x61, 0x67, 0x63, and, 1>;
defm ORB  : ArithB<"orb",  0x71, 0x77, 0x73, or,  1>;
defm XORB : ArithB<"xorb", 0x51, 0x57, 0x53, xor, 1>;

// Add and subtract with carry.  Their opcodes are the plain ones plus 10H,
// and they are what carries a wider addition or subtraction from one word to
// the next: the carry lives in the PSW, which is why these read it as well as
// write it.
defm ADDC : ArithW<"addc", 0x10, 0x16, 0x12, adde, 1, [PSW]>;
defm SUBC : ArithW<"subc", 0x30, 0x36, 0x32, sube, 0, [PSW]>;

// Unary operations, encoded as "n0".
class UnaryInst<bits<8> opcode, string mnemonic, RegisterClass RC,
                SDPatternOperator opnode>
    : InstS<opcode, (outs RC:$dst), (ins RC:$src),
            !strconcat(mnemonic, "\t$dst"),
            [(set RC:$dst, (opnode RC:$src))]> {
  bits<4> dst;
  let Inst{15-12} = dst;
  let Inst{11-8} = 0;
}

let Constraints = "$src = $dst", Defs = [PSW] in {
def CPL16r  : UnaryInst<0x91, "cpl",  GR16, not>;
def NEG16r  : UnaryInst<0x81, "neg",  GR16, ineg>;
def CPLB8r  : UnaryInst<0xB1, "cplb", GR8,  not>;
def NEGB8r  : UnaryInst<0xA1, "negb", GR8,  ineg>;
}

//===----------------------------------------------------------------------===//
// Shift and Rotate Instructions
//===----------------------------------------------------------------------===//

// The immediate form is encoded as "#n": the count occupies the high nibble
// and the register the low one.
multiclass ShiftW<string mnemonic, bits<8> opRR, bits<8> opRI, SDNode opnode> {
  let Constraints = "$src1 = $dst", Defs = [PSW] in {
    def 16rr : InstS<opRR, (outs GR16:$dst), (ins GR16:$src1, GR16:$src2),
                     !strconcat(mnemonic, "\t$dst, $src2"),
                     [(set GR16:$dst, (opnode GR16:$src1, GR16:$src2))]> {
      bits<4> dst;
      bits<4> src2;
      let Inst{15-12} = dst;
      let Inst{11-8} = src2;
    }
    def 16ri : InstS<opRI, (outs GR16:$dst), (ins GR16:$src1, imm4:$src2),
                     !strconcat(mnemonic, "\t$dst, $src2"),
                     [(set GR16:$dst, (opnode GR16:$src1, imm4:$src2))]> {
      bits<4> dst;
      bits<4> src2;
      let Inst{15-12} = src2;
      let Inst{11-8} = dst;
    }
  }
}

defm SHL  : ShiftW<"shl",  0x4C, 0x5C, shl>;
defm SHR  : ShiftW<"shr",  0x6C, 0x7C, srl>;
defm ASHR : ShiftW<"ashr", 0xAC, 0xBC, sra>;
defm ROL  : ShiftW<"rol",  0x0C, 0x1C, rotl>;
defm ROR  : ShiftW<"ror",  0x2C, 0x3C, rotr>;

//===----------------------------------------------------------------------===//
// Comparison Instructions
//===----------------------------------------------------------------------===//

// The compares are only ever emitted as part of expanding a fused
// compare-and-branch pseudo, so they carry no patterns of their own.
let Defs = [PSW] in {
def CMP16rr : InstS<0x40, (outs), (ins GR16:$src1, GR16:$src2),
                    "cmp\t$src1, $src2", []> {
  bits<4> src1;
  bits<4> src2;
  let Inst{15-12} = src1;
  let Inst{11-8} = src2;
}

// The two byte #data3 forms, which expandPostRAPseudo() picks when the fused
// compare-and-branch it is splitting carries a small enough constant.
class CompareImm3<bits<8> opcode, string mnemonic, RegisterClass RC,
                  Operand ImmTy>
    : InstS<opcode, (outs), (ins RC:$src1, ImmTy:$src2),
            !strconcat(mnemonic, "\t$src1, $src2"), []> {
  bits<4> src1;
  bits<3> src2;
  let Inst{15-12} = src1;
  let Inst{11} = 0;
  let Inst{10-8} = src2;
}

def CMP16ri3 : CompareImm3<0x48, "cmp", GR16, imm3>;
def CMPB8ri3 : CompareImm3<0x49, "cmpb", GR8, imm3b>;
let isCodeGenOnly = 1 in
def CMP16ri : InstL<0x46, (outs), (ins GR16:$src1, data16:$src2),
                    "cmp\t$src1, $src2", []> {
  bits<4> src1;
  bits<16> src2;
  let Inst{15-12} = 0b1111;
  let Inst{11-8} = src1;
  let Inst{31-16} = src2;
}
def CMPB8rr : InstS<0x41, (outs), (ins GR8:$src1, GR8:$src2),
                    "cmpb\t$src1, $src2", []> {
  bits<4> src1;
  bits<4> src2;
  let Inst{15-12} = src1;
  let Inst{11-8} = src2;
}
let isCodeGenOnly = 1 in
def CMPB8ri : InstL<0x47, (outs), (ins GR8:$src1, data8:$src2),
                    "cmpb\t$src1, $src2", []> {
  bits<4> src1;
  bits<8> src2;
  let Inst{15-12} = 0b1111;
  let Inst{11-8} = src1;
  let Inst{23-16} = src2;
  let Inst{31-24} = 0;
}
}

//===----------------------------------------------------------------------===//
// Multiply and Divide Instructions
//===----------------------------------------------------------------------===//

let Defs = [MDL, MDH, MDC, PSW] in {
def MULrr  : InstS<0x0B, (outs), (ins GR16:$src1, GR16:$src2),
                   "mul\t$src1, $src2", []> {
  bits<4> src1;
  bits<4> src2;
  let Inst{15-12} = src1;
  let Inst{11-8} = src2;
}
def MULUrr : InstS<0x1B, (outs), (ins GR16:$src1, GR16:$src2),
                   "mulu\t$src1, $src2", []> {
  bits<4> src1;
  bits<4> src2;
  let Inst{15-12} = src1;
  let Inst{11-8} = src2;
}
}

// DIV repeats the register number in both nibbles ("nn").
let Defs = [MDL, MDH, MDC, PSW], Uses = [MDL] in {
def DIVr  : InstS<0x4B, (outs), (ins GR16:$src), "div\t$src", []> {
  bits<4> src;
  let Inst{15-12} = src;
  let Inst{11-8} = src;
}
def DIVUr : InstS<0x5B, (outs), (ins GR16:$src), "divu\t$src", []> {
  bits<4> src;
  let Inst{15-12} = src;
  let Inst{11-8} = src;
}
}

// The multiply/divide registers are ordinary SFRs, so moving to and from them
// is a MOV mem, reg / MOV reg, mem with the SFR's address baked in.
// These are a strict subset of MOV mem, reg / MOV reg, mem, so they are
// codegen-only: the disassembler decodes the general form and prints the SFR
// address numerically.
class MoveToSFR<bits<16> addr, string name>
    : InstL<0xF6, (outs), (ins GR16:$src),
            !strconcat("mov\t", !strconcat(name, ", $src")), []> {
  let isCodeGenOnly = 1;
  bits<4> src;
  let Inst{15-12} = 0b1111;
  let Inst{11-8} = src;
  let Inst{31-16} = addr;
}

class MoveFromSFR<bits<16> addr, string name>
    : InstL<0xF2, (outs GR16:$dst), (ins),
            !strconcat("mov\t$dst, ", name), []> {
  let isCodeGenOnly = 1;
  bits<4> dst;
  let Inst{15-12} = 0b1111;
  let Inst{11-8} = dst;
  let Inst{31-16} = addr;
}

// MDH sits at FE0CH and MDL at FE0EH (short register addresses 06H and 07H);
// see the C166 User's Manual register table.
let Defs = [MDL, MDC] in
def MOVtoMDL : MoveToSFR<0xFE0E, "mdl">;
let Defs = [MDH, MDC] in
def MOVtoMDH : MoveToSFR<0xFE0C, "mdh">;
let Uses = [MDL] in
def MOVfromMDL : MoveFromSFR<0xFE0E, "mdl">;
let Uses = [MDH] in
def MOVfromMDH : MoveFromSFR<0xFE0C, "mdh">;

//===----------------------------------------------------------------------===//
// Control Flow Instructions
//===----------------------------------------------------------------------===//

let isReturn = 1, isTerminator = 1, isBarrier = 1, hasCtrlDep = 1,
    Defs = [SYSSP], Uses = [SYSSP] in {
def RET  : InstS<0xCB, (outs), (ins), "ret",  [(C166retglue)]> {
  let Inst{15-8} = 0x00;
}
def RETI : InstS<0xFB, (outs), (ins), "reti", [(C166retiglue)]> {
  let Inst{15-8} = 0x88;
}
}

// A two byte relative jump.  The low nibble of the opcode is fixed and the
// high one is the condition.
class JumpRel<dag ins, string asmstr, list<dag> pattern = []>
    : C166Inst<(outs), ins, 2, asmstr, pattern> {
  bits<8> dst;
  let Inst{3-0} = 0b1101;
  let Inst{15-8} = dst;
  let Inst{31-16} = 0;
}

let isBranch = 1, isTerminator = 1 in {
  // A plain jump is JMPAcc with cc_UC hardwired.  It is marked codegen-only so
  // that it does not collide with JMPAcc in the decoder and the asm matcher;
  // both print the same text, so nothing is lost.
  let isBarrier = 1, isCodeGenOnly = 1 in
  def JMPA : InstL<0xEA, (outs), (ins brtarget:$dst), "jmpa\tcc_UC, $dst",
                   []> {
    bits<16> dst;
    let Inst{15-12} = 0b0000;   // cc_UC
    let Inst{11-8} = 0;
    let Inst{31-16} = dst;
  }

  // Only produced by expanding the fused pseudos below and by the branch
  // rewriting done in C166InstrInfo.
  let Uses = [PSW] in
  def JMPAcc : InstL<0xEA, (outs), (ins brtarget:$dst, cc:$cond),
                     "jmpa\t$cond, $dst", []> {
    bits<16> dst;
    bits<4> cond;
    let Inst{15-12} = cond;
    let Inst{11-8} = 0;
    let Inst{31-16} = dst;
  }

  // JMPR is the two byte relative jump.  The condition is the high nibble of
  // the opcode - the whole xD row of the opcode map is JMPR, one entry per
  // condition, in the same order as the cc encoding - and the displacement
  // counts words from the instruction after it, so it reaches 127 words either
  // way.  This is what a branch is selected as; whether the target is close
  // enough is not known until the layout is, so C166AsmBackend grows the ones
  // that are not into JMPA.
  let isBarrier = 1, isCodeGenOnly = 1 in
  def JMPR : JumpRel<(ins reltarget8:$dst), "jmpr\tcc_UC, $dst",
                     [(br bb:$dst)]> {
    let Inst{7-4} = 0b0000;   // cc_UC
  }

  let Uses = [PSW] in
  def JMPRcc : JumpRel<(ins reltarget8:$dst, cc:$cond), "jmpr\t$cond, $dst"> {
    bits<4> cond;
    let Inst{7-4} = cond;
  }

  // Fused compare-and-branch.  Expanded into a cmp/jmpa pair by
  // C166InstrInfo::expandPostRAPseudo(), after everything that could have
  // scheduled a flag clobbering instruction in between has run.
  let Defs = [PSW] in {
  def BRCC16rr : Pseudo<(outs),
                        (ins brtarget:$dst, GR16:$lhs, GR16:$rhs, cc:$cond),
                        "#BRCC16rr",
                        [(C166brcc bb:$dst, GR16:$lhs, GR16:$rhs,
                                   imm:$cond)]> {
    let Size = 6;
  }
  def BRCC16ri : Pseudo<(outs),
                        (ins brtarget:$dst, GR16:$lhs, data16:$rhs, cc:$cond),
                        "#BRCC16ri",
                        [(C166brcc bb:$dst, GR16:$lhs, (i16 imm:$rhs),
                                   imm:$cond)]> {
    let Size = 8;
  }
  def BRCC8rr  : Pseudo<(outs),
                        (ins brtarget:$dst, GR8:$lhs, GR8:$rhs, cc:$cond),
                        "#BRCC8rr",
                        [(C166brcc bb:$dst, GR8:$lhs, GR8:$rhs, imm:$cond)]> {
    let Size = 6;
  }
  def BRCC8ri  : Pseudo<(outs),
                        (ins brtarget:$dst, GR8:$lhs, data8:$rhs, cc:$cond),
                        "#BRCC8ri",
                        [(C166brcc bb:$dst, GR8:$lhs, (i8 imm:$rhs),
                                   imm:$cond)]> {
    let Size = 8;
  }
  }

  let isBarrier = 1, isIndirectBranch = 1 in
  def JMPI : InstS<0x9C, (outs), (ins cc:$cond, GR16:$src),
                   "jmpi\t$cond, [$src]", []> {
    bits<4> cond;
    bits<4> src;
    let Inst{15-12} = cond;
    let Inst{11-8} = src;
  }
}

// Calls clobber the caller saved registers and the multiply/divide unit.
let isCall = 1,
    Defs = [R2, R3, R4, R5, R6, R7, R8, R9, R10, R11,
            PSW, MDL, MDH, MDC, SYSSP],
    Uses = [R0, SYSSP] in {
def CALLA : InstL<0xCA, (outs), (ins cc:$cond, calltarget:$dst),
                  "calla\t$cond, $dst", []> {
  bits<16> dst;
  bits<4> cond;
  let Inst{15-12} = cond;
  let Inst{11-8} = 0;
  let Inst{31-16} = dst;
}
def CALLI : InstS<0xAB, (outs), (ins cc:$cond, GR16:$src),
                  "calli\t$cond, [$src]", []> {
  bits<4> cond;
  bits<4> src;
  let Inst{15-12} = cond;
  let Inst{11-8} = src;
}
}

//===----------------------------------------------------------------------===//
// Miscellaneous Instructions
//===----------------------------------------------------------------------===//

let Defs = [SYSSP], Uses = [SYSSP] in {
def PUSH : InstS<0xEC, (outs), (ins reg8:$src), "push\t$src", []> {
  bits<8> src;
  let Inst{15-8} = src;
}
def POP  : InstS<0xFC, (outs reg8:$dst), (ins), "pop\t$dst", []> {
  bits<8> dst;
  let Inst{15-8} = dst;
}
}

// The protected instructions repeat their opcode in the second word.
class ProtectedInst<bits<8> op0, bits<8> op1, bits<8> op2, bits<8> op3,
                    string mnemonic>
    : InstL<op0, (outs), (ins), mnemonic, []> {
  let Inst{15-8} = op1;
  let Inst{23-16} = op2;
  let Inst{31-24} = op3;
}

let hasSideEffects = 1 in {
def NOP : InstS<0xCC, (outs), (ins), "nop", []> {
  let Inst{15-8} = 0x00;
}
def SRST   : ProtectedInst<0xB7, 0x48, 0xB7, 0xB7, "srst">;
def IDLE   : ProtectedInst<0x87, 0x78, 0x87, 0x87, "idle">;
def PWRDN  : ProtectedInst<0x97, 0x68, 0x97, 0x97, "pwrdn">;
def DISWDT : ProtectedInst<0xA5, 0x5A, 0xA5, 0xA5, "diswdt">;
def SRVWDT : ProtectedInst<0xA7, 0x58, 0xA7, 0xA7, "srvwdt">;
def EINIT  : ProtectedInst<0xB5, 0x4A, 0xB5, 0xB5, "einit">;
}

//===----------------------------------------------------------------------===//
// Bit Instructions
//===----------------------------------------------------------------------===//
//
// The bit-addressable space is named by an 8 bit "bitoff" word address: 00H to
// 7FH is internal RAM at FD00H + 2*bitoff, 80H to EFH is the SFR area at
// FF00H + 2*(bitoff - 80H), and F0H to FFH is R0 to R15.  An SFR's short
// register address is therefore also its bitoff, since both land on the same
// word.  A bit address adds a position 0 to 15 within that word.
//
// These are MC level only: knowing that an address is bit-addressable is
// beyond what the compiler can see, so nothing generates them yet.
//===----------------------------------------------------------------------===//

// BCLR and BSET put the bit position in the high nibble of the opcode byte.
class BitSetClear<bits<4> op, string mnemonic>
    : C166Inst<(outs), (ins bitaddr:$addr), 2,
               !strconcat(mnemonic, "\t$addr"), []> {
  bits<12> addr;
  let Inst{3-0} = op;
  let Inst{7-4} = addr{11-8};
  let Inst{15-8} = addr{7-0};
  let Inst{31-16} = 0;
  let Defs = [PSW];
}

def BCLR : BitSetClear<0xE, "bclr">;
def BSET : BitSetClear<0xF, "bset">;

// The compiler facing forms.  Bitoff F0H to FFH is R0 to R15, so a single bit
// set or cleared in a value the register allocator has already placed is one
// two byte instruction instead of a mask and an ALU operation - and, on a
// bit-addressable word, one indivisible bus operation instead of a read and a
// write an interrupt can arrive between.
//
// They share their encoding with the two above, so they are code generation
// only: the disassembler has exactly one way to spell the result, and the
// round trip goes back through the assembler facing form.
//
// The flags are not the ones "or" and "and" leave - BSET reports the bit as it
// was rather than anything about the result - which is sound here only because
// nothing in this backend reads PSW as a value except the carry chain, and
// that sees these as the same clobber it sees an ALU instruction as.
let Constraints = "$src = $dst", Defs = [PSW], isCodeGenOnly = 1,
    AddedComplexity = 20 in {
class BitSetClearReg<bits<4> op, string mnemonic, ImmLeaf mask, SDNode opnode>
    : C166Inst<(outs GR16:$dst), (ins GR16:$src, bitpos:$pos), 2,
               !strconcat(mnemonic, "\t$dst.$pos"),
               [(set GR16:$dst, (opnode GR16:$src, mask:$pos))]> {
  bits<4> dst;
  bits<4> pos;
  let Inst{3-0} = op;
  let Inst{7-4} = pos;
  let Inst{15-12} = 0b1111;
  let Inst{11-8} = dst;
  let Inst{31-16} = 0;
}

def BCLRr : BitSetClearReg<0xE, "bclr", bitpos_clr, and>;
def BSETr : BitSetClearReg<0xF, "bset", bitpos_set, or>;
}

// The two operand bit instructions name the destination first but encode the
// source first, and pack the two bit positions into the last byte as the
// source in the high nibble and the destination in the low one.
class BitBinary<bits<8> op, string mnemonic>
    : InstL<op, (outs), (ins bitaddr:$dst, bitaddr:$src),
            !strconcat(mnemonic, "\t$dst, $src"), []> {
  bits<12> dst;
  bits<12> src;
  let Inst{15-8} = src{7-0};
  let Inst{23-16} = dst{7-0};
  let Inst{27-24} = dst{11-8};
  let Inst{31-28} = src{11-8};
  let Defs = [PSW];
}

def BCMP  : BitBinary<0x2A, "bcmp">;
def BMOVN : BitBinary<0x3A, "bmovn">;
def BMOV  : BitBinary<0x4A, "bmov">;
def BOR   : BitBinary<0x5A, "bor">;
def BAND  : BitBinary<0x6A, "band">;
def BXOR  : BitBinary<0x7A, "bxor">;

// BFLDL and BFLDH replace the masked bits of one byte of a word.  The two are
// byte-swapped with respect to each other: BFLDL is "0A QQ @@ ##" and BFLDH is
// "1A QQ ## @@", so the mask and the value change places.
let Defs = [PSW] in {
def BFLDL : InstL<0x0A, (outs), (ins bitoff:$addr, mask8:$mask, data8:$value),
                  "bfldl\t$addr, $mask, $value", []> {
  bits<8> addr;
  bits<8> mask;
  bits<8> value;
  let Inst{15-8} = addr;
  let Inst{23-16} = mask;
  let Inst{31-24} = value;
}
def BFLDH : InstL<0x1A, (outs), (ins bitoff:$addr, mask8:$mask, data8:$value),
                  "bfldh\t$addr, $mask, $value", []> {
  bits<8> addr;
  bits<8> mask;
  bits<8> value;
  let Inst{15-8} = addr;
  let Inst{23-16} = value;
  let Inst{31-24} = mask;
}
}

// The bit test branches.  The bit position goes in the high nibble of the last
// byte, with the low one left clear.
let isBranch = 1, isTerminator = 1 in
class BitBranch<bits<8> op, string mnemonic, bit setsFlags>
    : InstL<op, (outs), (ins bitaddr:$addr, reltarget:$dst),
            !strconcat(mnemonic, "\t$addr, $dst"), []> {
  bits<12> addr;
  bits<8> dst;
  let Inst{15-8} = addr{7-0};
  let Inst{23-16} = dst;
  let Inst{27-24} = 0;
  let Inst{31-28} = addr{11-8};
  let Defs = !if(setsFlags, [PSW], []);
}

def JB   : BitBranch<0x8A, "jb",   0>;
def JNB  : BitBranch<0x9A, "jnb",  0>;
// JBC and JNBS also change the bit they tested.
def JBC  : BitBranch<0xAA, "jbc",  1>;
def JNBS : BitBranch<0xBA, "jnbs", 1>;

//===----------------------------------------------------------------------===//
// Segmented Addressing
//===----------------------------------------------------------------------===//
//
// A C166 in segmented mode addresses 16 MByte.  Data goes through the four
// Data Page Pointers: the top two bits of a 16 bit long or indirect address
// select a DPP, whose 10 bit page number supplies A23-A14.  The EXTend
// instructions override that for the next 1 to 4 instructions, either with an
// explicit page (EXTP) or by using the address as a plain 16 bit offset into
// an explicit segment (EXTS).  Code crosses segments with JMPS and CALLS,
// which name the target segment directly, and returns with RETS.
//
// These are MC level only for now: nothing generates them yet.
//===----------------------------------------------------------------------===//

// EXTS/EXTP and their combined-with-EXTR forms taking the page or segment
// from a register.  Bits 7-6 select which of the four it is.
class ExtendReg<bits<2> kind, string mnemonic>
    : InstS<0xDC, (outs), (ins GR16:$src, irang2:$range),
            !strconcat(mnemonic, "\t$src, $range"), []> {
  bits<4> src;
  bits<2> range;
  let Inst{15-14} = kind;
  let Inst{13-12} = range;
  let Inst{11-8} = src;
  let hasSideEffects = 1;
}

// The same four, taking an immediate page or segment.
class ExtendImmSeg<bits<2> kind, string mnemonic>
    : InstL<0xD7, (outs), (ins seg8:$seg, irang2:$range),
            !strconcat(mnemonic, "\t$seg, $range"), []> {
  bits<8> seg;
  bits<2> range;
  let Inst{15-14} = kind;
  let Inst{13-12} = range;
  let Inst{11-8} = 0;
  let Inst{23-16} = seg;
  let Inst{31-24} = 0;
  let hasSideEffects = 1;
}

class ExtendImmPage<bits<2> kind, string mnemonic>
    : InstL<0xD7, (outs), (ins pag10:$pag, irang2:$range),
            !strconcat(mnemonic, "\t$pag, $range"), []> {
  bits<10> pag;
  bits<2> range;
  let Inst{15-14} = kind;
  let Inst{13-12} = range;
  let Inst{11-8} = 0;
  let Inst{23-16} = pag{7-0};
  let Inst{25-24} = pag{9-8};
  let Inst{31-26} = 0;
  let hasSideEffects = 1;
}

def EXTSr  : ExtendReg<0b00, "exts">;
def EXTPr  : ExtendReg<0b01, "extp">;
def EXTSRr : ExtendReg<0b10, "extsr">;
def EXTPRr : ExtendReg<0b11, "extpr">;

def EXTSi  : ExtendImmSeg<0b00, "exts">;
def EXTSRi : ExtendImmSeg<0b10, "extsr">;

def EXTPi  : ExtendImmPage<0b01, "extp">;
def EXTPRi : ExtendImmPage<0b11, "extpr">;

// EXTR switches the short register addressing mode over to the ESFR space,
// and ATOMIC locks interrupts; both share opcode D1.
let hasSideEffects = 1 in {
def EXTR : InstS<0xD1, (outs), (ins irang2:$range), "extr\t$range", []> {
  bits<2> range;
  let Inst{15-14} = 0b10;
  let Inst{13-12} = range;
  let Inst{11-8} = 0;
}
def ATOMIC : InstS<0xD1, (outs), (ins irang2:$range), "atomic\t$range", []> {
  bits<2> range;
  let Inst{15-14} = 0b00;
  let Inst{13-12} = range;
  let Inst{11-8} = 0;
}
}

// Inter-segment control flow.
let isBranch = 1, isTerminator = 1, isBarrier = 1 in
def JMPS : InstL<0xFA, (outs), (ins jseg8:$seg, farbrtarget:$dst),
                 "jmps\t$seg, $dst", []> {
  bits<8> seg;
  bits<16> dst;
  let Inst{15-8} = seg;
  let Inst{31-16} = dst;
}

let isCall = 1,
    Defs = [R2, R3, R4, R5, R6, R7, R8, R9, R10, R11,
            PSW, MDL, MDH, MDC, SYSSP],
    Uses = [R0, SYSSP] in
def CALLS : InstL<0xDA, (outs), (ins jseg8:$seg, farcalltarget:$dst),
                  "calls\t$seg, $dst", []> {
  bits<8> seg;
  bits<16> dst;
  let Inst{15-8} = seg;
  let Inst{31-16} = dst;
}

// RETS pops the code segment pointer as well as the instruction pointer, so
// it is the counterpart of CALLS.
let isReturn = 1, isTerminator = 1, isBarrier = 1, hasCtrlDep = 1,
    Defs = [SYSSP], Uses = [SYSSP] in
def RETS : InstS<0xDB, (outs), (ins), "rets", [(C166retsglue)]> {
  let Inst{15-8} = 0x00;
}

// TRAP enters an interrupt routine from software.  It does not read a vector:
// it branches to the vector table entry itself, at 4 * the trap number, which
// is where a project puts a jump to its handler.  What it saves is what a
// hardware interrupt saves - PSW, then CSP while segmentation is on, then IP -
// so RETI returns from either.
//
// The trap number occupies bits 7 to 1 of the second byte, so the encoding
// holds half the vector's address.
let hasSideEffects = 1, Defs = [SYSSP], Uses = [SYSSP] in
def TRAP : InstS<0x9B, (outs), (ins trap7:$num), "trap\t$num", []> {
  bits<7> num;
  let Inst{15-9} = num;
  let Inst{8} = 0;
}

//===----------------------------------------------------------------------===//
// Non-Instruction Patterns
//===----------------------------------------------------------------------===//

// ADD and SUB always leave a carry behind, so the low word of a wider
// addition or subtraction is a plain one and only the words above it need
// ADDC/SUBC.  The carry travels from one to the next as glue, which keeps
// them together through instruction selection; after that only the register
// allocator can come between them, and everything it inserts - MOV, MOVB and
// the byte extensions - leaves the carry alone.
def : Pat<(addc GR16:$src1, GR16:$src2), (ADD16rr GR16:$src1, GR16:$src2)>;
def : Pat<(addc GR16:$src1, imm:$src2),  (ADD16ri GR16:$src1, imm:$src2)>;
let AddedComplexity = 10 in
def : Pat<(addc GR16:$src1, imm3:$src2), (ADD16ri3 GR16:$src1, imm3:$src2)>;
def : Pat<(addc GR16:$src1, (load addrabs:$src2)),
          (ADD16ra GR16:$src1, addrabs:$src2)>;

def : Pat<(subc GR16:$src1, GR16:$src2), (SUB16rr GR16:$src1, GR16:$src2)>;
def : Pat<(subc GR16:$src1, imm:$src2),  (SUB16ri GR16:$src1, imm:$src2)>;
let AddedComplexity = 10 in
def : Pat<(subc GR16:$src1, imm3:$src2), (SUB16ri3 GR16:$src1, imm3:$src2)>;
def : Pat<(subc GR16:$src1, (load addrabs:$src2)),
          (SUB16ra GR16:$src1, addrabs:$src2)>;

def : Pat<(C166tcret (i16 tglobaladdr:$dst)),  (TCRETURNa tglobaladdr:$dst)>;
def : Pat<(C166tcret (i16 texternalsym:$dst)), (TCRETURNa texternalsym:$dst)>;
def : Pat<(C166tcret GR16:$dst),               (TCRETURNi GR16:$dst)>;
def : Pat<(C166tcretseg (i16 tglobaladdr:$seg), (i16 tglobaladdr:$dst)),
          (TCRETURNs tglobaladdr:$seg, tglobaladdr:$dst)>;

// Direct and indirect calls, and indirect jumps, all with cc_UC (0).
def : Pat<(C166call (i16 tglobaladdr:$dst)),  (CALLA 0, tglobaladdr:$dst)>;
def : Pat<(C166call (i16 texternalsym:$dst)), (CALLA 0, texternalsym:$dst)>;
def : Pat<(C166call GR16:$src),               (CALLI 0, GR16:$src)>;

// A far call takes the callee's segment and offset as two relocations against
// the same symbol.
def : Pat<(C166callseg (i16 tglobaladdr:$seg), (i16 tglobaladdr:$dst)),
          (CALLS tglobaladdr:$seg, tglobaladdr:$dst)>;
def : Pat<(C166callseg (i16 texternalsym:$seg), (i16 texternalsym:$dst)),
          (CALLS texternalsym:$seg, texternalsym:$dst)>;
def : Pat<(brind GR16:$src),                  (JMPI 0, GR16:$src)>;

// Address materialisation.
// A far access whose segment is a symbol lets the EXTS name it directly.
multiclass FarSymbolAccess<SDPatternOperator sym> {
  // Both halves of the address settled at link time: nothing goes in a
  // register at all.
  let AddedComplexity = 10 in {
  def : Pat<(C166farload (i16 (C166Wrapper sym:$off)),
                         (i16 (C166Wrapper sym:$seg))),
            (FARLOAD16a sym:$off, sym:$seg)>;
  def : Pat<(i8 (C166farload (i16 (C166Wrapper sym:$off)),
                             (i16 (C166Wrapper sym:$seg)))),
            (FARLOAD8a sym:$off, sym:$seg)>;
  def : Pat<(C166farstore GR16:$src, (i16 (C166Wrapper sym:$off)),
                          (i16 (C166Wrapper sym:$seg))),
            (FARSTORE16a GR16:$src, sym:$off, sym:$seg)>;
  def : Pat<(C166farstore GR8:$src, (i16 (C166Wrapper sym:$off)),
                          (i16 (C166Wrapper sym:$seg))),
            (FARSTORE8a GR8:$src, sym:$off, sym:$seg)>;
  }

  def : Pat<(C166farload GR16:$off, (i16 (C166Wrapper sym:$seg))),
            (FARLOAD16i GR16:$off, sym:$seg)>;
  def : Pat<(i8 (C166farload GR16:$off, (i16 (C166Wrapper sym:$seg)))),
            (FARLOAD8i GR16:$off, sym:$seg)>;
  def : Pat<(C166farstore GR16:$src, GR16:$off, (i16 (C166Wrapper sym:$seg))),
            (FARSTORE16i GR16:$src, GR16:$off, sym:$seg)>;
  def : Pat<(C166farstore GR8:$src, GR16:$off, (i16 (C166Wrapper sym:$seg))),
            (FARSTORE8i GR8:$src, GR16:$off, sym:$seg)>;
}

defm : FarSymbolAccess<tglobaladdr>;
defm : FarSymbolAccess<tblockaddress>;

def : Pat<(i16 (C166Wrapper tglobaladdr:$dst)),   (MOV16ri tglobaladdr:$dst)>;
def : Pat<(i16 (C166Wrapper texternalsym:$dst)),  (MOV16ri texternalsym:$dst)>;
def : Pat<(i16 (C166Wrapper tblockaddress:$dst)), (MOV16ri tblockaddress:$dst)>;
def : Pat<(i16 (C166Wrapper tjumptable:$dst)),    (MOV16ri tjumptable:$dst)>;

// Truncation is free: use the low byte sub-register.
def : Pat<(i8 (trunc GR16:$src)), (EXTRACT_SUBREG GR16:$src, subreg_lo)>;

// An any-extension does not have to clear the high byte.
def : Pat<(i16 (anyext GR8:$src)),
          (INSERT_SUBREG (i16 (IMPLICIT_DEF)), GR8:$src, subreg_lo)>;

// Byte shifts.  The hardware only shifts words, so the value is widened first
// and the low byte of the result is kept.  A left shift does not care about
// the high byte; the right shifts need it filled in correctly.  Note that the
// shift amount is a word even for a byte shift.
class ByteShiftPat<SDPatternOperator opnode, dag widened, Instruction inst>
    : Pat<(i8 (opnode GR8:$src, GR16:$amt)),
          (EXTRACT_SUBREG (inst widened, GR16:$amt), subreg_lo)>;

class ByteShiftImmPat<SDPatternOperator opnode, dag widened, Instruction inst>
    : Pat<(i8 (opnode GR8:$src, (i16 imm4:$amt))),
          (EXTRACT_SUBREG (inst widened, imm4:$amt), subreg_lo)>;

defvar AnyExtByte = (INSERT_SUBREG (i16 (IMPLICIT_DEF)), GR8:$src, subreg_lo);
defvar ZExtByte = (MOVBZ16r8 GR8:$src);
defvar SExtByte = (MOVBS16r8 GR8:$src);

def : ByteShiftPat<shl, AnyExtByte, SHL16rr>;
def : ByteShiftPat<srl, ZExtByte, SHR16rr>;
def : ByteShiftPat<sra, SExtByte, ASHR16rr>;

def : ByteShiftImmPat<shl, AnyExtByte, SHL16ri>;
def : ByteShiftImmPat<srl, ZExtByte, SHR16ri>;
def : ByteShiftImmPat<sra, SExtByte, ASHR16ri>;

// Sign extending the low byte of a word in place.
def : Pat<(sext_inreg GR16:$src, i8),
          (MOVBS16r8 (EXTRACT_SUBREG GR16:$src, subreg_lo))>;

// Narrowing stores.
def : Pat<(truncstorei8 GR16:$src, addrri:$dst),
          (MOVB8mr addrri:$dst, (EXTRACT_SUBREG GR16:$src, subreg_lo))>;
def : Pat<(truncstorei8 GR16:$src, addrabs:$dst),
          (MOVB8ar addrabs:$dst, (EXTRACT_SUBREG GR16:$src, subreg_lo))>;

// Storing an address.
def : Pat<(store (i16 (C166Wrapper tglobaladdr:$src)), addrri:$dst),
          (MOV16mr addrri:$dst, (MOV16ri tglobaladdr:$src))>;
def : Pat<(store (i16 (C166Wrapper tglobaladdr:$src)), addrabs:$dst),
          (MOV16ar addrabs:$dst, (MOV16ri tglobaladdr:$src))>;
