//===-- C166SelectionDAGInfo.cpp ------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "C166SelectionDAGInfo.h"
#include "C166.h"
#include "llvm/CodeGen/SelectionDAG.h"
#include "llvm/CodeGen/TargetLowering.h"
#include "llvm/IR/DerivedTypes.h"

#define GET_SDNODE_DESC
#include "C166GenSDNodeInfo.inc"

using namespace llvm;

C166SelectionDAGInfo::C166SelectionDAGInfo()
    : SelectionDAGGenTargetInfo(C166GenSDNodeInfo) {}

C166SelectionDAGInfo::~C166SelectionDAGInfo() = default;

// The C library's memcpy and friends take near pointers, so a block move that
// touches the far address space needs its own entry points.  They take far
// pointers throughout: a near operand is widened on the way in, which assumes
// the same reset configuration of the DPP registers that an addrspacecast
// does.  Sizes stay 16 bit, so one call cannot cross more than a segment's
// worth of bytes.
//
//   void *__memcpy_far (void __far *dst, const void __far *src, unsigned n);
//   void *__memmove_far(void __far *dst, const void __far *src, unsigned n);
//   void *__memset_far (void __far *dst, int c, unsigned n);
//
// Only a copy the compiler cannot expand inline ends up here; a constant sized
// one has already been turned into loads and stores by the time this runs.
static bool isFar(const MachinePointerInfo &PtrInfo) {
  return PtrInfo.getAddrSpace() == C166AS::Far;
}

/// Widen a near pointer so that it can be passed to a far entry point.
static SDValue toFarPointer(SelectionDAG &DAG, const SDLoc &DL, SDValue Ptr) {
  if (Ptr.getValueType() == MVT::i32)
    return Ptr;
  return DAG.getNode(ISD::ZERO_EXTEND, DL, MVT::i32, Ptr);
}

static SDValue emitFarLibcall(SelectionDAG &DAG, const SDLoc &DL, SDValue Chain,
                              const char *Name, ArrayRef<SDValue> Ops) {
  const TargetLowering &TLI = DAG.getTargetLoweringInfo();
  LLVMContext &C = *DAG.getContext();

  TargetLowering::ArgListTy Args;
  for (SDValue Op : Ops)
    Args.emplace_back(/*Val=*/nullptr, Op, Op.getValueType().getTypeForEVT(C));

  TargetLowering::CallLoweringInfo CLI(DAG);
  CLI.setDebugLoc(DL)
      .setChain(Chain)
      .setLibCallee(
          TLI.getLibcallCallingConv(RTLIB::MEMCPY), Type::getVoidTy(C),
          DAG.getExternalSymbol(Name, TLI.getPointerTy(DAG.getDataLayout())),
          std::move(Args))
      .setDiscardResult();

  return TLI.LowerCallTo(CLI).second;
}

SDValue C166SelectionDAGInfo::EmitTargetCodeForMemcpy(
    SelectionDAG &DAG, const SDLoc &DL, SDValue Chain, SDValue Dst, SDValue Src,
    SDValue Size, Align DstAlign, Align SrcAlign, bool IsVolatile,
    bool AlwaysInline, MachinePointerInfo DstPtrInfo,
    MachinePointerInfo SrcPtrInfo) const {
  if (AlwaysInline || (!isFar(DstPtrInfo) && !isFar(SrcPtrInfo)))
    return SDValue();
  return emitFarLibcall(
      DAG, DL, Chain, "__memcpy_far",
      {toFarPointer(DAG, DL, Dst), toFarPointer(DAG, DL, Src), Size});
}

SDValue C166SelectionDAGInfo::EmitTargetCodeForMemmove(
    SelectionDAG &DAG, const SDLoc &DL, SDValue Chain, SDValue Dst, SDValue Src,
    SDValue Size, Align DstAlign, Align SrcAlign, bool IsVolatile,
    MachinePointerInfo DstPtrInfo, MachinePointerInfo SrcPtrInfo) const {
  if (!isFar(DstPtrInfo) && !isFar(SrcPtrInfo))
    return SDValue();
  return emitFarLibcall(
      DAG, DL, Chain, "__memmove_far",
      {toFarPointer(DAG, DL, Dst), toFarPointer(DAG, DL, Src), Size});
}

SDValue C166SelectionDAGInfo::EmitTargetCodeForMemset(
    SelectionDAG &DAG, const SDLoc &DL, SDValue Chain, SDValue Dst,
    SDValue Byte, SDValue Size, Align Alignment, bool IsVolatile,
    bool AlwaysInline, MachinePointerInfo DstPtrInfo) const {
  if (AlwaysInline || !isFar(DstPtrInfo))
    return SDValue();
  // The fill value is an int, as it is in the C library.
  SDValue Value = DAG.getZExtOrTrunc(Byte, DL, MVT::i16);
  return emitFarLibcall(DAG, DL, Chain, "__memset_far",
                        {toFarPointer(DAG, DL, Dst), Value, Size});
}
