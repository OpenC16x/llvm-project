//===-- C166InstrInfo.h - C166 Instruction Information ----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains the C166 implementation of the TargetInstrInfo class.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_C166_C166INSTRINFO_H
#define LLVM_LIB_TARGET_C166_C166INSTRINFO_H

#include "C166.h"
#include "C166RegisterInfo.h"
#include "llvm/CodeGen/TargetInstrInfo.h"

#define GET_INSTRINFO_HEADER
#include "C166GenInstrInfo.inc"

namespace llvm {

class C166Subtarget;

namespace C166 {
/// The coprocessor instruction a MAC32rr's $kind operand stands for.
///
/// Both places that take a MAC32rr apart - the expansion in
/// C166InstrInfo.cpp and the loop pass that hoists the accumulator - have to
/// agree about this, so it is written once.
inline unsigned getMACOpcode(unsigned Kind) {
  switch (Kind) {
  case C166MAC::Signed:
    return C166::CoMAC_rr;
  case C166MAC::Unsigned:
    return C166::CoMACu_rr;
  case C166MAC::SignedNegate:
    return C166::CoMACN_rr;
  case C166MAC::UnsignedNegate:
    return C166::CoMACuN_rr;
  }
  llvm_unreachable("Unknown C166MAC::Kind");
}

/// The same multiply-accumulate in the form that walks two pointers, which is
/// what a MACREP32 expands to.
///
/// Only the four kinds above have one here, and all four are symmetric in
/// their two operands - both signed or both unsigned - so which stream goes
/// behind IDX0 is free.  That is what lets C166MACRepeat put whichever of the
/// two is in the dual-port RAM there.  CoMACsu and CoMACus would take that
/// freedom away, and there is no kind for them; see combineMAC.
inline unsigned getMACIdxOpcode(unsigned Kind) {
  switch (Kind) {
  case C166MAC::Signed:
    return C166::CoMAC_xp;
  case C166MAC::Unsigned:
    return C166::CoMACu_xp;
  case C166MAC::SignedNegate:
    return C166::CoMACN_xp;
  case C166MAC::UnsignedNegate:
    return C166::CoMACuN_xp;
  }
  llvm_unreachable("Unknown C166MAC::Kind");
}

/// The comparison a MINMAX32rr's $kind operand stands for.
inline unsigned getMinMaxOpcode(unsigned Kind) {
  switch (Kind) {
  case C166MAC::Max:
    return C166::CoMAX_rr;
  case C166MAC::Min:
    return C166::CoMIN_rr;
  }
  llvm_unreachable("Unknown C166MAC::MinMax");
}

/// The rounding multiply a MULRND32rr's $kind operand stands for.
///
/// Only the two plain signs: the rounding idiom this comes from has no
/// accumulator to take a product away from, so the negating kinds never reach
/// here.
inline unsigned getMULRNDOpcode(unsigned Kind) {
  switch (Kind) {
  case C166MAC::Signed:
    return C166::CoMUL_rr_rnd;
  case C166MAC::Unsigned:
    return C166::CoMULu_rr_rnd;
  }
  llvm_unreachable("Unexpected C166MAC::Kind for a rounding multiply");
}
} // end namespace C166

class C166InstrInfo : public C166GenInstrInfo {
  const C166RegisterInfo RI;
  virtual void anchor();

public:
  explicit C166InstrInfo(const C166Subtarget &STI);

  /// TargetInstrInfo is a superset of MRegisterInfo, so whenever a client has
  /// an instance of instruction info it can get register info as well.
  const C166RegisterInfo &getRegisterInfo() const { return RI; }

  void copyPhysReg(MachineBasicBlock &MBB, MachineBasicBlock::iterator I,
                   const DebugLoc &DL, Register DestReg, Register SrcReg,
                   bool KillSrc, bool RenamableDest = false,
                   bool RenamableSrc = false) const override;

  /// Report that \p Reg cannot be copied and why, and put a NOP where the copy
  /// would have gone.  Only an asm statement naming a register by hand can ask
  /// for one of these, so it is a diagnostic rather than an assertion.
  void reportImpossibleCopy(MachineBasicBlock &MBB,
                            MachineBasicBlock::iterator I, const DebugLoc &DL,
                            Register Reg, const Twine &Why) const;

  void storeRegToStackSlot(
      MachineBasicBlock &MBB, MachineBasicBlock::iterator MI, Register SrcReg,
      bool IsKill, int FrameIndex, const TargetRegisterClass *RC, Register VReg,
      MachineInstr::MIFlag Flags = MachineInstr::NoFlags) const override;

  void loadRegFromStackSlot(
      MachineBasicBlock &MBB, MachineBasicBlock::iterator MI, Register DestReg,
      int FrameIndex, const TargetRegisterClass *RC, Register VReg,
      unsigned SubReg = 0,
      MachineInstr::MIFlag Flags = MachineInstr::NoFlags) const override;

  bool expandPostRAPseudo(MachineInstr &MI) const override;

  std::pair<unsigned, unsigned>
  decomposeMachineOperandsTargetFlags(unsigned TF) const override;

  ArrayRef<std::pair<unsigned, const char *>>
  getSerializableDirectMachineOperandTargetFlags() const override;

  // Branch analysis.
  bool
  reverseBranchCondition(SmallVectorImpl<MachineOperand> &Cond) const override;
  bool analyzeBranch(MachineBasicBlock &MBB, MachineBasicBlock *&TBB,
                     MachineBasicBlock *&FBB,
                     SmallVectorImpl<MachineOperand> &Cond,
                     bool AllowModify) const override;
  unsigned removeBranch(MachineBasicBlock &MBB,
                        int *BytesRemoved = nullptr) const override;
  unsigned getInstSizeInBytes(const MachineInstr &MI) const override;

  MachineBasicBlock *getBranchDestBlock(const MachineInstr &MI) const override;

  bool isBranchOffsetInRange(unsigned BranchOpc,
                             int64_t BrOffset) const override;

  void insertIndirectBranch(MachineBasicBlock &MBB, MachineBasicBlock &DestBB,
                            MachineBasicBlock &RestoreBB, const DebugLoc &DL,
                            int64_t BrOffset, RegScavenger *RS) const override;

  unsigned insertBranch(MachineBasicBlock &MBB, MachineBasicBlock *TBB,
                        MachineBasicBlock *FBB, ArrayRef<MachineOperand> Cond,
                        const DebugLoc &DL,
                        int *BytesAdded = nullptr) const override;

  int64_t getFramePoppedByCallee(const MachineInstr &I) const {
    assert(isFrameInstr(I) && "Not a frame instruction");
    assert(I.getOperand(1).getImm() >= 0 && "Size must not be negative");
    return I.getOperand(1).getImm();
  }
};

} // end namespace llvm

#endif
