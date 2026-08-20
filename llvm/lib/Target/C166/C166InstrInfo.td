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

//===----------------------------------------------------------------------===//
// C166 Specific Node Definitions
//===----------------------------------------------------------------------===//

def C166retglue  : SDNode<"C166ISD::RET_GLUE", SDTNone,
                          [SDNPHasChain, SDNPOptInGlue, SDNPVariadic]>;
def C166retiglue : SDNode<"C166ISD::RETI_GLUE", SDTNone,
                          [SDNPHasChain, SDNPOptInGlue, SDNPVariadic]>;
def C166call     : SDNode<"C166ISD::CALL", SDT_C166Call,
                          [SDNPHasChain, SDNPOutGlue, SDNPOptInGlue,
                           SDNPVariadic]>;
def C166callseq_start : SDNode<"ISD::CALLSEQ_START", SDT_C166CallSeqStart,
                               [SDNPHasChain, SDNPOutGlue]>;
def C166callseq_end   : SDNode<"ISD::CALLSEQ_END", SDT_C166CallSeqEnd,
                               [SDNPHasChain, SDNPOptInGlue, SDNPOutGlue]>;
def C166Wrapper  : SDNode<"C166ISD::Wrapper", SDT_C166Wrapper>;
def C166brcc     : SDNode<"C166ISD::BR_CC", SDT_C166BrCC, [SDNPHasChain]>;
def C166selectcc : SDNode<"C166ISD::SELECT_CC", SDT_C166SelectCC>;

//===----------------------------------------------------------------------===//
// Operand Definitions
//===----------------------------------------------------------------------===//

// A condition code, printed as cc_XX and encoded in a 4 bit field.
def cc : Operand<i8> {
  let PrintMethod = "printCCOperand";
  let DecoderMethod = "decodeCondCode";
}

// [Rw] / [Rw + #data16] addressing.  The encoder packs the two halves as
// (disp << 4) | base so that a single value can feed both instruction fields.
def memri : Operand<i16> {
  let PrintMethod = "printMemRIOperand";
  let MIOperandInfo = (ops GR16, i16imm);
  let EncoderMethod = "getMemRIOpValue";
  let DecoderMethod = "decodeMemRIOperand";
}

// A plain 16 bit data address ("mem" in the manual).
def addr16 : Operand<i16> {
  let PrintMethod = "printAddr16Operand";
  let EncoderMethod = "getAddr16OpValue";
}

// Branch and call destinations ("caddr") are printed bare (no '#' prefix).
def brtarget : Operand<OtherVT> {
  let PrintMethod = "printBrTargetOperand";
  let EncoderMethod = "getAddr16OpValue";
}

def calltarget : Operand<i16> {
  let PrintMethod = "printCallTargetOperand";
  let EncoderMethod = "getAddr16OpValue";
}

// 16 and 8 bit immediates (#data16 / #data8).  These get their own operand
// classes so that the encoder can install a relocation when the value turns
// out to be a symbol reference.
def data16 : Operand<i16> {
  let EncoderMethod = "getData16OpValue";
}

def data8 : Operand<i8> {
  let EncoderMethod = "getData8OpValue";
}

// Shift and rotate counts are #data4.
def imm4 : Operand<i16>, ImmLeaf<i16, [{
  return Imm >= 0 && Imm < 16;
}]>;

//===----------------------------------------------------------------------===//
// Complex Patterns
//===----------------------------------------------------------------------===//

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

def Select16_16 : SelectPseudo<GR16, GR16>;
def Select16_8  : SelectPseudo<GR16, GR8>;
def Select8_16  : SelectPseudo<GR8, GR16>;
def Select8_8   : SelectPseudo<GR8, GR8>;

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
// MOV reg, #data16  /  MOVB reg, #data8
def MOV16ri : InstL<0xE6, (outs GR16:$dst), (ins data16:$src),
                    "mov\t$dst, $src", [(set GR16:$dst, imm:$src)]> {
  bits<4> dst;
  bits<16> src;
  let Inst{15-12} = 0b1111;
  let Inst{11-8} = dst;
  let Inst{31-16} = src;
}
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
// the format as "mn", so the byte source sits in the high nibble.
let Defs = [PSW] in {
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
}

//===----------------------------------------------------------------------===//
// Arithmetic and Logical Instructions
//===----------------------------------------------------------------------===//

