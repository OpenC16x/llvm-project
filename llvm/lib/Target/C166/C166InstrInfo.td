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

// A condition code, printed as cc_XX.
def cc : Operand<i8> {
  let PrintMethod = "printCCOperand";
}

// [Rw] / [Rw + #data16] addressing.
def memri : Operand<i16> {
  let PrintMethod = "printMemRIOperand";
  let MIOperandInfo = (ops GR16, i16imm);
}

// A plain 16 bit data address.
def addr16 : Operand<i16> {
  let PrintMethod = "printAddr16Operand";
}

// Branch and call destinations are printed bare (no '#' prefix).
def brtarget : Operand<OtherVT> {
  let PrintMethod = "printBrTargetOperand";
}

def calltarget : Operand<i16> {
  let PrintMethod = "printCallTargetOperand";
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
def MOV16rr : InstS<(outs GR16:$dst), (ins GR16:$src), "mov\t$dst, $src", []>;
def MOVB8rr : InstS<(outs GR8:$dst), (ins GR8:$src), "movb\t$dst, $src", []>;
}

let isMoveImm = 1, isReMaterializable = 1, isAsCheapAsAMove = 1 in {
def MOV16ri : InstL<(outs GR16:$dst), (ins i16imm:$src), "mov\t$dst, $src",
                    [(set GR16:$dst, imm:$src)]>;
def MOVB8ri : InstL<(outs GR8:$dst), (ins i8imm:$src), "movb\t$dst, $src",
                    [(set GR8:$dst, imm:$src)]>;
}

let mayLoad = 1 in {
def MOV16rm : InstL<(outs GR16:$dst), (ins memri:$src), "mov\t$dst, $src",
                    [(set GR16:$dst, (load addrri:$src))]>;
def MOV16ra : InstL<(outs GR16:$dst), (ins addr16:$src), "mov\t$dst, $src",
                    [(set GR16:$dst, (load addrabs:$src))]>;
def MOVB8rm : InstL<(outs GR8:$dst), (ins memri:$src), "movb\t$dst, $src",
                    [(set GR8:$dst, (load addrri:$src))]>;
def MOVB8ra : InstL<(outs GR8:$dst), (ins addr16:$src), "movb\t$dst, $src",
                    [(set GR8:$dst, (load addrabs:$src))]>;
}

let mayStore = 1 in {
def MOV16mr : InstL<(outs), (ins memri:$dst, GR16:$src), "mov\t$dst, $src",
                    [(store GR16:$src, addrri:$dst)]>;
def MOV16ar : InstL<(outs), (ins addr16:$dst, GR16:$src), "mov\t$dst, $src",
                    [(store GR16:$src, addrabs:$dst)]>;
def MOVB8mr : InstL<(outs), (ins memri:$dst, GR8:$src), "movb\t$dst, $src",
                    [(store GR8:$src, addrri:$dst)]>;
def MOVB8ar : InstL<(outs), (ins addr16:$dst, GR8:$src), "movb\t$dst, $src",
                    [(store GR8:$src, addrabs:$dst)]>;
}

// Byte to word conversions.
let Defs = [PSW] in {
def MOVBZ16r8 : InstS<(outs GR16:$dst), (ins GR8:$src), "movbz\t$dst, $src",
                      [(set GR16:$dst, (zext GR8:$src))]>;
def MOVBS16r8 : InstS<(outs GR16:$dst), (ins GR8:$src), "movbs\t$dst, $src",
                      [(set GR16:$dst, (sext GR8:$src))]>;
}

//===----------------------------------------------------------------------===//
// Arithmetic and Logical Instructions
//===----------------------------------------------------------------------===//

// C166 ALU instructions are two operand: the destination is also the first
// source.  Supported source flavours are a register, a 16 bit immediate and a
// 16 bit absolute address.
multiclass ArithW<string mnemonic, SDNode opnode, bit commutable = 0> {
  let Constraints = "$src1 = $dst", Defs = [PSW] in {
    let isCommutable = commutable in
    def 16rr : InstS<(outs GR16:$dst), (ins GR16:$src1, GR16:$src2),
                     !strconcat(mnemonic, "\t$dst, $src2"),
                     [(set GR16:$dst, (opnode GR16:$src1, GR16:$src2))]>;
    def 16ri : InstL<(outs GR16:$dst), (ins GR16:$src1, i16imm:$src2),
                     !strconcat(mnemonic, "\t$dst, $src2"),
                     [(set GR16:$dst, (opnode GR16:$src1, imm:$src2))]>;
    let mayLoad = 1 in
    def 16ra : InstL<(outs GR16:$dst), (ins GR16:$src1, addr16:$src2),
                     !strconcat(mnemonic, "\t$dst, $src2"),
                     [(set GR16:$dst,
                           (opnode GR16:$src1, (load addrabs:$src2)))]>;
  }
}

