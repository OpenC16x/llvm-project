//===-- C166FrameLowering.h - Define frame lowering for C166 ----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_C166_C166FRAMELOWERING_H
#define LLVM_LIB_TARGET_C166_C166FRAMELOWERING_H

#include "MCTargetDesc/C166MCTargetDesc.h"
#include "llvm/CodeGen/TargetFrameLowering.h"
#include "llvm/Support/Alignment.h"

namespace llvm {

class C166Subtarget;

/// The C166 has two stacks.  Return addresses (and anything explicitly PUSHed)
/// live on the small hardware stack addressed by the SP special function
/// register.  Everything the ABI cares about - incoming arguments, locals,
/// spill slots and outgoing arguments - lives on a second, "user" stack whose
/// pointer is kept in R0.  R0 is an ordinary general purpose register, so
/// [R0 + #offset] addressing is available for frame accesses.
class C166FrameLowering : public TargetFrameLowering {
protected:
  bool hasFPImpl(const MachineFunction &MF) const override;

public:
  explicit C166FrameLowering(const C166Subtarget &STI)
      : TargetFrameLowering(TargetFrameLowering::StackGrowsDown, Align(2), 0,
                            Align(2)) {}

  void emitPrologue(MachineFunction &MF, MachineBasicBlock &MBB) const override;
  void emitEpilogue(MachineFunction &MF, MachineBasicBlock &MBB) const override;

  /// Where the canonical frame address starts out, which is what the common
  /// information entry says: the ABI stack pointer, with no offset.  A call on
  /// this part leaves its return address on the other stack, so nothing has
  /// been pushed on this one when a function is entered.
  ///
  /// CFIInstrInserter needs these to work out what each block's frame address
  /// really is, and to correct the rule where an epilogue has left one behind
  /// that does not describe the code after it.
  int getInitialCFAOffset(const MachineFunction &MF) const override {
    return 0;
  }
  Register getInitialCFARegister(const MachineFunction &MF) const override {
    return C166::R0;
  }

  MachineBasicBlock::iterator
  eliminateCallFramePseudoInstr(MachineFunction &MF, MachineBasicBlock &MBB,
                                MachineBasicBlock::iterator I) const override;

  void determineCalleeSaves(MachineFunction &MF, BitVector &SavedRegs,
                            RegScavenger *RS) const override;

  /// Say where the callee saved registers went, so that a debugger can put
  /// back the caller's copies rather than only naming the frame.
  void emitCalleeSavedFrameMoves(MachineBasicBlock &MBB,
                                 MachineBasicBlock::iterator MBBI,
                                 const DebugLoc &DL) const;

  /// Emit the rules that say where the return address is and how to get back
  /// to the caller's hardware stack pointer, given that \p Depth bytes have
  /// been pushed onto that stack since the function was entered.
  void emitUnwindRules(MachineBasicBlock &MBB, MachineBasicBlock::iterator MBBI,
                       const DebugLoc &DL, unsigned Depth,
                       MachineInstr::MIFlag Flag) const;

  bool spillCalleeSavedRegisters(MachineBasicBlock &MBB,
                                 MachineBasicBlock::iterator MI,
                                 ArrayRef<CalleeSavedInfo> CSI,
                                 const TargetRegisterInfo *TRI) const override;

  bool hasReservedCallFrame(const MachineFunction &MF) const override;

  /// Frame offsets are stack pointer relative, which is only valid once the
  /// prologue has run.  Keep the prologue in the entry block.
  bool enableShrinkWrapping(const MachineFunction &MF) const override {
    return false;
  }
};

} // end namespace llvm

#endif
