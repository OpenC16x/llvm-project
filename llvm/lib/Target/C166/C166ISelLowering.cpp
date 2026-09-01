//===-- C166ISelLowering.cpp - C166 DAG Lowering Implementation -----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the C166TargetLowering class.
//
//===----------------------------------------------------------------------===//

#include "C166ISelLowering.h"
#include "C166.h"
#include "C166MachineFunctionInfo.h"
#include "C166SelectionDAGInfo.h"
#include "C166Subtarget.h"
#include "C166TargetMachine.h"
#include "llvm/CodeGen/CallingConvLower.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/SelectionDAG.h"
#include "llvm/CodeGen/TargetLoweringObjectFileImpl.h"
#include "llvm/CodeGen/ValueTypes.h"
#include "llvm/IR/CallingConv.h"
#include "llvm/IR/DiagnosticInfo.h"
#include "llvm/IR/Function.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/raw_ostream.h"
#include <limits>

using namespace llvm;

#define DEBUG_TYPE "c166-lower"

#define GET_CALLING_CONV_IMPL
#include "C166GenCallingConv.inc"

C166TargetLowering::C166TargetLowering(const TargetMachine &TM,
                                       const C166Subtarget &STI)
    : TargetLowering(TM, STI), Subtarget(STI) {
  addRegisterClass(MVT::i8, &C166::GR8RegClass);
  addRegisterClass(MVT::i16, &C166::GR16RegClass);

  computeRegisterProperties(STI.getRegisterInfo());

  setStackPointerRegisterToSaveRestore(C166::R0);

  setBooleanContents(ZeroOrOneBooleanContent);
  setBooleanVectorContents(ZeroOrOneBooleanContent);

  setMinFunctionAlignment(Align(2));
  setPrefFunctionAlignment(Align(2));

  // There are no atomic instructions beyond a plain load and store.
  setMaxAtomicSizeInBitsSupported(16);

  // Extending loads of a byte always go through a real byte load followed by
  // MOVBZ/MOVBS.
  for (MVT VT : MVT::integer_valuetypes()) {
    setLoadExtAction(ISD::EXTLOAD, VT, MVT::i1, Promote);
    setLoadExtAction(ISD::SEXTLOAD, VT, MVT::i1, Promote);
    setLoadExtAction(ISD::ZEXTLOAD, VT, MVT::i1, Promote);
    setLoadExtAction(ISD::EXTLOAD, VT, MVT::i8, Expand);
    setLoadExtAction(ISD::SEXTLOAD, VT, MVT::i8, Expand);
    setLoadExtAction(ISD::ZEXTLOAD, VT, MVT::i8, Expand);
  }
  setTruncStoreAction(MVT::i16, MVT::i1, Expand);
  setTruncStoreAction(MVT::i8, MVT::i1, Expand);

  // Byte operations that the hardware does not have are done in words.
  for (unsigned Opc : {ISD::MUL, ISD::MULHS, ISD::MULHU, ISD::SDIV, ISD::UDIV,
                       ISD::SREM, ISD::UREM})
    setOperationAction(Opc, MVT::i8, Promote);

  // Byte shifts stay legal: they are matched by patterns that widen the value
  // into a word register.  Custom lowering them would fight the DAG combiner,
  // which narrows trunc(shift(extend x)) straight back again.

  // i16 multiply and divide exist in hardware, anything wider is a libcall.
  setOperationAction(ISD::MULHS, MVT::i16, Legal);
  setOperationAction(ISD::MULHU, MVT::i16, Legal);

  // A 32 bit division whose divisor came from 16 bits is two divides rather
  // than a call; see ReplaceNodeResults().  i32 is not a legal type here, so
  // the custom action reaches these through the type legalizer, and anything
  // that is not that shape falls back to the libcall.
  for (unsigned Opc : {ISD::UDIV, ISD::UREM, ISD::SDIV, ISD::SREM})
    setOperationAction(Opc, MVT::i32, Custom);

  // A 32 bit signed minimum or maximum is one comparison on the coprocessor's
  // 40 bit accumulator, where without it the type legalizer builds a tree of
  // five basic blocks to compare two words in order.  Only the signed pair:
  // CoMIN and CoMAX compare signed and there is no unsigned form of either.
  // The 16 bit ones are left alone - a compare and a conditional move is
  // already three instructions and six bytes, which nothing here beats.
  if (Subtarget.hasMAC())
    for (unsigned Opc : {ISD::SMAX, ISD::SMIN})
      setOperationAction(Opc, MVT::i32, Custom);

  for (MVT VT : {MVT::i8, MVT::i16}) {
    setOperationAction(ISD::ROTL, VT, VT == MVT::i16 ? Legal : Expand);
    setOperationAction(ISD::ROTR, VT, VT == MVT::i16 ? Legal : Expand);
    setOperationAction(ISD::BSWAP, VT, Expand);
    setOperationAction(ISD::CTTZ, VT, Expand);
    // PRIOR counts how far the leftmost set bit is from the top, which is the
    // count of leading zeroes and is what CTLZ_ZERO_POISON asks for.  It leaves
    // zero behind for a source of zero, where CTLZ wants sixteen, so the full
    // one stays expanded - the generic expansion is this instruction and a
    // test for zero rather than the twenty five instruction bit smear it was.
    setOperationAction(ISD::CTLZ, VT, Expand);
    setOperationAction(ISD::CTLZ_ZERO_POISON, VT,
                       VT == MVT::i16 ? Legal : Expand);
    setOperationAction(ISD::CTPOP, VT, Expand);
    // One DIV produces the quotient and the remainder together, so keeping
    // DIVREM whole is what stops "a / b" and "a % b" issuing two of the
    // slowest instruction on the core.  A byte one is promoted to a word.
    setOperationAction(ISD::SDIVREM, VT, VT == MVT::i16 ? Legal : Expand);
    setOperationAction(ISD::UDIVREM, VT, VT == MVT::i16 ? Legal : Expand);
    // A word multiply already produces both halves, so keeping MUL_LOHI whole
    // is what stops a widening multiply issuing the MUL twice.  A byte one is
    // promoted to a word before it gets here.
    setOperationAction(ISD::SMUL_LOHI, VT, VT == MVT::i16 ? Legal : Expand);
    setOperationAction(ISD::UMUL_LOHI, VT, VT == MVT::i16 ? Legal : Expand);
    // A word shift takes its count from a register, so a wide shift by a
    // variable amount is a handful of instructions rather than a call to
    // __ashlsi3 and friends.  That matters beyond the speed: code in PSRAM
    // cannot reach the builtins, which live in flash, so a variable shift used
    // to be something such code had to avoid.  Only the word form; a byte one
    // is promoted before it gets here.
    LegalizeAction ShiftParts = VT == MVT::i16 ? Custom : Expand;
    setOperationAction(ISD::SHL_PARTS, VT, ShiftParts);
    setOperationAction(ISD::SRL_PARTS, VT, ShiftParts);
    setOperationAction(ISD::SRA_PARTS, VT, ShiftParts);
    // A word add or subtract leaves its carry in the PSW and ADDC/SUBC read
    // it back, so wider arithmetic is a carry chain rather than a sequence of
    // compares.  Only the word forms exist; a byte carry has nowhere to go
    // because nothing wider than a byte is built out of bytes.
    LegalizeAction Action = VT == MVT::i16 ? Legal : Expand;
    setOperationAction(ISD::ADDC, VT, Action);
    setOperationAction(ISD::ADDE, VT, Action);
    setOperationAction(ISD::SUBC, VT, Action);
    setOperationAction(ISD::SUBE, VT, Action);

    setOperationAction(ISD::SETCC, VT, Custom);
    setOperationAction(ISD::SELECT, VT, Expand);
    setOperationAction(ISD::SELECT_CC, VT, Custom);
    setOperationAction(ISD::BR_CC, VT, Custom);
  }

  setOperationAction(ISD::BRCOND, MVT::Other, Expand);
  setOperationAction(ISD::BR_JT, MVT::Other, Expand);

  // A jump table is a run of 16 bit block addresses that BR_JT indexes into
  // and jumps through; the generic expansion produces exactly the shift, add,
  // load and JMPI the machine wants.

  setOperationAction(ISD::GlobalAddress, MVT::i16, Custom);
  setOperationAction(ISD::BlockAddress, MVT::i16, Custom);
  setOperationAction(ISD::ExternalSymbol, MVT::i16, Custom);
  setOperationAction(ISD::JumpTable, MVT::i16, Custom);

  setOperationAction(ISD::VASTART, MVT::Other, Custom);
  setOperationAction(ISD::VAARG, MVT::Other, Expand);
  setOperationAction(ISD::VAEND, MVT::Other, Expand);
  setOperationAction(ISD::VACOPY, MVT::Other, Expand);

  setOperationAction(ISD::FRAMEADDR, MVT::i16, Custom);
  // The return address lives on the hardware stack, which is not addressable.
  setOperationAction(ISD::RETURNADDR, MVT::i16, Expand);

  setOperationAction(ISD::DYNAMIC_STACKALLOC, MVT::i16, Expand);
  setOperationAction(ISD::STACKSAVE, MVT::Other, Expand);
  setOperationAction(ISD::STACKRESTORE, MVT::Other, Expand);

  setOperationAction(ISD::SIGN_EXTEND_INREG, MVT::i1, Expand);

  // __builtin_mul_overflow.  MUL and MULU set V exactly when the product will
  // not fit in a word, which is the question being asked, so this is answered
  // out of the flags instead of by working it out again.
  setOperationAction(ISD::SMULO, MVT::i16, Custom);
  setOperationAction(ISD::UMULO, MVT::i16, Custom);

  // llvm.trap and llvm.debugtrap.  The default for the first is a call to
  // abort(), which on a part with no operating system is a call to something
  // that may not be linked; the default for the second is no lowering at all,
  // so __builtin_debugtrap() was a compile error.  Both are a TRAP here: see
  // the patterns in C166InstrInfo.td for which vector and why.
  setOperationAction(ISD::TRAP, MVT::Other, Legal);
  setOperationAction(ISD::DEBUGTRAP, MVT::Other, Legal);

  // [Rw+] reads and then steps the pointer past what it read, which is what
  // walking an array wants.  There is no matching post-incrementing store:
  // the only auto-stepping store form is the pre-decrementing [-Rw].
  setIndexedLoadAction(ISD::POST_INC, MVT::i8, Legal);
  setIndexedLoadAction(ISD::POST_INC, MVT::i16, Legal);

  setTargetDAGCombine(ISD::ADD);
  setTargetDAGCombine(
      {ISD::SDIV, ISD::UDIV, ISD::SREM, ISD::UREM, ISD::ADDE, ISD::SUBE});

  // A far pointer is an i32, which is not a legal type, so an access through
  // one is caught while the type legalizer is expanding the pointer operand.
  // Ordinary i32 loads and stores come through the same hook and are handed
  // straight back for the generic expansion.
  setOperationAction(ISD::LOAD, MVT::i32, Custom);
  setOperationAction(ISD::STORE, MVT::i32, Custom);
  setOperationAction(ISD::ADDRSPACECAST, MVT::i16, Custom);
  setOperationAction(ISD::ADDRSPACECAST, MVT::i32, Custom);

  // Naming a symbol that lives in the far address space needs relocations for
  // its segment that the object writer does not have yet.  Catching it here
  // turns what would be a type legalizer crash into a diagnostic.
  setOperationAction(ISD::GlobalAddress, MVT::i32, Custom);
  setOperationAction(ISD::BlockAddress, MVT::i32, Custom);
}

