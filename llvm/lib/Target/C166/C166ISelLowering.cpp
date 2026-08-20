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
    : TargetLowering(TM, STI) {
  addRegisterClass(MVT::i8, &C166::GR8RegClass);
  addRegisterClass(MVT::i16, &C166::GR16RegClass);

  computeRegisterProperties(STI.getRegisterInfo());

  setStackPointerRegisterToSaveRestore(C166::R0);

  setBooleanContents(ZeroOrOneBooleanContent);
  setBooleanVectorContents(ZeroOrOneBooleanContent);

  setMinFunctionAlignment(Align(2));
  setPrefFunctionAlignment(Align(2));

  // There are no atomic instructions beyond a plain load and store.
  setMaxAtomicSizeInBitsSupported(0);

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

  for (MVT VT : {MVT::i8, MVT::i16}) {
    setOperationAction(ISD::ROTL, VT, VT == MVT::i16 ? Legal : Expand);
    setOperationAction(ISD::ROTR, VT, VT == MVT::i16 ? Legal : Expand);
    setOperationAction(ISD::BSWAP, VT, Expand);
    setOperationAction(ISD::CTTZ, VT, Expand);
    setOperationAction(ISD::CTLZ, VT, Expand);
    setOperationAction(ISD::CTPOP, VT, Expand);
    setOperationAction(ISD::SDIVREM, VT, Expand);
    setOperationAction(ISD::UDIVREM, VT, Expand);
    setOperationAction(ISD::SMUL_LOHI, VT, Expand);
    setOperationAction(ISD::UMUL_LOHI, VT, Expand);
    setOperationAction(ISD::SHL_PARTS, VT, Expand);
    setOperationAction(ISD::SRL_PARTS, VT, Expand);
    setOperationAction(ISD::SRA_PARTS, VT, Expand);
    setOperationAction(ISD::ADDC, VT, Expand);
    setOperationAction(ISD::ADDE, VT, Expand);
    setOperationAction(ISD::SUBC, VT, Expand);
    setOperationAction(ISD::SUBE, VT, Expand);

    setOperationAction(ISD::SETCC, VT, Custom);
    setOperationAction(ISD::SELECT, VT, Expand);
    setOperationAction(ISD::SELECT_CC, VT, Custom);
    setOperationAction(ISD::BR_CC, VT, Custom);
  }

  setOperationAction(ISD::BRCOND, MVT::Other, Expand);
  setOperationAction(ISD::BR_JT, MVT::Other, Expand);

  // Jump tables would need a 16 bit indirect jump through a table in code
  // memory; plain compare-and-branch chains are used instead.
  setMinimumJumpTableEntries(std::numeric_limits<unsigned>::max());

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
  default:
    llvm_unreachable("unimplemented operation lowering");
  }
}

SDValue C166TargetLowering::LowerGlobalAddress(SDValue Op,
                                               SelectionDAG &DAG) const {
  auto *N = cast<GlobalAddressSDNode>(Op);
  EVT PtrVT = Op.getValueType();
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

SDValue C166TargetLowering::LowerLOAD(SDValue Op, SelectionDAG &DAG) const {
  auto *LD = cast<LoadSDNode>(Op);
  MVT AccessVT;
  if (LD->getAddressSpace() != C166AS::Far || !LD->isUnindexed() ||
      !getFarAccessType(LD->getMemoryVT(), AccessVT))
    return SDValue();

  SDLoc DL(Op);
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
    DAG.getContext()->diagnose(DiagnosticInfoUnsupported(
        DAG.getMachineFunction().getFunction(),
        "cannot take the address of a symbol in the far address space; place "
        "the object in the default address space and cast the pointer instead",
        SDLoc(N).getDebugLoc()));
    Results.push_back(DAG.getPOISON(N->getValueType(0)));
    return;
  }
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
  if (Constraint.size() == 1 && Constraint[0] == 'r') {
    if (VT == MVT::i8)
      return std::make_pair(0U, &C166::GR8RegClass);
    return std::make_pair(0U, &C166::GR16RegClass);
  }
  return TargetLowering::getRegForInlineAsmConstraint(TRI, Constraint, VT);
}