multiclass ArithB<string mnemonic, SDNode opnode, bit commutable = 0> {
  let Constraints = "$src1 = $dst", Defs = [PSW] in {
    let isCommutable = commutable in
    def 8rr : InstS<(outs GR8:$dst), (ins GR8:$src1, GR8:$src2),
                    !strconcat(mnemonic, "\t$dst, $src2"),
                    [(set GR8:$dst, (opnode GR8:$src1, GR8:$src2))]>;
    def 8ri : InstL<(outs GR8:$dst), (ins GR8:$src1, i8imm:$src2),
                    !strconcat(mnemonic, "\t$dst, $src2"),
                    [(set GR8:$dst, (opnode GR8:$src1, imm:$src2))]>;
    let mayLoad = 1 in
    def 8ra : InstL<(outs GR8:$dst), (ins GR8:$src1, addr16:$src2),
                    !strconcat(mnemonic, "\t$dst, $src2"),
                    [(set GR8:$dst, (opnode GR8:$src1, (load addrabs:$src2)))]>;
  }
}

defm ADD  : ArithW<"add",  add,  1>;
defm SUB  : ArithW<"sub",  sub,  0>;
defm AND  : ArithW<"and",  and,  1>;
defm OR   : ArithW<"or",   or,   1>;
defm XOR  : ArithW<"xor",  xor,  1>;

defm ADDB : ArithB<"addb", add,  1>;
defm SUBB : ArithB<"subb", sub,  0>;
defm ANDB : ArithB<"andb", and,  1>;
defm ORB  : ArithB<"orb",  or,   1>;
defm XORB : ArithB<"xorb", xor,  1>;

// Add/subtract with carry, used when expanding wider integer arithmetic.
let Constraints = "$src1 = $dst", Defs = [PSW], Uses = [PSW] in {
def ADDC16rr : InstS<(outs GR16:$dst), (ins GR16:$src1, GR16:$src2),
                     "addc\t$dst, $src2", []>;
def SUBC16rr : InstS<(outs GR16:$dst), (ins GR16:$src1, GR16:$src2),
                     "subc\t$dst, $src2", []>;
}

// Unary operations.
let Constraints = "$src = $dst", Defs = [PSW] in {
def CPL16r  : InstS<(outs GR16:$dst), (ins GR16:$src), "cpl\t$dst",
                    [(set GR16:$dst, (not GR16:$src))]>;
def NEG16r  : InstS<(outs GR16:$dst), (ins GR16:$src), "neg\t$dst",
                    [(set GR16:$dst, (ineg GR16:$src))]>;
def CPLB8r  : InstS<(outs GR8:$dst), (ins GR8:$src), "cplb\t$dst",
                    [(set GR8:$dst, (not GR8:$src))]>;
def NEGB8r  : InstS<(outs GR8:$dst), (ins GR8:$src), "negb\t$dst",
                    [(set GR8:$dst, (ineg GR8:$src))]>;
}

//===----------------------------------------------------------------------===//
// Shift and Rotate Instructions
//===----------------------------------------------------------------------===//

multiclass ShiftW<string mnemonic, SDNode opnode> {
  let Constraints = "$src1 = $dst", Defs = [PSW] in {
    def 16rr : InstS<(outs GR16:$dst), (ins GR16:$src1, GR16:$src2),
                     !strconcat(mnemonic, "\t$dst, $src2"),
                     [(set GR16:$dst, (opnode GR16:$src1, GR16:$src2))]>;
    def 16ri : InstS<(outs GR16:$dst), (ins GR16:$src1, imm4:$src2),
                     !strconcat(mnemonic, "\t$dst, $src2"),
                     [(set GR16:$dst, (opnode GR16:$src1, imm4:$src2))]>;
  }
}

defm SHL  : ShiftW<"shl",  shl>;
defm SHR  : ShiftW<"shr",  srl>;
defm ASHR : ShiftW<"ashr", sra>;
defm ROL  : ShiftW<"rol",  rotl>;
defm ROR  : ShiftW<"ror",  rotr>;

//===----------------------------------------------------------------------===//
// Comparison Instructions
//===----------------------------------------------------------------------===//

// The compares are only ever emitted as part of expanding a fused
// compare-and-branch pseudo, so they carry no patterns of their own.
let Defs = [PSW] in {
def CMP16rr : InstS<(outs), (ins GR16:$src1, GR16:$src2), "cmp\t$src1, $src2",
                    []>;
def CMP16ri : InstL<(outs), (ins GR16:$src1, i16imm:$src2), "cmp\t$src1, $src2",
                    []>;
def CMPB8rr : InstS<(outs), (ins GR8:$src1, GR8:$src2), "cmpb\t$src1, $src2",
                    []>;
def CMPB8ri : InstL<(outs), (ins GR8:$src1, i8imm:$src2), "cmpb\t$src1, $src2",
                    []>;
}