//===----------------------------------------------------------------------===//
// Custom lowering
//===----------------------------------------------------------------------===//

SDValue C166TargetLowering::LowerOperation(SDValue Op,
                                           SelectionDAG &DAG) const {
  switch (Op.getOpcode()) {
  case ISD::GlobalAddress:
    return LowerGlobalAddress(Op, DAG);
  case ISD::BlockAddress:
    return LowerBlockAddress(Op, DAG);
  case ISD::ExternalSymbol:
    return LowerExternalSymbol(Op, DAG);
  case ISD::JumpTable:
    return LowerJumpTable(Op, DAG);
  case ISD::BR_CC:
    return LowerBR_CC(Op, DAG);
  case ISD::SETCC:
    return LowerSETCC(Op, DAG);
  case ISD::SMULO:
  case ISD::UMULO:
    return LowerXMULO(Op, DAG);
  case ISD::SELECT_CC:
    return LowerSELECT_CC(Op, DAG);
  case ISD::FRAMEADDR:
    return LowerFRAMEADDR(Op, DAG);
  case ISD::VASTART:
    return LowerVASTART(Op, DAG);
  case ISD::LOAD:
    return LowerLOAD(Op, DAG);
  case ISD::STORE:
    return LowerSTORE(Op, DAG);
  case ISD::ADDRSPACECAST:
    return LowerADDRSPACECAST(Op, DAG);
  case ISD::SHL_PARTS:
  case ISD::SRL_PARTS:
  case ISD::SRA_PARTS:
    return LowerShiftParts(Op, DAG);
  default:
    llvm_unreachable("unimplemented operation lowering");
  }
}

/// A shift of a value two words wide by an amount only known at run time.
///
/// The machine shifts a word by a count in a register, so this is a handful of
/// instructions rather than the call to __ashlsi3 it used to be.  Two things
/// shape it.
///
/// The count register is read as its low four bits, so a shift by sixteen is a
/// shift by nothing.  That is a trap in the obvious expansion, which wants to
/// bring the bits that cross between the halves over with a shift by
/// "16 - amount": at an amount of zero that is a shift by sixteen, and instead
/// of contributing nothing it would contribute the whole word.  Shifting by
/// "15 - amount" and then once more gets it right at zero without asking, and
/// is the same everywhere else.
///
/// The masking is useful in the other direction, though.  When the amount is
/// sixteen or more the result is one half shifted by "amount - 16", and since
/// the hardware is already reading the count modulo sixteen, that is the same
/// instruction as the one the small case needs.  So it is computed once and
/// used twice.
SDValue C166TargetLowering::LowerShiftParts(SDValue Op,
                                            SelectionDAG &DAG) const {
  SDLoc DL(Op);
  SDValue Lo = Op.getOperand(0);
  SDValue Hi = Op.getOperand(1);
  SDValue Amt = Op.getOperand(2);
  unsigned Opc = Op.getOpcode();
  EVT VT = Lo.getValueType();

  SDValue Fifteen = DAG.getConstant(15, DL, VT);
  SDValue One = DAG.getConstant(1, DL, VT);
  SDValue Zero = DAG.getConstant(0, DL, VT);

  // Whether the shift moves a whole word across, which is bit 4 of the amount.
  SDValue IsWide =
      DAG.getSetCC(DL, getSetCCResultType(DAG.getDataLayout(), *DAG.getContext(),
                                          VT),
                   DAG.getNode(ISD::AND, DL, VT, Amt,
                               DAG.getConstant(16, DL, VT)),
                   Zero, ISD::SETNE);

  // The bits that leave one half and arrive in the other, shifted the long way
  // round so that an amount of zero brings nothing over.
  SDValue Lack = DAG.getNode(ISD::SUB, DL, VT, Fifteen, Amt);

  SDValue OutLo, OutHi;
  if (Opc == ISD::SHL_PARTS) {
    SDValue ShLo = DAG.getNode(ISD::SHL, DL, VT, Lo, Amt);
    SDValue Cross = DAG.getNode(ISD::SRL, DL, VT,
                                DAG.getNode(ISD::SRL, DL, VT, Lo, Lack), One);
    SDValue NearHi = DAG.getNode(ISD::OR, DL, VT,
                                 DAG.getNode(ISD::SHL, DL, VT, Hi, Amt), Cross);
    // Wide: everything left in the result came from the low half, and the low
    // half itself is empty.
    OutLo = DAG.getSelect(DL, VT, IsWide, Zero, ShLo);
    OutHi = DAG.getSelect(DL, VT, IsWide, ShLo, NearHi);
  } else {
    bool Arithmetic = Opc == ISD::SRA_PARTS;
    unsigned HiOpc = Arithmetic ? ISD::SRA : ISD::SRL;
    SDValue ShHi = DAG.getNode(HiOpc, DL, VT, Hi, Amt);
    SDValue Cross = DAG.getNode(ISD::SHL, DL, VT,
                                DAG.getNode(ISD::SHL, DL, VT, Hi, Lack), One);
    SDValue NearLo = DAG.getNode(ISD::OR, DL, VT,
                                 DAG.getNode(ISD::SRL, DL, VT, Lo, Amt), Cross);
    // Wide: the low half comes from the high one.  What is left above it is
    // zero for a logical shift, and the sign for an arithmetic one, which is
    // the high half shifted right by fifteen.
    SDValue WideHi =
        Arithmetic ? DAG.getNode(ISD::SRA, DL, VT, Hi, Fifteen) : Zero;
    OutLo = DAG.getSelect(DL, VT, IsWide, ShHi, NearLo);
    OutHi = DAG.getSelect(DL, VT, IsWide, WideHi, ShHi);
  }

  return DAG.getMergeValues({OutLo, OutHi}, DL);
}

/// A function placed in another segment has to be entered with CALLS and left
/// with RETS, which is a different frame layout on the hardware stack, so it
/// can only be reached by name.
static bool isFarFunction(const GlobalValue *GV) {
  const auto *F = dyn_cast<Function>(GV);
  return F && F->hasFnAttribute("far");
}

SDValue C166TargetLowering::LowerGlobalAddress(SDValue Op,
                                               SelectionDAG &DAG) const {
  auto *N = cast<GlobalAddressSDNode>(Op);
  EVT PtrVT = Op.getValueType();

  // The callee of a direct call never reaches here, so anything that does is
  // the address of the function being taken.  There is no far indirect call:
  // CALLI stays inside the current segment and RETS would pop a segment that
  // was never pushed.
  if (isFarFunction(N->getGlobal()))
    DAG.getContext()->diagnose(DiagnosticInfoUnsupported(
        DAG.getMachineFunction().getFunction(),
        "cannot take the address of a far function; it can only be called by "
        "name",
        SDLoc(Op).getDebugLoc()));

  SDValue Result = DAG.getTargetGlobalAddress(N->getGlobal(), SDLoc(Op), PtrVT,
                                              N->getOffset());
  return DAG.getNode(C166ISD::Wrapper, SDLoc(Op), PtrVT, Result);
}

SDValue C166TargetLowering::LowerBlockAddress(SDValue Op,
                                              SelectionDAG &DAG) const {
  const BlockAddress *BA = cast<BlockAddressSDNode>(Op)->getBlockAddress();
  EVT PtrVT = Op.getValueType();
  SDValue Result = DAG.getTargetBlockAddress(BA, PtrVT);
  return DAG.getNode(C166ISD::Wrapper, SDLoc(Op), PtrVT, Result);
}

SDValue C166TargetLowering::LowerExternalSymbol(SDValue Op,
                                                SelectionDAG &DAG) const {
  const char *Sym = cast<ExternalSymbolSDNode>(Op)->getSymbol();
  EVT PtrVT = Op.getValueType();
  SDValue Result = DAG.getTargetExternalSymbol(Sym, PtrVT);
  return DAG.getNode(C166ISD::Wrapper, SDLoc(Op), PtrVT, Result);
}

SDValue C166TargetLowering::LowerJumpTable(SDValue Op,
                                           SelectionDAG &DAG) const {
  auto *JT = cast<JumpTableSDNode>(Op);
  EVT PtrVT = Op.getValueType();
  SDValue Result = DAG.getTargetJumpTable(JT->getIndex(), PtrVT);
  return DAG.getNode(C166ISD::Wrapper, SDLoc(Op), PtrVT, Result);
}

/// Translate an ISD condition code into the matching C166 condition code.
/// C166 has the full set of signed and unsigned conditions, so every integer
/// predicate maps onto exactly one of them.
static C166CC::CondCode translateCondCode(ISD::CondCode CC) {
  switch (CC) {
  default:
    llvm_unreachable("Invalid integer condition code");
  case ISD::SETEQ:
    return C166CC::COND_Z;
  case ISD::SETNE:
    return C166CC::COND_NZ;
  case ISD::SETULT:
    return C166CC::COND_ULT;
  case ISD::SETULE:
    return C166CC::COND_ULE;
  case ISD::SETUGT:
    return C166CC::COND_UGT;
  case ISD::SETUGE:
    return C166CC::COND_UGE;
  case ISD::SETLT:
    return C166CC::COND_SLT;
  case ISD::SETLE:
    return C166CC::COND_SLE;
  case ISD::SETGT:
    return C166CC::COND_SGT;
  case ISD::SETGE:
    return C166CC::COND_SGE;
  }
}

/// Put the comparison into the shape the CMP instruction wants - only its
/// right hand operand can be an immediate - and produce the C166 condition
/// code for it.
static SDValue prepareCompare(SDValue &LHS, SDValue &RHS, ISD::CondCode CC,
                              const SDLoc &DL, SelectionDAG &DAG) {
  assert(!LHS.getValueType().isFloatingPoint() &&
         "C166 has no floating point unit");

  if (isa<ConstantSDNode>(LHS)) {
    std::swap(LHS, RHS);
    CC = ISD::getSetCCSwappedOperands(CC);
  }

  return DAG.getConstant(translateCondCode(CC), DL, MVT::i8);
}

SDValue C166TargetLowering::LowerBR_CC(SDValue Op, SelectionDAG &DAG) const {
  SDValue Chain = Op.getOperand(0);
  ISD::CondCode CC = cast<CondCodeSDNode>(Op.getOperand(1))->get();
  SDValue LHS = Op.getOperand(2);
  SDValue RHS = Op.getOperand(3);
  SDValue Dest = Op.getOperand(4);
  SDLoc DL(Op);

  SDValue TargetCC = prepareCompare(LHS, RHS, CC, DL, DAG);

  return DAG.getNode(C166ISD::BR_CC, DL, Op.getValueType(), Chain, Dest, LHS,
                     RHS, TargetCC);
}