// C166 ALU instructions are two operand: the destination is also the first
// source.  Supported source flavours are a register, a 16 bit immediate and a
// 16 bit absolute address.  The three opcodes of a group differ only in the
// low nibble (x0 / x6 / x2 for words, x1 / x7 / x3 for bytes).
multiclass ArithW<string mnemonic, bits<8> opRR, bits<8> opRI, bits<8> opRA,
                  SDNode opnode, bit commutable = 0> {
  let Constraints = "$src1 = $dst", Defs = [PSW] in {
    let isCommutable = commutable in
    def 16rr : InstS<opRR, (outs GR16:$dst), (ins GR16:$src1, GR16:$src2),
                     !strconcat(mnemonic, "\t$dst, $src2"),
                     [(set GR16:$dst, (opnode GR16:$src1, GR16:$src2))]> {
      bits<4> dst;
      bits<4> src2;
      let Inst{15-12} = dst;
      let Inst{11-8} = src2;
    }
    def 16ri : InstL<opRI, (outs GR16:$dst), (ins GR16:$src1, data16:$src2),
                     !strconcat(mnemonic, "\t$dst, $src2"),
                     [(set GR16:$dst, (opnode GR16:$src1, imm:$src2))]> {
      bits<4> dst;
      bits<16> src2;
      let Inst{15-12} = 0b1111;
      let Inst{11-8} = dst;
      let Inst{31-16} = src2;
    }
    let mayLoad = 1 in
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
    let isCommutable = commutable in
    def 8rr : InstS<opRR, (outs GR8:$dst), (ins GR8:$src1, GR8:$src2),
                    !strconcat(mnemonic, "\t$dst, $src2"),
                    [(set GR8:$dst, (opnode GR8:$src1, GR8:$src2))]> {
      bits<4> dst;
      bits<4> src2;
      let Inst{15-12} = dst;
      let Inst{11-8} = src2;
    }
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
    let mayLoad = 1 in
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

// Add/subtract with carry, used when expanding wider integer arithmetic.
let Constraints = "$src1 = $dst", Defs = [PSW], Uses = [PSW] in {
def ADDC16rr : InstS<0x10, (outs GR16:$dst), (ins GR16:$src1, GR16:$src2),
                     "addc\t$dst, $src2", []> {
  bits<4> dst;
  bits<4> src2;
  let Inst{15-12} = dst;
  let Inst{11-8} = src2;
}
def SUBC16rr : InstS<0x30, (outs GR16:$dst), (ins GR16:$src1, GR16:$src2),
                     "subc\t$dst, $src2", []> {
  bits<4> dst;
  bits<4> src2;
  let Inst{15-12} = dst;
  let Inst{11-8} = src2;
}
}

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

let Defs = [MDL, MDC] in
def MOVtoMDL : MoveToSFR<0xFE0C, "mdl">;
let Defs = [MDH, MDC] in
def MOVtoMDH : MoveToSFR<0xFE0E, "mdh">;
let Uses = [MDL] in
def MOVfromMDL : MoveFromSFR<0xFE0C, "mdl">;
let Uses = [MDH] in
def MOVfromMDH : MoveFromSFR<0xFE0E, "mdh">;

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

let isBranch = 1, isTerminator = 1 in {
  // A plain jump is JMPAcc with cc_UC hardwired.  It is marked codegen-only so
  // that it does not collide with JMPAcc in the decoder and the asm matcher;
  // both print the same text, so nothing is lost.
  let isBarrier = 1, isCodeGenOnly = 1 in
  def JMPA : InstL<0xEA, (outs), (ins brtarget:$dst), "jmpa\tcc_UC, $dst",
                   [(br bb:$dst)]> {
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
  def JMPI : InstS<0x9C, (outs), (ins GR16:$src), "jmpi\tcc_UC, [$src]",
                   [(brind GR16:$src)]> {
    bits<4> src;
    let Inst{15-12} = 0b0000;   // cc_UC
    let Inst{11-8} = src;
  }
}

// Calls clobber the caller saved registers and the multiply/divide unit.
let isCall = 1,
    Defs = [R2, R3, R4, R5, R6, R7, R8, R9, R10, R11,
            PSW, MDL, MDH, MDC, SYSSP],
    Uses = [R0, SYSSP] in {
def CALLA : InstL<0xCA, (outs), (ins calltarget:$dst), "calla\tcc_UC, $dst",
                  []> {
  bits<16> dst;
  let Inst{15-12} = 0b0000;   // cc_UC
  let Inst{11-8} = 0;
  let Inst{31-16} = dst;
}
def CALLI : InstS<0xAB, (outs), (ins GR16:$src), "calli\tcc_UC, [$src]",
                  [(C166call GR16:$src)]> {
  bits<4> src;
  let Inst{15-12} = 0b0000;   // cc_UC
  let Inst{11-8} = src;
}
}

//===----------------------------------------------------------------------===//
// Miscellaneous Instructions
//===----------------------------------------------------------------------===//

let Defs = [SYSSP], Uses = [SYSSP] in {
def PUSH : InstS<0xEC, (outs), (ins GR16:$src), "push\t$src", []> {
  bits<4> src;
  let Inst{15-12} = 0b1111;
  let Inst{11-8} = src;
}
def POP  : InstS<0xFC, (outs GR16:$dst), (ins), "pop\t$dst", []> {
  bits<4> dst;
  let Inst{15-12} = 0b1111;
  let Inst{11-8} = dst;
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
// Non-Instruction Patterns
//===----------------------------------------------------------------------===//

// Direct calls.
def : Pat<(C166call (i16 tglobaladdr:$dst)),  (CALLA tglobaladdr:$dst)>;
def : Pat<(C166call (i16 texternalsym:$dst)), (CALLA texternalsym:$dst)>;

// Address materialisation.
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