MachineBasicBlock *
C166TargetLowering::EmitInstrWithCustomInserter(MachineInstr &MI,
                                                MachineBasicBlock *BB) const {
  unsigned BranchOpc;
  switch (MI.getOpcode()) {
  default:
    llvm_unreachable("Unexpected instruction for the custom inserter");
  case C166::Select16_16:
  case C166::Select8_16:
    BranchOpc = C166::BRCC16rr;
    break;
  case C166::Select16_8:
  case C166::Select8_8:
    BranchOpc = C166::BRCC8rr;
    break;
  }

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
      .addReg(MI.getOperand(4).getReg())
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

SDValue C166TargetLowering::LowerCall(TargetLowering::CallLoweringInfo &CLI,
                                      SmallVectorImpl<SDValue> &InVals) const {
  // Tail calls are not implemented yet.
  CLI.IsTailCall = false;

  switch (CLI.CallConv) {
  case CallingConv::C:
  case CallingConv::Fast:
    return LowerCCCCallTo(CLI.Chain, CLI.Callee, CLI.CallConv, CLI.IsVarArg,
                          CLI.IsTailCall, CLI.Outs, CLI.OutVals, CLI.Ins,
                          CLI.DL, CLI.DAG, InVals);
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

SDValue C166TargetLowering::LowerCCCCallTo(
    SDValue Chain, SDValue Callee, CallingConv::ID CallConv, bool IsVarArg,
    bool IsTailCall, const SmallVectorImpl<ISD::OutputArg> &Outs,
    const SmallVectorImpl<SDValue> &OutVals,
    const SmallVectorImpl<ISD::InputArg> &Ins, const SDLoc &DL,
    SelectionDAG &DAG, SmallVectorImpl<SDValue> &InVals) const {
  MachineFunction &MF = DAG.getMachineFunction();

  SmallVector<CCValAssign, 16> ArgLocs;
  CCState CCInfo(CallConv, IsVarArg, MF, ArgLocs, *DAG.getContext());
  CCInfo.AnalyzeCallOperands(Outs, IsVarArg ? CC_C166_VarArg : CC_C166);

  unsigned NumBytes = CCInfo.getStackSize();
  MVT PtrVT = getFrameIndexTy(DAG.getDataLayout());

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

  if (auto *G = dyn_cast<GlobalAddressSDNode>(Callee))
    Callee = DAG.getTargetGlobalAddress(G->getGlobal(), DL, MVT::i16);
  else if (auto *E = dyn_cast<ExternalSymbolSDNode>(Callee))
    Callee = DAG.getTargetExternalSymbol(E->getSymbol(), MVT::i16);

  SDVTList NodeTys = DAG.getVTList(MVT::Other, MVT::Glue);
  SmallVector<SDValue, 8> Ops;
  Ops.push_back(Chain);
  Ops.push_back(Callee);

  for (const auto &[Reg, N] : RegsToPass)
    Ops.push_back(DAG.getRegister(Reg, N.getValueType()));

  const uint32_t *Mask = MF.getSubtarget().getRegisterInfo()->getCallPreservedMask(
      MF, CallConv);
  assert(Mask && "Missing call preserved mask");
  Ops.push_back(DAG.getRegisterMask(Mask));

  if (InGlue.getNode())
    Ops.push_back(InGlue);

  Chain = DAG.getNode(C166ISD::CALL, DL, NodeTys, Ops);
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

  unsigned Opc = MF.getFunction().hasFnAttribute("interrupt")
                     ? C166ISD::RETI_GLUE
                     : C166ISD::RET_GLUE;
  return DAG.getNode(Opc, DL, MVT::Other, RetOps);
}