/// __builtin_mul_overflow on a word, as one multiply and a branch on its
/// overflow flag.  The node produces the product and the flag as two words;
/// the flag is narrowed to the i1 the caller asked for.
SDValue C166TargetLowering::LowerXMULO(SDValue Op, SelectionDAG &DAG) const {
  SDLoc DL(Op);
  bool IsSigned = Op.getOpcode() == ISD::SMULO;
  SDVTList VTs = DAG.getVTList(MVT::i16, MVT::i16);
  SDValue Res = DAG.getNode(IsSigned ? C166ISD::SMULO : C166ISD::UMULO, DL, VTs,
                            Op.getOperand(0), Op.getOperand(1));
  // Type legalization has already been over this, so the overflow result is a
  // word by the time it gets here rather than the i1 the intrinsic declares.
  // The inserter produces exactly zero or one, which is what
  // ZeroOrOneBooleanContent promises, so nothing has to be masked.
  EVT OvfVT = Op->getValueType(1);
  SDValue Ovf = Res.getValue(1);
  if (OvfVT != MVT::i16)
    Ovf = DAG.getNode(ISD::TRUNCATE, DL, OvfVT, Ovf);
  return DAG.getMergeValues({Res.getValue(0), Ovf}, DL);
}

SDValue C166TargetLowering::LowerSETCC(SDValue Op, SelectionDAG &DAG) const {
  SDValue LHS = Op.getOperand(0);
  SDValue RHS = Op.getOperand(1);
  ISD::CondCode CC = cast<CondCodeSDNode>(Op.getOperand(2))->get();
  SDLoc DL(Op);
  EVT VT = Op.getValueType();

  SDValue TargetCC = prepareCompare(LHS, RHS, CC, DL, DAG);

  SDValue Ops[] = {DAG.getConstant(1, DL, VT), DAG.getConstant(0, DL, VT), LHS,
                   RHS, TargetCC};
  return DAG.getNode(C166ISD::SELECT_CC, DL, VT, Ops);
}

SDValue C166TargetLowering::LowerSELECT_CC(SDValue Op,
                                           SelectionDAG &DAG) const {
  SDValue LHS = Op.getOperand(0);
  SDValue RHS = Op.getOperand(1);
  SDValue TrueV = Op.getOperand(2);
  SDValue FalseV = Op.getOperand(3);
  ISD::CondCode CC = cast<CondCodeSDNode>(Op.getOperand(4))->get();
  SDLoc DL(Op);

  SDValue TargetCC = prepareCompare(LHS, RHS, CC, DL, DAG);

  SDValue Ops[] = {TrueV, FalseV, LHS, RHS, TargetCC};
  return DAG.getNode(C166ISD::SELECT_CC, DL, Op.getValueType(), Ops);
}

SDValue C166TargetLowering::LowerFRAMEADDR(SDValue Op,
                                           SelectionDAG &DAG) const {
  MachineFrameInfo &MFI = DAG.getMachineFunction().getFrameInfo();
  MFI.setFrameAddressIsTaken(true);

  EVT VT = Op.getValueType();
  SDLoc DL(Op);
  unsigned Depth = Op.getConstantOperandVal(0);
  SDValue FrameAddr =
      DAG.getCopyFromReg(DAG.getEntryNode(), DL, C166::R1, VT);
  while (Depth--)
    FrameAddr = DAG.getLoad(VT, DL, DAG.getEntryNode(), FrameAddr,
                            MachinePointerInfo());
  return FrameAddr;
}

SDValue C166TargetLowering::LowerVASTART(SDValue Op, SelectionDAG &DAG) const {
  MachineFunction &MF = DAG.getMachineFunction();
  auto *FuncInfo = MF.getInfo<C166MachineFunctionInfo>();

  SDValue Ptr = Op.getOperand(1);
  EVT PtrVT = Ptr.getValueType();

  SDValue FrameIndex =
      DAG.getFrameIndex(FuncInfo->getVarArgsFrameIndex(), PtrVT);
  const Value *SV = cast<SrcValueSDNode>(Op.getOperand(2))->getValue();

  return DAG.getStore(Op.getOperand(0), SDLoc(Op), FrameIndex, Ptr,
                      MachinePointerInfo(SV));
}

//===----------------------------------------------------------------------===//
// Far (segmented) accesses
//===----------------------------------------------------------------------===//

// A far pointer holds a linear 24 bit address: bits 15-0 are the offset within
// a segment and bits 23-16 are the segment number.  The access itself is an
// ordinary 16 bit one preceded by an EXTS naming the segment, so the pointer
// is handed to the pseudo as two separate halves.  Both nodes built here are
// still i32 and get expanded when the type legalizer revisits them.
static void splitFarPointer(SDValue Ptr, const SDLoc &DL, SelectionDAG &DAG,
                            SDValue &Offset, SDValue &Segment) {
  Offset = DAG.getNode(ISD::TRUNCATE, DL, MVT::i16, Ptr);
  SDValue High = DAG.getNode(ISD::SRL, DL, MVT::i32, Ptr,
                             DAG.getShiftAmountConstant(16, MVT::i32, DL));
  Segment = DAG.getNode(ISD::TRUNCATE, DL, MVT::i16, High);
}

/// Work out how wide a single far access has to be, or return false if this
/// one is not something a lone EXTS plus MOV can do.  Anything wider than a
/// word is split by the generic legalizer first and comes back here as its
/// halves; an i1 is held in a byte just as a promoting load would have it.
static bool getFarAccessType(EVT MemVT, MVT &AccessVT) {
  if (!MemVT.isScalarInteger())
    return false;
  unsigned Bits = MemVT.getSizeInBits();
  if (Bits > 16)
    return false;
  AccessVT = Bits > 8 ? MVT::i16 : MVT::i8;
  return true;
}

/// [Rw+] steps the pointer by the width of the access and nothing else, so a
/// load only folds an increment of exactly two for a word or one for a byte.
bool C166TargetLowering::getPostIndexedAddressParts(SDNode *N, SDNode *Op,
                                                    SDValue &Base,
                                                    SDValue &Offset,
                                                    ISD::MemIndexedMode &AM,
                                                    SelectionDAG &DAG) const {
  auto *LD = dyn_cast<LoadSDNode>(N);
  if (!LD || LD->getExtensionType() != ISD::NON_EXTLOAD)
    return false;

  // A far pointer is 32 bits wide and its accesses go through an EXTS, which
  // this form has no room for.
  if (LD->getAddressSpace() != C166AS::Near)
    return false;

  EVT VT = LD->getMemoryVT();
  if (VT != MVT::i8 && VT != MVT::i16)
    return false;

  if (Op->getOpcode() != ISD::ADD)
    return false;

  auto *Step = dyn_cast<ConstantSDNode>(Op->getOperand(1));
  if (!Step || Step->getZExtValue() != (VT == MVT::i16 ? 2u : 1u))
    return false;

  Base = Op->getOperand(0);
  Offset = Op->getOperand(1);
  AM = ISD::POST_INC;
  return true;
}

/// Whether a far access can be reached at all on the part being built for.
///
/// A far address is a segment and an offset, and the only way to hand a
/// segment to an ordinary MOV is the EXTS in front of it.  The first
/// generation of the family has no EXTS, and there is no sequence that stands
/// in for one: rewriting a data page pointer would reach the object, but it
/// would also silently redirect every near address that shares that pointer,
/// including the ones an interrupt taken in the middle would use.
///
/// So this is a diagnostic rather than a fallback, and the caller drops the
/// access afterwards - continuing to select it would put an EXTS through the
/// asm printer's predicate check, which reports a rather less helpful thing.
static bool diagnoseFarWithoutExtInstr(const C166Subtarget &Subtarget,
                                       SelectionDAG &DAG, const SDLoc &DL,
                                       StringRef What) {
  if (Subtarget.hasExtInstr())
    return false;
  DAG.getContext()->diagnose(DiagnosticInfoUnsupported(
      DAG.getMachineFunction().getFunction(),
      "cannot reach a far object on this part: " + What +
          " needs an EXTS, which the first generation of the family does not "
          "have; -mcpu=c167 or later has it",
      DL.getDebugLoc()));
  return true;
}

SDValue C166TargetLowering::LowerLOAD(SDValue Op, SelectionDAG &DAG) const {
  auto *LD = cast<LoadSDNode>(Op);
  MVT AccessVT;
  if (LD->getAddressSpace() != C166AS::Far || !LD->isUnindexed() ||
      !getFarAccessType(LD->getMemoryVT(), AccessVT))
    return SDValue();

  SDLoc DL(Op);
  if (diagnoseFarWithoutExtInstr(Subtarget, DAG, DL, "a load"))
    return DAG.getMergeValues(
        {DAG.getPOISON(LD->getValueType(0)), LD->getChain()}, DL);

  SDValue Offset, Segment;
  splitFarPointer(LD->getBasePtr(), DL, DAG, Offset, Segment);

  SDValue Ops[] = {LD->getChain(), Offset, Segment};
  SDValue Load = DAG.getMemIntrinsicNode(
      C166ISD::FAR_LOAD, DL, DAG.getVTList(AccessVT, MVT::Other), Ops,
      LD->getMemoryVT(), LD->getMemOperand());

  SDValue Value = Load;
  EVT VT = LD->getValueType(0);
  if (VT != AccessVT) {
    unsigned Opc = ISD::ANY_EXTEND;
    if (LD->getExtensionType() == ISD::ZEXTLOAD)
      Opc = ISD::ZERO_EXTEND;
    else if (LD->getExtensionType() == ISD::SEXTLOAD)
      Opc = ISD::SIGN_EXTEND;
    Value = DAG.getNode(Opc, DL, VT, Load);
  }
  return DAG.getMergeValues({Value, Load.getValue(1)}, DL);
}

SDValue C166TargetLowering::LowerSTORE(SDValue Op, SelectionDAG &DAG) const {
  auto *ST = cast<StoreSDNode>(Op);
  MVT AccessVT;
  if (ST->getAddressSpace() != C166AS::Far || !ST->isUnindexed() ||
      !getFarAccessType(ST->getMemoryVT(), AccessVT))
    return SDValue();

  SDLoc DL(Op);
  if (diagnoseFarWithoutExtInstr(Subtarget, DAG, DL, "a store"))
    return ST->getChain();

  SDValue Offset, Segment;
  splitFarPointer(ST->getBasePtr(), DL, DAG, Offset, Segment);

  SDValue Value = ST->getValue();
  if (Value.getValueType() != AccessVT)
    Value = DAG.getNode(ISD::TRUNCATE, DL, AccessVT, Value);

  SDValue Ops[] = {ST->getChain(), Value, Offset, Segment};
  return DAG.getMemIntrinsicNode(C166ISD::FAR_STORE, DL,
                                 DAG.getVTList(MVT::Other), Ops,
                                 ST->getMemoryVT(), ST->getMemOperand());
}