//===----------------------------------------------------------------------===//
// Multiply and Divide Instructions
//===----------------------------------------------------------------------===//

let Defs = [MDL, MDH, MDC, PSW] in {
def MULrr  : InstS<(outs), (ins GR16:$src1, GR16:$src2), "mul\t$src1, $src2",
                   []>;
def MULUrr : InstS<(outs), (ins GR16:$src1, GR16:$src2), "mulu\t$src1, $src2",
                   []>;
}

let Defs = [MDL, MDH, MDC, PSW], Uses = [MDL] in {
def DIVr  : InstS<(outs), (ins GR16:$src), "div\t$src", []>;
def DIVUr : InstS<(outs), (ins GR16:$src), "divu\t$src", []>;
}

let Defs = [MDL, MDC] in
def MOVtoMDL : InstL<(outs), (ins GR16:$src), "mov\tmdl, $src", []>;
let Defs = [MDH, MDC] in
def MOVtoMDH : InstL<(outs), (ins GR16:$src), "mov\tmdh, $src", []>;
let Uses = [MDL] in
def MOVfromMDL : InstL<(outs GR16:$dst), (ins), "mov\t$dst, mdl", []>;
let Uses = [MDH] in
def MOVfromMDH : InstL<(outs GR16:$dst), (ins), "mov\t$dst, mdh", []>;

//===----------------------------------------------------------------------===//
// Control Flow Instructions
//===----------------------------------------------------------------------===//

let isReturn = 1, isTerminator = 1, isBarrier = 1, hasCtrlDep = 1,
    Defs = [SYSSP], Uses = [SYSSP] in {
def RET  : InstS<(outs), (ins), "ret",  [(C166retglue)]>;
def RETI : InstS<(outs), (ins), "reti", [(C166retiglue)]>;
}

let isBranch = 1, isTerminator = 1 in {
  let isBarrier = 1 in
  def JMPA : InstL<(outs), (ins brtarget:$dst), "jmpa\tcc_UC, $dst",
                   [(br bb:$dst)]>;

  // Only produced by expanding the fused pseudos below and by the branch
  // rewriting done in C166InstrInfo.
  let Uses = [PSW] in
  def JMPAcc : InstL<(outs), (ins brtarget:$dst, cc:$cond), "jmpa\t$cond, $dst",
                     []>;

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
                        (ins brtarget:$dst, GR16:$lhs, i16imm:$rhs, cc:$cond),
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
                        (ins brtarget:$dst, GR8:$lhs, i8imm:$rhs, cc:$cond),
                        "#BRCC8ri",
                        [(C166brcc bb:$dst, GR8:$lhs, (i8 imm:$rhs),
                                   imm:$cond)]> {
    let Size = 8;
  }
  }

  let isBarrier = 1, isIndirectBranch = 1 in
  def JMPI : InstS<(outs), (ins GR16:$src), "jmpi\tcc_UC, [$src]",
                   [(brind GR16:$src)]>;
}

// Calls clobber the caller saved registers and the multiply/divide unit.
let isCall = 1,
    Defs = [R2, R3, R4, R5, R6, R7, R8, R9, R10, R11,
            PSW, MDL, MDH, MDC, SYSSP],
    Uses = [R0, SYSSP] in {
def CALLA : InstL<(outs), (ins calltarget:$dst), "calla\tcc_UC, $dst", []>;
def CALLI : InstS<(outs), (ins GR16:$src), "calli\tcc_UC, [$src]",
                  [(C166call GR16:$src)]>;
}

//===----------------------------------------------------------------------===//
// Miscellaneous Instructions
//===----------------------------------------------------------------------===//

let Defs = [SYSSP], Uses = [SYSSP] in {
def PUSH : InstS<(outs), (ins GR16:$src), "push\t$src", []>;
def POP  : InstS<(outs GR16:$dst), (ins), "pop\t$dst", []>;
}

let hasSideEffects = 1 in {
def NOP    : InstS<(outs), (ins), "nop", []>;
def SRST   : InstL<(outs), (ins), "srst", []>;
def IDLE   : InstL<(outs), (ins), "idle", []>;
def PWRDN  : InstL<(outs), (ins), "pwrdn", []>;
def DISWDT : InstL<(outs), (ins), "diswdt", []>;
def SRVWDT : InstL<(outs), (ins), "srvwdt", []>;
def EINIT  : InstL<(outs), (ins), "einit", []>;
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
