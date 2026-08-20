//===-- C166FrameLowering.h - Define frame lowering for C166 ----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_C166_C166FRAMELOWERING_H
#define LLVM_LIB_TARGET_C166_C166FRAMELOWERING_H

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

  MachineBasicBlock::iterator
  eliminateCallFramePseudoInstr(MachineFunction &MF, MachineBasicBlock &MBB,
                                MachineBasicBlock::iterator I) const override;

  void determineCalleeSaves(MachineFunction &MF, BitVector &SavedRegs,
                            RegScavenger *RS) const override;

  bool hasReservedCallFrame(const MachineFunction &MF) const override;

  /// Frame offsets are stack pointer relative, which is only valid once the
  /// prologue has run.  Keep the prologue in the entry block.
  bool enableShrinkWrapping(const MachineFunction &MF) const override {
    return false;
  }
};

} // end namespace llvm

#endif