/// One DIV answers both "a / b" and "a % b", so a function that asks for both
/// should issue one rather than two.
///
/// The generic combiner declines to form DIVREM when plain division is legal,
/// on the reasoning that a target with a legal divide is better served by the
/// ordinary expansion.  That reasoning does not hold here.  DIV is the slowest
/// instruction on the core, and it leaves the quotient in MDL and the
/// remainder in MDH whether or not anything wants the second one, so pairing
/// them costs a register move and saves an entire divide.
static SDValue combineDivRem(SDNode *N, TargetLowering::DAGCombinerInfo &DCI) {
  unsigned Opc = N->getOpcode();
  bool IsSigned = Opc == ISD::SDIV || Opc == ISD::SREM;
  bool IsDiv = Opc == ISD::SDIV || Opc == ISD::UDIV;
  unsigned SiblingOpc = IsDiv ? (IsSigned ? ISD::SREM : ISD::UREM)
                              : (IsSigned ? ISD::SDIV : ISD::UDIV);
  unsigned DivRemOpc = IsSigned ? ISD::SDIVREM : ISD::UDIVREM;

  EVT VT = N->getValueType(0);
  if (VT != MVT::i16)
    return SDValue();

  // Find the other half of the pair: same operands, opposite opcode.
  SDValue Op0 = N->getOperand(0), Op1 = N->getOperand(1);
  SDNode *Sibling = nullptr;
  for (SDNode *User : Op0->users()) {
    if (User->getOpcode() != SiblingOpc || User->use_empty() ||
        User->getOperand(0) != Op0 || User->getOperand(1) != Op1)
      continue;
    Sibling = User;
    break;
  }
  if (!Sibling)
    return SDValue();

  SelectionDAG &DAG = DCI.DAG;
  SDLoc DL(N);
  SDValue Pair = DAG.getNode(DivRemOpc, DL,
                             DAG.getVTList(MVT::i16, MVT::i16), Op0, Op1);
  // Replace the sibling here; returning only covers the node being visited.
  DCI.CombineTo(Sibling, Pair.getValue(IsDiv ? 1 : 0));
  return Pair.getValue(IsDiv ? 0 : 1);
}

/// "acc += a * b" and "acc -= a * b" with a and b words, onto the MAC unit.
///
/// By the time this runs the widening multiply is an SMUL_LOHI or a UMUL_LOHI
/// and the 32 bit add is the ADDC and ADDE pair that carries between the two
/// words, so what is matched is the ADDE: its carry has to come from the ADDC
/// of the other half, and both halves have to be the two results of the same
/// multiply.  A subtraction is the same shape in SUBC and SUBE, and picks the
/// negating form of the instruction.
///
/// Which multiply the type legalizer built is what says whether the product is
/// signed, and so which of CoMAC and CoMACu this is.  There is no node for a
/// mixed sign widening multiply and the legalizer does not make one - a value
/// zero extended from a word has sixteen sign bits, one short of what its
/// check for a signed pair wants - so CoMACsu and CoMACus have no shape here
/// to match and are left to hand written assembly.
///
/// The multiply has to feed nothing else.  If either half is used again the
/// MAC would compute the product a second time to hand it over, which costs
/// more than it saves and is why the uses are counted rather than assumed.
static SDValue combineMAC(SDNode *N, TargetLowering::DAGCombinerInfo &DCI,
                          const C166Subtarget &ST) {
  if (!ST.hasMAC() || N->getValueType(0) != MVT::i16)
    return SDValue();

  // Subtraction does not commute, so on that side the product has to be the
  // right hand operand of both halves rather than either one.
  bool Negate = N->getOpcode() == ISD::SUBE;
  unsigned LowOpc = Negate ? ISD::SUBC : ISD::ADDC;

  SDValue AccHi = N->getOperand(0), MulHi = N->getOperand(1);
  if (!Negate && MulHi.getOpcode() != ISD::SMUL_LOHI &&
      MulHi.getOpcode() != ISD::UMUL_LOHI)
    std::swap(AccHi, MulHi);
  bool Unsigned = MulHi.getOpcode() == ISD::UMUL_LOHI;
  if (!Unsigned && MulHi.getOpcode() != ISD::SMUL_LOHI)
    return SDValue();
  if (MulHi.getResNo() != 1)
    return SDValue();

  SDNode *AddC = N->getOperand(2).getNode();
  if (AddC->getOpcode() != LowOpc)
    return SDValue();
  SDValue AccLo = AddC->getOperand(0), MulLo = AddC->getOperand(1);
  if (!Negate && MulLo.getNode() != MulHi.getNode())
    std::swap(AccLo, MulLo);
  if (MulLo.getNode() != MulHi.getNode() || MulLo.getResNo() != 0)
    return SDValue();

  SDNode *Mul = MulHi.getNode();
  if (!Mul->hasNUsesOfValue(1, 0) || !Mul->hasNUsesOfValue(1, 1))
    return SDValue();
  // The low sum goes to whatever wanted it and the carry to this node; if the
  // ADDC is feeding anything else its carry is being read twice over.
  if (!AddC->hasNUsesOfValue(1, 1))
    return SDValue();

  // A carry out of the high word means this is part of something wider than
  // 32 bits, and the MAC produces no carry to continue it with.
  if (N->hasAnyUseOfValue(1))
    return SDValue();

  unsigned Kind = Negate ? (Unsigned ? C166MAC::UnsignedNegate
                                     : C166MAC::SignedNegate)
                         : (Unsigned ? C166MAC::Unsigned : C166MAC::Signed);

  SelectionDAG &DAG = DCI.DAG;
  SDLoc DL(N);

  // What looks like an accumulate onto the constant 00 0000 8000H, with the
  // low word of the sum thrown away, is "(a * b + 0x8000) >> 16" - rounding a
  // fixed point product to its high word.  The unit does that in one
  // instruction: the rounding forms add 00 0000 8000H and clear MAL, leaving
  // the answer in MAH alone.  Six instructions become two.
  //
  // It is exactly the C answer and not merely close, because the addend
  // cannot carry out of thirty two bits: a signed 16 by 16 product reaches
  // 2^30 and an unsigned one 2^32 - 2^17, so neither wraps once 0x8000 is
  // added.  That argument is what confines this to a bare product.  With a
  // real accumulator in play the forty bit accumulator can hold a sum that a
  // thirty two bit one would have wrapped, and reading MAH alone - with no
  // CoSTORE of MAL to truncate through - would then differ from what the
  // program asked for.  So an accumulating round is left alone.
  auto isConstant = [](SDValue V, uint64_t Want) {
    auto *C = dyn_cast<ConstantSDNode>(V);
    return C && C->getAPIntValue() == Want;
  };
  if (!Negate && isConstant(AccLo, 0x8000) && isConstant(AccHi, 0) &&
      !AddC->hasAnyUseOfValue(0)) {
    SDValue Rnd = DAG.getNode(
        C166ISD::MUL_RND, DL, MVT::i16, Mul->getOperand(0), Mul->getOperand(1),
        DAG.getTargetConstant(Kind, DL, MVT::i16));
    DAG.ReplaceAllUsesOfValueWith(SDValue(N, 0), Rnd);
    return SDValue(N, 0);
  }
  SDValue MAC = DAG.getNode(C166ISD::MAC, DL,
                            DAG.getVTList(MVT::i16, MVT::i16), AccLo, AccHi,
                            Mul->getOperand(0), Mul->getOperand(1),
                            DAG.getTargetConstant(Kind, DL, MVT::i16));
  // Both words are replaced by hand.  Returning one of them would not do:
  // these two nodes each carry a glue result besides their sum, and the
  // combiner's own replacement is for a node with a single value.
  DAG.ReplaceAllUsesOfValueWith(SDValue(AddC, 0), MAC.getValue(0));
  DAG.ReplaceAllUsesOfValueWith(SDValue(N, 0), MAC.getValue(1));
  return SDValue(N, 0);
}

/// A constant added to the address of a far object is part of that address
/// rather than arithmetic on it: the relocations carry an addend, so the
/// linker can do the adding.  Without this the offset survives to become a
/// real 32 bit add, since the near path only folds one at selection time and
/// by then a far address has been split into its two halves.
SDValue C166TargetLowering::PerformDAGCombine(SDNode *N,
                                              DAGCombinerInfo &DCI) const {
  switch (N->getOpcode()) {
  case ISD::SDIV:
  case ISD::UDIV:
  case ISD::SREM:
  case ISD::UREM:
    return combineDivRem(N, DCI);
  case ISD::ADDE:
  case ISD::SUBE:
    return combineMAC(N, DCI, Subtarget);
  default:
    break;
  }

  if (N->getOpcode() != ISD::ADD)
    return SDValue();

  auto *GA = dyn_cast<GlobalAddressSDNode>(N->getOperand(0));
  auto *Offset = dyn_cast<ConstantSDNode>(N->getOperand(1));
  if (!GA || !Offset || GA->getAddressSpace() != C166AS::Far)
    return SDValue();

  return DCI.DAG.getGlobalAddress(GA->getGlobal(), SDLoc(N), N->getValueType(0),
                                  GA->getOffset() + Offset->getSExtValue());
}

// The address of a symbol in the far address space is only known once the
// linker has placed it, so it is built from two relocations: one for the
// segment and one for the offset within it.
SDValue C166TargetLowering::LowerFarSymbol(SDValue Op,
                                           SelectionDAG &DAG) const {
  SDLoc DL(Op);
  SDValue Seg, Sof;

  if (auto *GA = dyn_cast<GlobalAddressSDNode>(Op)) {
    Seg = DAG.getTargetGlobalAddress(GA->getGlobal(), DL, MVT::i16,
                                     GA->getOffset(), C166II::MO_SEG);
    Sof = DAG.getTargetGlobalAddress(GA->getGlobal(), DL, MVT::i16,
                                     GA->getOffset(), C166II::MO_SOF);
  } else {
    auto *BA = cast<BlockAddressSDNode>(Op);
    Seg = DAG.getTargetBlockAddress(BA->getBlockAddress(), MVT::i16,
                                    BA->getOffset(), C166II::MO_SEG);
    Sof = DAG.getTargetBlockAddress(BA->getBlockAddress(), MVT::i16,
                                    BA->getOffset(), C166II::MO_SOF);
  }

  Seg = DAG.getNode(C166ISD::Wrapper, DL, MVT::i16, Seg);
  Sof = DAG.getNode(C166ISD::Wrapper, DL, MVT::i16, Sof);
  return DAG.getNode(ISD::BUILD_PAIR, DL, MVT::i32, Sof, Seg);
}

SDValue C166TargetLowering::LowerADDRSPACECAST(SDValue Op,
                                               SelectionDAG &DAG) const {
  // Narrowing a far pointer back to a near one keeps the offset and drops the
  // segment; see ReplaceNodeResults() for the other direction.
  SDValue Src = Op.getOperand(0);
  if (Op.getValueType().bitsGE(Src.getValueType()))
    return SDValue();
  return DAG.getNode(ISD::TRUNCATE, SDLoc(Op), Op.getValueType(), Src);
}

void C166TargetLowering::ReplaceNodeResults(SDNode *N,
                                            SmallVectorImpl<SDValue> &Results,
                                            SelectionDAG &DAG) const {
  // Leaving Results empty asks the type legalizer to carry on with its own
  // expansion, which is what the ordinary i32 loads and stores that share the
  // custom action with the far ones want.
  switch (N->getOpcode()) {
  default:
    return;
  case ISD::ADDRSPACECAST:
    // Widening a near pointer into a far one assumes the reset configuration
    // of the data page pointers, in which a 16 bit address maps onto the
    // identical physical address in segment zero.
    Results.push_back(DAG.getNode(ISD::ZERO_EXTEND, SDLoc(N),
                                  N->getValueType(0), N->getOperand(0)));
    return;
  case ISD::GlobalAddress:
  case ISD::BlockAddress:
    Results.push_back(LowerFarSymbol(SDValue(N, 0), DAG));
    return;
  case ISD::UDIV:
  case ISD::UREM:
    ReplaceDivBy16Results(N, Results, DAG);
    return;
  case ISD::SDIV:
  case ISD::SREM:
    ReplaceSignedDivBy16Results(N, Results, DAG);
    return;
  case ISD::SMAX:
  case ISD::SMIN:
    ReplaceMinMaxResults(N, Results, DAG);
    return;
  }
}

/// A 32 bit signed minimum or maximum, as one comparison in the coprocessor.
///
/// CoLOAD puts the first operand in the accumulator, sign extended from 32
/// bits into 40; CoMIN or CoMAX replaces it with whichever of the two is
/// wanted, comparing all 40 bits signed; and the two CoSTOREs bring the answer
/// back. The extension is what makes the 40 bit comparison the 32 bit one the
/// program asked for, and the truncation on the way out cannot lose anything,
/// because the result is one of the two operands and both came from 32 bits.
///
/// Saturation cannot interfere. The manual says the MS bit of MCW does not
/// affect CoMIN or CoMAX at all, and CoLOAD saturates only on a 32 bit
/// overflow, which a value that started as 32 bits cannot cause.
void C166TargetLowering::ReplaceMinMaxResults(
    SDNode *N, SmallVectorImpl<SDValue> &Results, SelectionDAG &DAG) const {
  if (N->getValueType(0) != MVT::i32)
    return;

  SDLoc DL(N);
  auto Split = [&](SDValue V) {
    return std::make_pair(
        DAG.getNode(ISD::EXTRACT_ELEMENT, DL, MVT::i16, V,
                    DAG.getIntPtrConstant(0, DL)),
        DAG.getNode(ISD::EXTRACT_ELEMENT, DL, MVT::i16, V,
                    DAG.getIntPtrConstant(1, DL)));
  };
  auto [ALo, AHi] = Split(N->getOperand(0));
  auto [BLo, BHi] = Split(N->getOperand(1));

  unsigned Kind = N->getOpcode() == ISD::SMAX ? C166MAC::Max : C166MAC::Min;
  SDValue R = DAG.getNode(C166ISD::MINMAX, DL,
                          DAG.getVTList(MVT::i16, MVT::i16), ALo, AHi, BLo, BHi,
                          DAG.getTargetConstant(Kind, DL, MVT::i16));
  Results.push_back(DAG.getNode(ISD::BUILD_PAIR, DL, MVT::i32, R.getValue(0),
                                R.getValue(1)));
}

/// A 32 bit unsigned division whose divisor came from 16 bits, as two divides
/// rather than a call to __udivsi3.
///
/// The dividend is split at the word boundary and divided in two steps.  The
/// first divides the high word, which DIVU does on its own; the second divides
/// the remainder of that, carried in MDH, together with the low word, which is
/// what DIVLU is for.  Neither step can overflow, and that is the whole reason
/// the pair is safe to emit: the first is a 16 by 16 divide, whose quotient is
/// at most the dividend, and the second divides r:lo by d with r < d, so its
/// quotient is below 65536.  DIVLU on a general 32 bit dividend has no such
/// guarantee, which is why nothing here tries to use it for a divisor that did
/// not start out 16 bits wide.
void C166TargetLowering::ReplaceDivBy16Results(
    SDNode *N, SmallVectorImpl<SDValue> &Results, SelectionDAG &DAG) const {
  if (N->getValueType(0) != MVT::i32)
    return;

  // Only a divisor that is genuinely 16 bits.  Asking what is known about the
  // high word rather than looking for a zero extension is what catches the
  // shapes a front end actually produces: "a / b" and "a % b" written next to
  // each other leaves the divisor behind a freeze, and a masked value never
  // had an extension to find in the first place.
  SDValue Divisor = N->getOperand(1);
  if (!DAG.MaskedValueIsZero(Divisor, APInt::getHighBitsSet(32, 16)))
    return;

  SDLoc DL(N);
  SDValue D = DAG.getNode(ISD::TRUNCATE, DL, MVT::i16, Divisor);
  SDValue N32 = N->getOperand(0);
  SDValue Lo = DAG.getNode(ISD::EXTRACT_ELEMENT, DL, MVT::i16, N32,
                           DAG.getIntPtrConstant(0, DL));
  SDValue Hi = DAG.getNode(ISD::EXTRACT_ELEMENT, DL, MVT::i16, N32,
                           DAG.getIntPtrConstant(1, DL));

  SDVTList VTs = DAG.getVTList(MVT::i16, MVT::i16, MVT::i16);
  SDValue Div = DAG.getNode(C166ISD::UDIVREM32BY16, DL, VTs, Lo, Hi, D);

  if (N->getOpcode() == ISD::UDIV) {
    Results.push_back(DAG.getNode(ISD::BUILD_PAIR, DL, MVT::i32,
                                  Div.getValue(0), Div.getValue(1)));
    return;
  }
  // The remainder is below the divisor, so it is a word and the high half of
  // the answer is zero.
  Results.push_back(DAG.getNode(ISD::BUILD_PAIR, DL, MVT::i32, Div.getValue(2),
                                DAG.getConstant(0, DL, MVT::i16)));
}

/// The signed form of the above: magnitudes through the same two divides, with
/// the signs put back afterwards.
///
/// Both magnitudes are representable, which is the thing to check before
/// believing this works at the edges.  The dividend's is at most 2^31, which
/// is a 32 bit value; the divisor's is at most 32768, which is a 16 bit one.
/// So -2147483648 / -32768 goes through as 2147483648 / 32768 and comes back
/// as 65536, and nothing along the way needs a bit it does not have.
///
/// It is not done when the function is being compiled for size.  The sign
/// handling is around twenty instructions on top of the seven the division
/// itself takes, against the four bytes of a call, and unlike the unsigned
/// case the library routine does not disappear from the image - the compiler
/// still emits calls to it from anywhere this did not fire.
void C166TargetLowering::ReplaceSignedDivBy16Results(
    SDNode *N, SmallVectorImpl<SDValue> &Results, SelectionDAG &DAG) const {
  if (N->getValueType(0) != MVT::i32 || DAG.shouldOptForSize())
    return;

  // A divisor that is genuinely a signed 16 bit value: everything above bit 15
  // repeats bit 15.
  SDValue Divisor = N->getOperand(1);
  if (DAG.ComputeNumSignBits(Divisor) < 17)
    return;

  SDLoc DL(N);
  SDValue N32 = N->getOperand(0);

  // The sign of each operand as a mask of all ones or all zeroes, which is
  // what turns "negate if negative" into an exclusive or and a subtract.
  SDValue NSign = DAG.getNode(ISD::SRA, DL, MVT::i32, N32,
                              DAG.getShiftAmountConstant(31, MVT::i32, DL));
  SDValue D16 = DAG.getNode(ISD::TRUNCATE, DL, MVT::i16, Divisor);
  SDValue DSign = DAG.getNode(ISD::SRA, DL, MVT::i16, D16,
                              DAG.getShiftAmountConstant(15, MVT::i16, DL));

  SDValue NAbs = DAG.getNode(ISD::SUB, DL, MVT::i32,
                             DAG.getNode(ISD::XOR, DL, MVT::i32, N32, NSign),
                             NSign);
  SDValue DAbs = DAG.getNode(ISD::SUB, DL, MVT::i16,
                             DAG.getNode(ISD::XOR, DL, MVT::i16, D16, DSign),
                             DSign);

  SDValue Lo = DAG.getNode(ISD::EXTRACT_ELEMENT, DL, MVT::i16, NAbs,
                           DAG.getIntPtrConstant(0, DL));
  SDValue Hi = DAG.getNode(ISD::EXTRACT_ELEMENT, DL, MVT::i16, NAbs,
                           DAG.getIntPtrConstant(1, DL));
  SDVTList VTs = DAG.getVTList(MVT::i16, MVT::i16, MVT::i16);
  SDValue Div = DAG.getNode(C166ISD::UDIVREM32BY16, DL, VTs, Lo, Hi, DAbs);

  if (N->getOpcode() == ISD::SDIV) {
    // The quotient is negative when the operands' signs differ.
    SDValue DSign32 = DAG.getNode(ISD::SIGN_EXTEND, DL, MVT::i32, DSign);
    SDValue QSign = DAG.getNode(ISD::XOR, DL, MVT::i32, NSign, DSign32);
    SDValue Q = DAG.getNode(ISD::BUILD_PAIR, DL, MVT::i32, Div.getValue(0),
                            Div.getValue(1));
    Results.push_back(
        DAG.getNode(ISD::SUB, DL, MVT::i32,
                    DAG.getNode(ISD::XOR, DL, MVT::i32, Q, QSign), QSign));
    return;
  }

  // The remainder takes the dividend's sign.  Its magnitude is below the
  // divisor's, so at most 32767, and it is a signed word before it is widened.
  SDValue RSign = DAG.getNode(ISD::TRUNCATE, DL, MVT::i16, NSign);
  SDValue R = DAG.getNode(
      ISD::SUB, DL, MVT::i16,
      DAG.getNode(ISD::XOR, DL, MVT::i16, Div.getValue(2), RSign), RSign);
  Results.push_back(DAG.getNode(ISD::SIGN_EXTEND, DL, MVT::i32, R));
}

//===----------------------------------------------------------------------===//
// Miscellaneous target hooks
//===----------------------------------------------------------------------===//

bool C166TargetLowering::isTruncateFree(Type *Ty1, Type *Ty2) const {
  if (!Ty1->isIntegerTy() || !Ty2->isIntegerTy())
    return false;
  return Ty1->getPrimitiveSizeInBits() == 16 &&
         Ty2->getPrimitiveSizeInBits() == 8;
}

bool C166TargetLowering::isTruncateFree(EVT VT1, EVT VT2) const {
  if (!VT1.isInteger() || !VT2.isInteger())
    return false;
  return VT1 == MVT::i16 && VT2 == MVT::i8;
}

bool C166TargetLowering::isLegalICmpImmediate(int64_t Imm) const {
  // CMP reg, #data16 takes any 16 bit value.
  return isInt<16>(Imm) || isUInt<16>(Imm);
}

bool C166TargetLowering::isLegalAddImmediate(int64_t Imm) const {
  return isInt<16>(Imm) || isUInt<16>(Imm);
}

TargetLowering::ConstraintType
C166TargetLowering::getConstraintType(StringRef Constraint) const {
  if (Constraint.size() == 1) {
    switch (Constraint[0]) {
    case 'r':
    case 'q':
      return C_RegisterClass;
    default:
      break;
    }
  }
  return TargetLowering::getConstraintType(Constraint);
}

std::pair<unsigned, const TargetRegisterClass *>
C166TargetLowering::getRegForInlineAsmConstraint(const TargetRegisterInfo *TRI,
                                                 StringRef Constraint,
                                                 MVT VT) const {
  if (Constraint.size() == 1) {
    switch (Constraint[0]) {
    case 'r':
      if (VT == MVT::i8)
        return std::make_pair(0U, &C166::GR8RegClass);
      return std::make_pair(0U, &C166::GR16RegClass);
    // R0 to R3, which is as far as the two bit pointer field of an indirect
    // ALU form reaches.
    case 'q':
      return std::make_pair(0U, &C166::GR16PRegClass);
    default:
      break;
    }
  }
  return TargetLowering::getRegForInlineAsmConstraint(TRI, Constraint, VT);
}

/// A compare and exchange, held together by clearing PSW.IEN rather than by
/// ATOMIC: it stores only when what it read matched, so it branches, and a
/// branch is what an ATOMIC sequence cannot contain.
///
///     mov  save, psw          what IEN was, along with the flags
///     bclr psw.11             interrupts off
///     mov  old, [addr]
///     cmp  old, cmp
///     jmpr cc_NE, out
///     mov  [addr], new
///   out:
///     mov  psw, save          exactly what it was, IEN included
///
/// Putting the whole word back rather than the one bit is a single
/// instruction, and the flags it also restores are dead by then: the only
/// reader was the branch above.
/// Whether \p Ptr is a far pointer, which no atomic here can reach.
///
/// Every one of these sequences holds together because of what surrounds it,
/// and a far access does not fit inside either kind of surround: reaching one
/// needs an EXTend, and the hardware keeps a single instruction counter that
/// an ATOMIC sequence is already using.  Saying so is better than the type
/// legalizer falling over on the 32 bit pointer, and much better than code
/// that looks atomic and is not.
static bool diagnoseFarAtomic(const Instruction *I, const Value *Ptr,
                              StringRef What) {
  if (Ptr->getType()->getPointerAddressSpace() != C166AS::Far)
    return false;
  I->getContext().diagnose(DiagnosticInfoUnsupported(
      *I->getFunction(),
      "cannot make a far access atomic: " + What +
          " on a far pointer needs an EXTend, which an ATOMIC sequence has no "
          "instruction counter left for",
      I->getDebugLoc()));
  return true;
}

TargetLowering::AtomicExpansionKind
C166TargetLowering::shouldExpandAtomicCmpXchgInIR(const AtomicCmpXchgInst *CI) const {
  if (diagnoseFarAtomic(CI, CI->getPointerOperand(), "a compare and exchange"))
    return AtomicExpansionKind::NotAtomic;
  return AtomicExpansionKind::None;
}

TargetLowering::AtomicExpansionKind
C166TargetLowering::shouldExpandAtomicLoadInIR(LoadInst *LI) const {
  if (diagnoseFarAtomic(LI, LI->getPointerOperand(), "a load"))
    return AtomicExpansionKind::NotAtomic;
  return AtomicExpansionKind::None;
}

TargetLowering::AtomicExpansionKind
C166TargetLowering::shouldExpandAtomicStoreInIR(StoreInst *SI) const {
  if (diagnoseFarAtomic(SI, SI->getPointerOperand(), "a store"))
    return AtomicExpansionKind::NotAtomic;
  return AtomicExpansionKind::None;
}

/// Which read-modify-writes the ATOMIC sequence can carry.
///
/// The sequence is a read, a copy, the change and a write back, and ATOMIC
/// reaches four instructions, so an operation the machine does in one
/// instruction fits and nothing else does.  NAND is an AND and a complement,
/// and the minimum and maximum need a comparison and a choice; those go round
/// a compare and exchange loop instead, which is one instruction longer per
/// turn but has no count to overrun.
TargetLowering::AtomicExpansionKind
C166TargetLowering::shouldExpandAtomicRMWInIR(const AtomicRMWInst *AI) const {
  // Reported, then left as an ordinary read-modify-write so that code
  // generation has something it can finish; the error stops the compile
  // before any of it is written out.
  if (diagnoseFarAtomic(AI, AI->getPointerOperand(), "a read-modify-write"))
    return AtomicExpansionKind::NotAtomic;

  // A first generation part has no ATOMIC, so none of these fit in a sequence
  // it can hold.  They all go round the compare and exchange loop instead,
  // which is what the minimum and maximum already do here and which reaches
  // indivisibility by clearing PSW.IEN rather than by counting instructions -
  // an idiom this part does have.  Longer per turn, and correct.
  if (!Subtarget.hasExtInstr())
    return AtomicExpansionKind::CmpXChg;

  switch (AI->getOperation()) {
  case AtomicRMWInst::Add:
  case AtomicRMWInst::Sub:
  case AtomicRMWInst::And:
  case AtomicRMWInst::Or:
  case AtomicRMWInst::Xor:
  case AtomicRMWInst::Xchg:
    return AtomicExpansionKind::None;
  default:
    return AtomicExpansionKind::CmpXChg;
  }
}

/// One multiply, and the overflow taken out of the flag it already set.
///
/// MUL and MULU both set V when the product will not fit in a word, so the
/// answer __builtin_mul_overflow wants is a branch on cc_V.  What the generic
/// expansion does instead is read MDH back and, for the signed case, sign
/// extend MDL to compare against it - four instructions to recompute what the
/// multiply already recorded.
///
///   thisMBB:
///     %one = MOV #1
///     MUL / MULU a, b            ; sets V
///     %prod = MOV MDL            ; row "* * - - *", so V survives it
///     JMPR cc_V, sinkMBB
///   falseMBB:
///     %zero = MOV #0
///   sinkMBB:
///     %ovf = phi [ %zero, falseMBB ], [ %one, thisMBB ]
///
/// The constant one is materialised ahead of the multiply rather than after
/// it, so that nothing stands between the MUL and the branch except the move
/// out of MDL.
MachineBasicBlock *
C166TargetLowering::emitMulOverflow(MachineInstr &MI,
                                    MachineBasicBlock *BB) const {
  const TargetInstrInfo &TII = *BB->getParent()->getSubtarget().getInstrInfo();
  MachineRegisterInfo &MRI = BB->getParent()->getRegInfo();
  DebugLoc DL = MI.getDebugLoc();
  bool Unsigned = MI.getOpcode() == C166::UMULO16;

  Register Prod = MI.getOperand(0).getReg();
  Register Ovf = MI.getOperand(1).getReg();
  Register A = MI.getOperand(2).getReg();
  Register B = MI.getOperand(3).getReg();

  const BasicBlock *LLVMBB = BB->getBasicBlock();
  MachineFunction::iterator It = ++BB->getIterator();
  MachineFunction *MF = BB->getParent();
  MachineBasicBlock *ThisMBB = BB;
  MachineBasicBlock *FalseMBB = MF->CreateMachineBasicBlock(LLVMBB);
  MachineBasicBlock *SinkMBB = MF->CreateMachineBasicBlock(LLVMBB);
  MF->insert(It, FalseMBB);
  MF->insert(It, SinkMBB);

  unsigned CallFrameSize = TII.getCallFrameSizeAt(MI);
  FalseMBB->setCallFrameSize(CallFrameSize);
  SinkMBB->setCallFrameSize(CallFrameSize);

  SinkMBB->splice(SinkMBB->begin(), BB,
                  std::next(MachineBasicBlock::iterator(MI)), BB->end());
  SinkMBB->transferSuccessorsAndUpdatePHIs(BB);

  Register One = MRI.createVirtualRegister(&C166::GR16RegClass);
  Register Zero = MRI.createVirtualRegister(&C166::GR16RegClass);

  BuildMI(BB, DL, TII.get(C166::MOV16ri4), One).addImm(1);
  BuildMI(BB, DL, TII.get(Unsigned ? C166::MULUrr : C166::MULrr))
      .addReg(A)
      .addReg(B);
  BuildMI(BB, DL, TII.get(C166::MOVfromMDL), Prod);
  BuildMI(BB, DL, TII.get(C166::JMPRcc)).addMBB(SinkMBB).addImm(C166CC::COND_V);

  BB->addSuccessor(FalseMBB);
  BB->addSuccessor(SinkMBB);

  BuildMI(FalseMBB, DL, TII.get(C166::MOV16ri4), Zero).addImm(0);
  FalseMBB->addSuccessor(SinkMBB);

  BuildMI(*SinkMBB, SinkMBB->begin(), DL, TII.get(C166::PHI), Ovf)
      .addReg(Zero)
      .addMBB(FalseMBB)
      .addReg(One)
      .addMBB(ThisMBB);

  MI.eraseFromParent();
  return SinkMBB;
}

MachineBasicBlock *
C166TargetLowering::emitCmpXchg(MachineInstr &MI, MachineBasicBlock *BB) const {
  bool Byte = MI.getOpcode() == C166::CMPXCHG8;
  const TargetSubtargetInfo &STI = BB->getParent()->getSubtarget();
  const TargetInstrInfo &TII = *STI.getInstrInfo();
  const TargetRegisterInfo &TRI = *STI.getRegisterInfo();
  MachineRegisterInfo &MRI = BB->getParent()->getRegInfo();
  DebugLoc DL = MI.getDebugLoc();

  Register Dst = MI.getOperand(0).getReg();
  Register Addr = MI.getOperand(1).getReg();
  Register Cmp = MI.getOperand(2).getReg();
  Register New = MI.getOperand(3).getReg();

  // PSW carries its own short address as its encoding.  For a bit addressable
  // register that short address is its bitoff, and the address it is reached
  // by as memory is derived from it - which is how a move to or from it is
  // written, since there is no register to register form that can name an SFR.
  unsigned PSWShort = TRI.getEncodingValue(C166::PSW);
  unsigned PSWAddr = C166::getSFRAddressForShort(PSWShort);
  const unsigned IEN = 11;

  const BasicBlock *LLVMBB = BB->getBasicBlock();
  MachineFunction::iterator It = ++BB->getIterator();
  MachineFunction *MF = BB->getParent();
  MachineBasicBlock *StoreMBB = MF->CreateMachineBasicBlock(LLVMBB);
  MachineBasicBlock *OutMBB = MF->CreateMachineBasicBlock(LLVMBB);
  MF->insert(It, StoreMBB);
  MF->insert(It, OutMBB);

  unsigned CallFrameSize = TII.getCallFrameSizeAt(MI);
  StoreMBB->setCallFrameSize(CallFrameSize);
  OutMBB->setCallFrameSize(CallFrameSize);

  OutMBB->splice(OutMBB->begin(), BB,
                 std::next(MachineBasicBlock::iterator(MI)), BB->end());
  OutMBB->transferSuccessorsAndUpdatePHIs(BB);

  BB->addSuccessor(StoreMBB);
  BB->addSuccessor(OutMBB);
  StoreMBB->addSuccessor(OutMBB);

  Register Save = MRI.createVirtualRegister(&C166::GR16RegClass);
  BuildMI(BB, DL, TII.get(C166::MOV16ra), Save).addImm(PSWAddr);
  BuildMI(BB, DL, TII.get(C166::BCLR)).addImm(PSWShort).addImm(IEN);
  BuildMI(BB, DL, TII.get(Byte ? C166::MOVB8rp : C166::MOV16rp), Dst)
      .addReg(Addr);
  BuildMI(BB, DL, TII.get(Byte ? C166::CMPB8rr : C166::CMP16rr))
      .addReg(Dst)
      .addReg(Cmp);
  BuildMI(BB, DL, TII.get(C166::JMPRcc))
      .addMBB(OutMBB)
      .addImm(C166CC::COND_NZ);

  BuildMI(StoreMBB, DL, TII.get(Byte ? C166::MOVB8pr : C166::MOV16pr))
      .addReg(Addr)
      .addReg(New);

  BuildMI(*OutMBB, OutMBB->begin(), DL, TII.get(C166::MOV16ar))
      .addImm(PSWAddr)
      .addReg(Save);

  MI.eraseFromParent();
  return OutMBB;
}

MachineBasicBlock *
C166TargetLowering::EmitInstrWithCustomInserter(MachineInstr &MI,
                                                MachineBasicBlock *BB) const {
  if (MI.getOpcode() == C166::CMPXCHG16 || MI.getOpcode() == C166::CMPXCHG8)
    return emitCmpXchg(MI, BB);

  if (MI.getOpcode() == C166::SMULO16 || MI.getOpcode() == C166::UMULO16)
    return emitMulOverflow(MI, BB);

  // The comparison is a word or a byte one depending on what is being
  // compared, not on what is being selected, and its right hand side is
  // either a register or a constant.
  bool CompareIsByte;
  switch (MI.getOpcode()) {
  default:
    llvm_unreachable("Unexpected instruction for the custom inserter");
  case C166::Select16_16:
  case C166::Select8_16:
  case C166::Select16_16i:
  case C166::Select8_16i:
    CompareIsByte = false;
    break;
  case C166::Select16_8:
  case C166::Select8_8:
  case C166::Select16_8i:
  case C166::Select8_8i:
    CompareIsByte = true;
    break;
  }

  unsigned BranchOpc;
  if (MI.getOperand(4).isImm())
    BranchOpc = CompareIsByte ? C166::BRCC8ri : C166::BRCC16ri;
  else
    BranchOpc = CompareIsByte ? C166::BRCC8rr : C166::BRCC16rr;

  const TargetInstrInfo &TII = *BB->getParent()->getSubtarget().getInstrInfo();
  DebugLoc DL = MI.getDebugLoc();

  // Expand the select into a diamond:
  //
  //   thisMBB:
  //     cmp lhs, rhs / jmpa cc, sinkMBB     (still fused at this point)
  //   falseMBB:
  //     ; fall through
  //   sinkMBB:
  //     %dst = phi [ %false, falseMBB ], [ %true, thisMBB ]
  const BasicBlock *LLVMBB = BB->getBasicBlock();
  MachineFunction::iterator It = ++BB->getIterator();

  MachineBasicBlock *ThisMBB = BB;
  MachineFunction *MF = BB->getParent();
  MachineBasicBlock *FalseMBB = MF->CreateMachineBasicBlock(LLVMBB);
  MachineBasicBlock *SinkMBB = MF->CreateMachineBasicBlock(LLVMBB);
  MF->insert(It, FalseMBB);
  MF->insert(It, SinkMBB);

  // The select may sit inside a call frame setup/destroy pair; the new blocks
  // inherit its call frame size.
  unsigned CallFrameSize = TII.getCallFrameSizeAt(MI);
  FalseMBB->setCallFrameSize(CallFrameSize);
  SinkMBB->setCallFrameSize(CallFrameSize);

  SinkMBB->splice(SinkMBB->begin(), BB,
                  std::next(MachineBasicBlock::iterator(MI)), BB->end());
  SinkMBB->transferSuccessorsAndUpdatePHIs(BB);

  BB->addSuccessor(FalseMBB);
  BB->addSuccessor(SinkMBB);

  BuildMI(BB, DL, TII.get(BranchOpc))
      .addMBB(SinkMBB)
      .addReg(MI.getOperand(3).getReg())
      .add(MI.getOperand(4))
      .addImm(MI.getOperand(5).getImm());

  FalseMBB->addSuccessor(SinkMBB);

  BuildMI(*SinkMBB, SinkMBB->begin(), DL, TII.get(C166::PHI),
          MI.getOperand(0).getReg())
      .addReg(MI.getOperand(2).getReg())
      .addMBB(FalseMBB)
      .addReg(MI.getOperand(1).getReg())
      .addMBB(ThisMBB);

  MI.eraseFromParent();
  return SinkMBB;
}

//===----------------------------------------------------------------------===//
// Calling convention implementation
//===----------------------------------------------------------------------===//

SDValue C166TargetLowering::LowerFormalArguments(
    SDValue Chain, CallingConv::ID CallConv, bool IsVarArg,
    const SmallVectorImpl<ISD::InputArg> &Ins, const SDLoc &DL,
    SelectionDAG &DAG, SmallVectorImpl<SDValue> &InVals) const {
  switch (CallConv) {
  case CallingConv::C:
  case CallingConv::Fast:
    return LowerCCCArguments(Chain, CallConv, IsVarArg, Ins, DL, DAG, InVals);
  default:
    report_fatal_error("Unsupported calling convention for C166");
  }
}

/// A tail call jumps to the callee with this function's frame already gone, so
/// the callee's RET returns to our caller.  That only works when the two ends
/// agree about how a return happens and when nothing of ours has to survive
/// the jump.
bool C166TargetLowering::isEligibleForTailCall(
    const TargetLowering::CallLoweringInfo &CLI,
    const SmallVectorImpl<CCValAssign> &ArgLocs, unsigned StackSize) const {
  const Function &Caller = CLI.DAG.getMachineFunction().getFunction();

  // An interrupt handler comes back with RETI, which restores the PSW the
  // hardware stacked; a plain RET in the callee would leave it there.
  if (Caller.hasFnAttribute("interrupt"))
    return false;

  // A far function was entered with CALLS and has a code segment on the
  // hardware stack waiting for RETS, so both ends have to be the same kind:
  // a near callee's RET would pop half of what a far caller left there, and a
  // far callee's RETS would pop a segment a near caller never pushed.
  const auto *Callee = dyn_cast<GlobalAddressSDNode>(CLI.Callee);
  bool CallerIsFar = Caller.hasFnAttribute("far");
  bool CalleeIsFar = Callee && isFarFunction(Callee->getGlobal());
  if (CallerIsFar != CalleeIsFar)
    return false;

  // The far jump names its target segment, and only the direct form does;
  // CALLI has no inter-segment counterpart to tail call through.
  if (CallerIsFar && !Callee)
    return false;

  if (CLI.IsVarArg || Caller.isVarArg())
    return false;

  // Outgoing arguments would go where this function's frame is about to stop
  // being, and anything passed by reference into that frame would outlive it.
  if (StackSize != 0)
    return false;
  for (const CCValAssign &VA : ArgLocs)
    if (!VA.isRegLoc())
      return false;
  for (const ISD::OutputArg &Out : CLI.Outs)
    if (Out.Flags.isByVal() || Out.Flags.isSRet() || Out.Flags.isInAlloca())
      return false;

  return true;
}

SDValue C166TargetLowering::LowerCall(TargetLowering::CallLoweringInfo &CLI,
                                      SmallVectorImpl<SDValue> &InVals) const {
  switch (CLI.CallConv) {
  case CallingConv::C:
  case CallingConv::Fast:
    return LowerCCCCallTo(CLI, InVals);
  default:
    report_fatal_error("Unsupported calling convention for C166");
  }
}

SDValue C166TargetLowering::LowerCCCArguments(
    SDValue Chain, CallingConv::ID CallConv, bool IsVarArg,
    const SmallVectorImpl<ISD::InputArg> &Ins, const SDLoc &DL,
    SelectionDAG &DAG, SmallVectorImpl<SDValue> &InVals) const {
  MachineFunction &MF = DAG.getMachineFunction();
  MachineFrameInfo &MFI = MF.getFrameInfo();
  MachineRegisterInfo &RegInfo = MF.getRegInfo();
  auto *FuncInfo = MF.getInfo<C166MachineFunctionInfo>();

  SmallVector<CCValAssign, 16> ArgLocs;
  CCState CCInfo(CallConv, IsVarArg, MF, ArgLocs, *DAG.getContext());
  CCInfo.AnalyzeFormalArguments(Ins, IsVarArg ? CC_C166_VarArg : CC_C166);

  // The first vararg starts right behind the named arguments.
  if (IsVarArg)
    FuncInfo->setVarArgsFrameIndex(
        MFI.CreateFixedObject(1, CCInfo.getStackSize(), true));

  for (unsigned I = 0, E = ArgLocs.size(); I != E; ++I) {
    CCValAssign &VA = ArgLocs[I];

    if (VA.isRegLoc()) {
      EVT RegVT = VA.getLocVT();
      assert(RegVT == MVT::i16 && "Arguments are passed in word registers");

      Register VReg = RegInfo.createVirtualRegister(&C166::GR16RegClass);
      RegInfo.addLiveIn(VA.getLocReg(), VReg);
      SDValue ArgValue = DAG.getCopyFromReg(Chain, DL, VReg, RegVT);

      // Narrow values are passed promoted to a full word.
      if (VA.getLocInfo() == CCValAssign::SExt)
        ArgValue = DAG.getNode(ISD::AssertSext, DL, RegVT, ArgValue,
                               DAG.getValueType(VA.getValVT()));
      else if (VA.getLocInfo() == CCValAssign::ZExt)
        ArgValue = DAG.getNode(ISD::AssertZext, DL, RegVT, ArgValue,
                               DAG.getValueType(VA.getValVT()));

      if (VA.getLocInfo() != CCValAssign::Full)
        ArgValue = DAG.getNode(ISD::TRUNCATE, DL, VA.getValVT(), ArgValue);

      InVals.push_back(ArgValue);
      continue;
    }

    assert(VA.isMemLoc() && "Argument is neither in a register nor in memory");

    ISD::ArgFlagsTy Flags = Ins[I].Flags;
    if (Flags.isByVal()) {
      int FI = MFI.CreateFixedObject(Flags.getByValSize(),
                                     VA.getLocMemOffset(), true);
      InVals.push_back(DAG.getFrameIndex(FI, MVT::i16));
      continue;
    }

    unsigned ObjSize = VA.getLocVT().getStoreSize();
    int FI = MFI.CreateFixedObject(ObjSize, VA.getLocMemOffset(), true);
    SDValue FIN = DAG.getFrameIndex(FI, MVT::i16);
    SDValue InVal = DAG.getLoad(
        VA.getLocVT(), DL, Chain, FIN,
        MachinePointerInfo::getFixedStack(MF, FI));

    if (VA.getLocInfo() != CCValAssign::Full)
      InVal = DAG.getNode(ISD::TRUNCATE, DL, VA.getValVT(), InVal);

    InVals.push_back(InVal);
  }

  // Remember the register holding the hidden struct return pointer so that it
  // can be handed back to the caller.
  for (unsigned I = 0, E = ArgLocs.size(); I != E; ++I) {
    if (!Ins[I].Flags.isSRet())
      continue;

    Register Reg = FuncInfo->getSRetReturnReg();
    if (!Reg) {
      Reg = MF.getRegInfo().createVirtualRegister(getRegClassFor(MVT::i16));
      FuncInfo->setSRetReturnReg(Reg);
    }
    SDValue Copy = DAG.getCopyToReg(DAG.getEntryNode(), DL, Reg, InVals[I]);
    Chain = DAG.getNode(ISD::TokenFactor, DL, MVT::Other, Copy, Chain);
  }

  return Chain;
}

SDValue
C166TargetLowering::LowerCCCCallTo(TargetLowering::CallLoweringInfo &CLI,
                                   SmallVectorImpl<SDValue> &InVals) const {
  SelectionDAG &DAG = CLI.DAG;
  const SDLoc &DL = CLI.DL;
  SDValue Chain = CLI.Chain;
  SDValue Callee = CLI.Callee;
  CallingConv::ID CallConv = CLI.CallConv;
  bool IsVarArg = CLI.IsVarArg;
  const SmallVectorImpl<ISD::OutputArg> &Outs = CLI.Outs;
  const SmallVectorImpl<SDValue> &OutVals = CLI.OutVals;
  const SmallVectorImpl<ISD::InputArg> &Ins = CLI.Ins;
  MachineFunction &MF = DAG.getMachineFunction();

  SmallVector<CCValAssign, 16> ArgLocs;
  CCState CCInfo(CallConv, IsVarArg, MF, ArgLocs, *DAG.getContext());
  CCInfo.AnalyzeCallOperands(Outs, IsVarArg ? CC_C166_VarArg : CC_C166);

  unsigned NumBytes = CCInfo.getStackSize();
  MVT PtrVT = getFrameIndexTy(DAG.getDataLayout());

  if (CLI.IsTailCall && !isEligibleForTailCall(CLI, ArgLocs, NumBytes))
    CLI.IsTailCall = false;

  if (!CLI.IsTailCall)
    Chain = DAG.getCALLSEQ_START(Chain, NumBytes, 0, DL);

  SmallVector<std::pair<unsigned, SDValue>, 4> RegsToPass;
  SmallVector<SDValue, 8> MemOpChains;
  SDValue StackPtr;

  for (unsigned I = 0, E = ArgLocs.size(); I != E; ++I) {
    CCValAssign &VA = ArgLocs[I];
    SDValue Arg = OutVals[I];

    switch (VA.getLocInfo()) {
    case CCValAssign::Full:
      break;
    case CCValAssign::SExt:
      Arg = DAG.getNode(ISD::SIGN_EXTEND, DL, VA.getLocVT(), Arg);
      break;
    case CCValAssign::ZExt:
      Arg = DAG.getNode(ISD::ZERO_EXTEND, DL, VA.getLocVT(), Arg);
      break;
    case CCValAssign::AExt:
      Arg = DAG.getNode(ISD::ANY_EXTEND, DL, VA.getLocVT(), Arg);
      break;
    default:
      llvm_unreachable("Unknown argument location info");
    }

    if (VA.isRegLoc()) {
      RegsToPass.push_back(std::make_pair(VA.getLocReg(), Arg));
      continue;
    }

    assert(VA.isMemLoc());

    if (!StackPtr.getNode())
      StackPtr = DAG.getCopyFromReg(Chain, DL, C166::R0, PtrVT);

    SDValue PtrOff =
        DAG.getNode(ISD::ADD, DL, PtrVT, StackPtr,
                    DAG.getIntPtrConstant(VA.getLocMemOffset(), DL));

    ISD::ArgFlagsTy Flags = Outs[I].Flags;
    if (Flags.isByVal()) {
      SDValue SizeNode = DAG.getConstant(Flags.getByValSize(), DL, MVT::i16);
      Align Alignment = Flags.getNonZeroByValAlign();
      MemOpChains.push_back(DAG.getMemcpy(
          Chain, DL, PtrOff, Arg, SizeNode, Alignment, Alignment,
          /*isVolatile=*/false, /*AlwaysInline=*/true,
          /*CI=*/nullptr, std::nullopt, MachinePointerInfo(),
          MachinePointerInfo()));
    } else {
      MemOpChains.push_back(
          DAG.getStore(Chain, DL, Arg, PtrOff, MachinePointerInfo()));
    }
  }

  if (!MemOpChains.empty())
    Chain = DAG.getNode(ISD::TokenFactor, DL, MVT::Other, MemOpChains);

  SDValue InGlue;
  for (const auto &[Reg, N] : RegsToPass) {
    Chain = DAG.getCopyToReg(Chain, DL, Reg, N, InGlue);
    InGlue = Chain.getValue(1);
  }

  // A far callee is reached with CALLS, which names its segment as well as its
  // offset within that segment; both come from the same symbol.
  bool IsFarCall = false;
  SDValue CalleeSeg;
  if (auto *G = dyn_cast<GlobalAddressSDNode>(Callee)) {
    IsFarCall = isFarFunction(G->getGlobal());
    CalleeSeg = DAG.getTargetGlobalAddress(G->getGlobal(), DL, MVT::i16, 0,
                                           C166II::MO_SEG);
    Callee = DAG.getTargetGlobalAddress(G->getGlobal(), DL, MVT::i16, 0,
                                        IsFarCall ? C166II::MO_SOF
                                                  : C166II::MO_None);
  } else if (auto *E = dyn_cast<ExternalSymbolSDNode>(Callee)) {
    Callee = DAG.getTargetExternalSymbol(E->getSymbol(), MVT::i16);
  }

  SDVTList NodeTys = DAG.getVTList(MVT::Other, MVT::Glue);
  SmallVector<SDValue, 8> Ops;
  Ops.push_back(Chain);
  if (IsFarCall)
    Ops.push_back(CalleeSeg);
  Ops.push_back(Callee);

  for (const auto &[Reg, N] : RegsToPass)
    Ops.push_back(DAG.getRegister(Reg, N.getValueType()));

  const uint32_t *Mask = MF.getSubtarget().getRegisterInfo()->getCallPreservedMask(
      MF, CallConv);
  assert(Mask && "Missing call preserved mask");
  Ops.push_back(DAG.getRegisterMask(Mask));

  if (InGlue.getNode())
    Ops.push_back(InGlue);

  // A tail call is a jump, so it produces no chain for anything to hang off
  // and returns whatever the callee returns straight to our caller.
  if (CLI.IsTailCall)
    return DAG.getNode(IsFarCall ? C166ISD::TC_RETURN_SEG : C166ISD::TC_RETURN,
                       DL, MVT::Other, Ops);

  Chain = DAG.getNode(IsFarCall ? C166ISD::CALL_SEG : C166ISD::CALL, DL,
                      NodeTys, Ops);
  InGlue = Chain.getValue(1);

  Chain = DAG.getCALLSEQ_END(Chain, NumBytes, 0, InGlue, DL);
  InGlue = Chain.getValue(1);

  return LowerCallResult(Chain, InGlue, CallConv, IsVarArg, Ins, DL, DAG,
                         InVals);
}

SDValue C166TargetLowering::LowerCallResult(
    SDValue Chain, SDValue InGlue, CallingConv::ID CallConv, bool IsVarArg,
    const SmallVectorImpl<ISD::InputArg> &Ins, const SDLoc &DL,
    SelectionDAG &DAG, SmallVectorImpl<SDValue> &InVals) const {
  SmallVector<CCValAssign, 16> RVLocs;
  CCState CCInfo(CallConv, IsVarArg, DAG.getMachineFunction(), RVLocs,
                 *DAG.getContext());
  CCInfo.AnalyzeCallResult(Ins, RetCC_C166);

  for (CCValAssign &VA : RVLocs) {
    Chain = DAG.getCopyFromReg(Chain, DL, VA.getLocReg(), VA.getLocVT(), InGlue)
                .getValue(1);
    InGlue = Chain.getValue(2);

    SDValue Val = Chain.getValue(0);
    if (VA.getLocInfo() != CCValAssign::Full)
      Val = DAG.getNode(ISD::TRUNCATE, DL, VA.getValVT(), Val);

    InVals.push_back(Val);
  }

  return Chain;
}

bool C166TargetLowering::CanLowerReturn(
    CallingConv::ID CallConv, MachineFunction &MF, bool IsVarArg,
    const SmallVectorImpl<ISD::OutputArg> &Outs, LLVMContext &Context,
    const Type *RetTy) const {
  SmallVector<CCValAssign, 16> RVLocs;
  CCState CCInfo(CallConv, IsVarArg, MF, RVLocs, Context);
  return CCInfo.CheckReturn(Outs, RetCC_C166);
}

SDValue
C166TargetLowering::LowerReturn(SDValue Chain, CallingConv::ID CallConv,
                                bool IsVarArg,
                                const SmallVectorImpl<ISD::OutputArg> &Outs,
                                const SmallVectorImpl<SDValue> &OutVals,
                                const SDLoc &DL, SelectionDAG &DAG) const {
  MachineFunction &MF = DAG.getMachineFunction();

  SmallVector<CCValAssign, 16> RVLocs;
  CCState CCInfo(CallConv, IsVarArg, MF, RVLocs, *DAG.getContext());
  CCInfo.AnalyzeReturn(Outs, RetCC_C166);

  SDValue Glue;
  SmallVector<SDValue, 4> RetOps(1, Chain);

  for (unsigned I = 0, E = RVLocs.size(); I != E; ++I) {
    CCValAssign &VA = RVLocs[I];
    assert(VA.isRegLoc() && "Return values must live in registers");

    SDValue Val = OutVals[I];
    switch (VA.getLocInfo()) {
    case CCValAssign::Full:
      break;
    case CCValAssign::SExt:
      Val = DAG.getNode(ISD::SIGN_EXTEND, DL, VA.getLocVT(), Val);
      break;
    case CCValAssign::ZExt:
      Val = DAG.getNode(ISD::ZERO_EXTEND, DL, VA.getLocVT(), Val);
      break;
    case CCValAssign::AExt:
      Val = DAG.getNode(ISD::ANY_EXTEND, DL, VA.getLocVT(), Val);
      break;
    default:
      llvm_unreachable("Unknown return value location info");
    }

    Chain = DAG.getCopyToReg(Chain, DL, VA.getLocReg(), Val, Glue);
    Glue = Chain.getValue(1);
    RetOps.push_back(DAG.getRegister(VA.getLocReg(), VA.getLocVT()));
  }

  // A function returning a struct hands the hidden pointer back in R2.
  if (MF.getFunction().hasStructRetAttr()) {
    auto *FuncInfo = MF.getInfo<C166MachineFunctionInfo>();
    Register Reg = FuncInfo->getSRetReturnReg();
    assert(Reg && "sret virtual register was not created in the entry block");

    MVT PtrVT = getFrameIndexTy(DAG.getDataLayout());
    SDValue Val = DAG.getCopyFromReg(Chain, DL, Reg, PtrVT);
    Chain = DAG.getCopyToReg(Chain, DL, C166::R2, Val, Glue);
    Glue = Chain.getValue(1);
    RetOps.push_back(DAG.getRegister(C166::R2, PtrVT));
  }

  RetOps[0] = Chain;
  if (Glue.getNode())
    RetOps.push_back(Glue);

  // An interrupt handler always comes back with RETI, whichever segment it
  // was placed in: the hardware, not a CALLS, put the return address there.
  const Function &F = MF.getFunction();
  unsigned Opc = C166ISD::RET_GLUE;
  if (F.hasFnAttribute("interrupt"))
    Opc = C166ISD::RETI_GLUE;
  else if (F.hasFnAttribute("far"))
    Opc = C166ISD::RETS_GLUE;
  return DAG.getNode(Opc, DL, MVT::Other, RetOps);
}
