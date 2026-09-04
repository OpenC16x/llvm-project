//===-- C166InstrInfo.cpp - C166 Instruction Information ------------------===//
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

#include "C166InstrInfo.h"
#include "C166.h"
#include "C166Subtarget.h"
#include "MCTargetDesc/C166InstPrinter.h"
#include "MCTargetDesc/C166MCTargetDesc.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/CodeGen/MachineInstrBundle.h"
#include "llvm/CodeGen/MachineMemOperand.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/IR/DiagnosticInfo.h"
#include "llvm/IR/Function.h"
#include "llvm/Support/ErrorHandling.h"

using namespace llvm;

#define GET_INSTRINFO_CTOR_DTOR
#include "C166GenInstrInfo.inc"

void C166InstrInfo::anchor() {}

C166InstrInfo::C166InstrInfo(const C166Subtarget &STI)
    : C166GenInstrInfo(STI, RI, C166::ADJCALLSTACKDOWN, C166::ADJCALLSTACKUP),
      RI() {}

// Refuse a copy the machine cannot make, saying which register was asked for
// rather than aborting.  Everything that reaches here past the two register to
// register moves came from an asm statement naming a register by hand, so this
// is a user error to report and not an internal one to assert on.
//
// The name is the one the asm statement wrote, which is the printer's rather
// than the register file's: the record is called SYSSP where the assembly says
// "sp", and quoting a name back that the user cannot have written would send
// them looking for it.
//
// A NOP goes in where the copy would have been.  Compilation stops on the
// diagnostic, but not before this pass finishes, and the pass expects
// copyPhysReg to have left an instruction behind to carry on from.
void C166InstrInfo::reportImpossibleCopy(MachineBasicBlock &MBB,
                                         MachineBasicBlock::iterator I,
                                         const DebugLoc &DL, Register Reg,
                                         const Twine &Why) const {
  const Function &F = MBB.getParent()->getFunction();
  F.getContext().diagnose(DiagnosticInfoUnsupported(
      F, "cannot copy " + Twine(C166InstPrinter::getRegisterName(Reg)) + ": " +
             Why,
      DL));
  BuildMI(MBB, I, DL, get(C166::NOP));
}

void C166InstrInfo::copyPhysReg(MachineBasicBlock &MBB,
                                MachineBasicBlock::iterator I,
                                const DebugLoc &DL, Register DestReg,
                                Register SrcReg, bool KillSrc,
                                bool RenamableDest, bool RenamableSrc) const {
  if (C166::GR16RegClass.contains(DestReg, SrcReg)) {
    BuildMI(MBB, I, DL, get(C166::MOV16rr), DestReg)
        .addReg(SrcReg, getKillRegState(KillSrc));
    return;
  }
  if (C166::GR8RegClass.contains(DestReg, SrcReg)) {
    BuildMI(MBB, I, DL, get(C166::MOVB8rr), DestReg)
        .addReg(SrcReg, getKillRegState(KillSrc));
    return;
  }

  // Everything else is a special function register at one end, which is not a
  // register the machine can move to or from: it is a memory location with a
  // name, and the move that reaches it is the absolute addressed one.  That is
  // exactly what the assembler emits for "mov r4, idx0" - the same two opcodes
  // with the same address in them - so the two agree by construction rather
  // than by both being written out.
  //
  // Nothing selects a copy like this; the only way to ask for one is an asm
  // statement that pins a value to a named register, which is the only way to
  // reach the multiply-accumulate unit's pointers and accumulator from C at
  // all.  Both directions write E, Z and N, which the two opcodes below say
  // and the register to register move above says too, so a copy has always
  // cost the flags here.
  bool DestIsWord = C166::GR16RegClass.contains(DestReg);
  bool SrcIsWord = C166::GR16RegClass.contains(SrcReg);
  bool DestIsByte = C166::GR8RegClass.contains(DestReg);
  bool SrcIsByte = C166::GR8RegClass.contains(SrcReg);
  bool DestIsSFR = !DestIsWord && !DestIsByte;
  bool SrcIsSFR = !SrcIsWord && !SrcIsByte;

  if (DestIsSFR && SrcIsSFR) {
    reportImpossibleCopy(MBB, I, DL, DestReg,
                         "a move between two special function registers has to "
                         "go through a general purpose register");
    return;
  }
  if (!DestIsSFR && !SrcIsSFR) {
    reportImpossibleCopy(MBB, I, DL, DestReg,
                         "a byte register and a word register are different "
                         "widths");
    return;
  }

  Register Mapped = DestIsSFR ? DestReg : SrcReg;

  // A byte access to one of these is a real instruction and not a half of it:
  // writing a byte to a word wide special function register writes the whole
  // word, with 00H in the half that was not addressed.  Silently throwing away
  // the other half of MAL is not what "register unsigned char x
  // __asm__(\"mal\")" asks for, so it is refused instead.
  if (DestIsByte || SrcIsByte) {
    reportImpossibleCopy(MBB, I, DL, Mapped,
                         "a byte access to a special function register writes "
                         "the whole word, so only a word value can be pinned "
                         "to one");
    return;
  }

  int64_t Addr = C166::getSFRAddressForReg(RI, Mapped);
  if (Addr < 0) {
    reportImpossibleCopy(MBB, I, DL, Mapped,
                         "it has no address, so no move can reach it");
    return;
  }
  // The name is in the register file whatever part is selected, because one
  // file holds every map at once.  Whether the selected part has the register
  // is a different question, and one worth asking here: unlike hand written
  // assembly this never passes the assembler, so an address that means
  // something else on this part would be written without a word said.
  if (!C166::isSFRInSelectedMap(Mapped,
                                MBB.getParent()->getSubtarget<C166Subtarget>())) {
    reportImpossibleCopy(MBB, I, DL, Mapped,
                         "the selected processor does not have it");
    return;
  }

  if (DestIsSFR)
    BuildMI(MBB, I, DL, get(C166::MOV16ar))
        .addImm(Addr)
        .addReg(SrcReg, getKillRegState(KillSrc));
  else
    BuildMI(MBB, I, DL, get(C166::MOV16ra), DestReg).addImm(Addr);
}

void C166InstrInfo::storeRegToStackSlot(MachineBasicBlock &MBB,
                                        MachineBasicBlock::iterator MI,
                                        Register SrcReg, bool IsKill,
                                        int FrameIndex,
                                        const TargetRegisterClass *RC,
                                        Register VReg,
                                        MachineInstr::MIFlag Flags) const {
  DebugLoc DL;
  if (MI != MBB.end())
    DL = MI->getDebugLoc();

  MachineFunction &MF = *MBB.getParent();
  MachineFrameInfo &MFI = MF.getFrameInfo();

  MachineMemOperand *MMO = MF.getMachineMemOperand(
      MachinePointerInfo::getFixedStack(MF, FrameIndex),
      MachineMemOperand::MOStore, MFI.getObjectSize(FrameIndex),
      MFI.getObjectAlign(FrameIndex));

  unsigned Opc;
  if (C166::GR16RegClass.hasSubClassEq(RC))
    Opc = C166::MOV16mr;
  else if (C166::GR8RegClass.hasSubClassEq(RC))
    Opc = C166::MOVB8mr;
  else
    llvm_unreachable("Cannot store this register to a stack slot!");

  BuildMI(MBB, MI, DL, get(Opc))
      .addFrameIndex(FrameIndex)
      .addImm(0)
      .addReg(SrcReg, getKillRegState(IsKill))
      .addMemOperand(MMO);
}

void C166InstrInfo::loadRegFromStackSlot(MachineBasicBlock &MBB,
                                         MachineBasicBlock::iterator MI,
                                         Register DestReg, int FrameIndex,
                                         const TargetRegisterClass *RC,
                                         Register VReg, unsigned SubReg,
                                         MachineInstr::MIFlag Flags) const {
  DebugLoc DL;
  if (MI != MBB.end())
    DL = MI->getDebugLoc();

  MachineFunction &MF = *MBB.getParent();
  MachineFrameInfo &MFI = MF.getFrameInfo();

  MachineMemOperand *MMO = MF.getMachineMemOperand(
      MachinePointerInfo::getFixedStack(MF, FrameIndex),
      MachineMemOperand::MOLoad, MFI.getObjectSize(FrameIndex),
      MFI.getObjectAlign(FrameIndex));

  unsigned Opc;
  if (C166::GR16RegClass.hasSubClassEq(RC))
    Opc = C166::MOV16rm;
  else if (C166::GR8RegClass.hasSubClassEq(RC))
    Opc = C166::MOVB8rm;
  else
    llvm_unreachable("Cannot load this register from a stack slot!");

  BuildMI(MBB, MI, DL, get(Opc), DestReg)
      .addFrameIndex(FrameIndex)
      .addImm(0)
      .addMemOperand(MMO);
}

/// Expand the multiply and divide pseudos.  The C166 multiply/divide unit
/// always reads and writes the MDL/MDH register pair, so the operations are
/// modelled as pseudos that get surrounded by the required moves here.
bool C166InstrInfo::expandPostRAPseudo(MachineInstr &MI) const {
  MachineBasicBlock &MBB = *MI.getParent();
  DebugLoc DL = MI.getDebugLoc();

  auto Emit = [&](unsigned Opc) {
    return BuildMI(MBB, MI, DL, get(Opc));
  };

  switch (MI.getOpcode()) {
  default:
    return false;
  case C166::ATOMADD16:
  case C166::ATOMSUB16:
  case C166::ATOMAND16:
  case C166::ATOMOR16:
  case C166::ATOMXOR16:
  case C166::ATOMADD8:
  case C166::ATOMSUB8:
  case C166::ATOMAND8:
  case C166::ATOMOR8:
  case C166::ATOMXOR8:
  case C166::ATOMSWAP16:
  case C166::ATOMSWAP8: {
    // ATOMIC #n holds interrupts and PEC transfers off for the next n
    // instructions, so what follows has to be exactly the n instructions
    // counted here: a read, the change, and the write back.  Nothing in it
    // changes the program flow and nothing in it is an EXTend, which are the
    // two things the single instruction counter cannot survive.
    bool Swap = MI.getOpcode() == C166::ATOMSWAP16 ||
                MI.getOpcode() == C166::ATOMSWAP8;
    bool Byte = MI.getOpcode() == C166::ATOMADD8 ||
                MI.getOpcode() == C166::ATOMSUB8 ||
                MI.getOpcode() == C166::ATOMAND8 ||
                MI.getOpcode() == C166::ATOMOR8 ||
                MI.getOpcode() == C166::ATOMXOR8 ||
                MI.getOpcode() == C166::ATOMSWAP8;

    Register Dst = MI.getOperand(0).getReg();
    Register Tmp = Swap ? Register() : MI.getOperand(1).getReg();
    Register Addr = MI.getOperand(Swap ? 1 : 2).getReg();
    Register Val = MI.getOperand(Swap ? 2 : 3).getReg();

    unsigned LoadOpc = Byte ? C166::MOVB8rp : C166::MOV16rp;
    unsigned StoreOpc = Byte ? C166::MOVB8pr : C166::MOV16pr;
    unsigned CopyOpc = Byte ? C166::MOVB8rr : C166::MOV16rr;

    unsigned AluOpc = 0;
    switch (MI.getOpcode()) {
    case C166::ATOMADD16:
      AluOpc = C166::ADD16rr;
      break;
    case C166::ATOMSUB16:
      AluOpc = C166::SUB16rr;
      break;
    case C166::ATOMAND16:
      AluOpc = C166::AND16rr;
      break;
    case C166::ATOMOR16:
      AluOpc = C166::OR16rr;
      break;
    case C166::ATOMXOR16:
      AluOpc = C166::XOR16rr;
      break;
    case C166::ATOMADD8:
      AluOpc = C166::ADDB8rr;
      break;
    case C166::ATOMSUB8:
      AluOpc = C166::SUBB8rr;
      break;
    case C166::ATOMAND8:
      AluOpc = C166::ANDB8rr;
      break;
    case C166::ATOMOR8:
      AluOpc = C166::ORB8rr;
      break;
    case C166::ATOMXOR8:
      AluOpc = C166::XORB8rr;
      break;
    }

    // An exchange is a read and a write; anything else copies what it read so
    // that the old value survives the change, and is four.
    Emit(C166::ATOMIC).addImm(Swap ? 2 : 4);
    Emit(LoadOpc).addReg(Dst, RegState::Define).addReg(Addr);
    if (!Swap) {
      Emit(CopyOpc).addReg(Tmp, RegState::Define).addReg(Dst);
      Emit(AluOpc).addReg(Tmp, RegState::Define).addReg(Tmp).addReg(Val);
    }
    Emit(StoreOpc).addReg(Addr).addReg(Swap ? Val : Tmp);
    MI.eraseFromParent();
    return true;
  }
  case C166::BRCC16rr:
  case C166::BRCC16ri:
  case C166::BRCC8rr:
  case C166::BRCC8ri: {
    // Nothing may come between the compare and the jump that reads its flags,
    // which is exactly why the two travelled together until now.
    // A constant of 0 to 7 fits the two byte form of the compare.
    const MachineOperand &Rhs = MI.getOperand(2);
    bool Short = Rhs.isImm() && Rhs.getImm() >= 0 && Rhs.getImm() < 8;

    unsigned CmpOpc;
    switch (MI.getOpcode()) {
    case C166::BRCC16rr:
      CmpOpc = C166::CMP16rr;
      break;
    case C166::BRCC16ri:
      CmpOpc = Short ? C166::CMP16ri3 : C166::CMP16ri;
      break;
    case C166::BRCC8rr:
      CmpOpc = C166::CMPB8rr;
      break;
    default:
      CmpOpc = Short ? C166::CMPB8ri3 : C166::CMPB8ri;
      break;
    }

    Emit(CmpOpc).add(MI.getOperand(1)).add(MI.getOperand(2));
    Emit(C166::JMPRcc)
        .addMBB(MI.getOperand(0).getMBB())
        .addImm(MI.getOperand(3).getImm());
    break;
  }
  case C166::TCRETURNs: {
    // A far tail call keeps the segment its caller expects RETS to come back
    // from, so it names the callee's segment outright.
    Emit(C166::TAILJMPs).add(MI.getOperand(0)).add(MI.getOperand(1));
    break;
  }
  case C166::TCRETURNa:
  case C166::TCRETURNi: {
    // The frame is already gone by the time this runs, so all that is left is
    // the jump that the callee will return from on our behalf.
    bool Indirect = MI.getOpcode() == C166::TCRETURNi;
    Emit(Indirect ? C166::TAILJMPi : C166::TAILJMPa).add(MI.getOperand(0));
    break;
  }
  case C166::FARLOAD16POST:
  case C166::FARLOAD8POST: {
    // The same EXTS and the same access, with the step folded into it.  The
    // operands are (value, offsetend, offset, segment) - the offset is tied to
    // the second result, which is the register the access itself writes back,
    // so the pair the machine runs is "exts seg, #1" and "mov val, [off+]".
    //
    // It is a form of its own rather than a case of the one below because the
    // access is a different instruction with a different operand shape, not
    // the same instruction with a different addressing mode.
    bool IsByte = MI.getOpcode() == C166::FARLOAD8POST;
    const MachineOperand &Value = MI.getOperand(0);
    const MachineOperand &Offset = MI.getOperand(2);
    const MachineOperand &Segment = MI.getOperand(3);

    MachineInstrBuilder Exts;
    if (Segment.isReg())
      Exts = Emit(C166::EXTSr)
                 .addReg(Segment.getReg(), getKillRegState(Segment.isKill()))
                 .addImm(1);
    else
      Exts = Emit(C166::EXTSi).add(Segment).addImm(1);

    MachineInstrBuilder Access =
        Emit(IsByte ? C166::MOVB8rpi : C166::MOV16rpi)
            .add(Value)
            .addReg(MI.getOperand(1).getReg(), RegState::Define)
            .addReg(Offset.getReg(), getKillRegState(Offset.isKill()));
    Access.cloneMemRefs(MI);

    // The post-RA scheduler runs straight after this pass and would happily
    // drop an unrelated instruction into the middle of the pair, so tie the
    // two together where nothing can get at them - the same bundling the
    // unstepped forms below get, and for the same reason.
    finalizeBundle(MBB, Exts->getIterator(), std::next(Access->getIterator()));
    break;
  }
  case C166::FARLOAD16:
  case C166::FARLOAD8:
  case C166::FARSTORE16:
  case C166::FARSTORE8:
  case C166::FARLOAD16i:
  case C166::FARLOAD8i:
  case C166::FARSTORE16i:
  case C166::FARSTORE8i:
  case C166::FARLOAD16a:
  case C166::FARLOAD8a:
  case C166::FARSTORE16a:
  case C166::FARSTORE8a: {
    // EXTS makes the address of the instruction that follows it a plain 16 bit
    // offset into the segment held by its register operand, and only covers
    // that one instruction, so the pair must not be broken up.  The hardware
    // locks interrupts for the sequence as well, so nothing observes the
    // partly extended state either.
    bool IsStore = MI.mayStore();
    bool IsByte =
        MI.getOpcode() == C166::FARLOAD8 || MI.getOpcode() == C166::FARSTORE8 ||
        MI.getOpcode() == C166::FARLOAD8i ||
        MI.getOpcode() == C166::FARSTORE8i ||
        MI.getOpcode() == C166::FARLOAD8a || MI.getOpcode() == C166::FARSTORE8a;

    // The operands are (value, offset, segment), with the value defined
    // rather than used for a load.  Either half of the address may already be
    // settled at link time, in which case it is an immediate rather than a
    // register and the access names it outright.
    const MachineOperand &Value = MI.getOperand(0);
    const MachineOperand &Offset = MI.getOperand(1);
    const MachineOperand &Segment = MI.getOperand(2);

    unsigned Opc;
    if (Offset.isReg())
      // The address is the offset register on its own, so this is the two byte
      // [Rw] form rather than the four byte one with nothing added.
      Opc = IsStore ? (IsByte ? C166::MOVB8pr : C166::MOV16pr)
                    : (IsByte ? C166::MOVB8rp : C166::MOV16rp);
    else
      Opc = IsStore ? (IsByte ? C166::MOVB8ar : C166::MOV16ar)
                    : (IsByte ? C166::MOVB8ra : C166::MOV16ra);

    MachineInstrBuilder Exts;
    if (Segment.isReg())
      Exts = Emit(C166::EXTSr)
                 .addReg(Segment.getReg(), getKillRegState(Segment.isKill()))
                 .addImm(1);
    else
      Exts = Emit(C166::EXTSi).add(Segment).addImm(1);

    auto AddOffset = [&](MachineInstrBuilder &MIB) {
      if (Offset.isReg())
        MIB.addReg(Offset.getReg(), getKillRegState(Offset.isKill()));
      else
        MIB.add(Offset);
    };

    MachineInstrBuilder Access = Emit(Opc);
    if (IsStore) {
      AddOffset(Access);
      Access.add(Value);
    } else {
      Access.add(Value);
      AddOffset(Access);
    }
    Access.cloneMemRefs(MI);

    // The post-RA scheduler runs straight after this pass and would happily
    // drop an unrelated instruction into the middle of the pair, so tie the
    // two together where nothing can get at them.
    finalizeBundle(MBB, Exts->getIterator(), std::next(Access->getIterator()));
    break;
  }
  case C166::MUL16rr:
  case C166::MULHS16rr:
  case C166::MULHU16rr: {
    Register Dst = MI.getOperand(0).getReg();
    Register Src1 = MI.getOperand(1).getReg();
    Register Src2 = MI.getOperand(2).getReg();
    bool Unsigned = MI.getOpcode() == C166::MULHU16rr;
    bool High = MI.getOpcode() != C166::MUL16rr;

    Emit(Unsigned ? C166::MULUrr : C166::MULrr)
        .addReg(Src1)
        .addReg(Src2);
    Emit(High ? C166::MOVfromMDH : C166::MOVfromMDL).addDef(Dst);
    break;
  }
  case C166::SMUL16rrLOHI:
  case C166::UMUL16rrLOHI: {
    // One MUL, then both halves out of the register pair it left them in.
    Register Lo = MI.getOperand(0).getReg();
    Register Hi = MI.getOperand(1).getReg();
    Register Src1 = MI.getOperand(2).getReg();
    Register Src2 = MI.getOperand(3).getReg();
    bool Unsigned = MI.getOpcode() == C166::UMUL16rrLOHI;

    Emit(Unsigned ? C166::MULUrr : C166::MULrr).addReg(Src1).addReg(Src2);
    Emit(C166::MOVfromMDL).addDef(Lo);
    Emit(C166::MOVfromMDH).addDef(Hi);
    break;
  }
  case C166::SDIV16rr:
  case C166::UDIV16rr:
  case C166::SREM16rr:
  case C166::UREM16rr: {
    Register Dst = MI.getOperand(0).getReg();
    Register Src1 = MI.getOperand(1).getReg();
    Register Src2 = MI.getOperand(2).getReg();
    bool Unsigned =
        MI.getOpcode() == C166::UDIV16rr || MI.getOpcode() == C166::UREM16rr;
    bool Remainder =
        MI.getOpcode() == C166::SREM16rr || MI.getOpcode() == C166::UREM16rr;

    // The dividend goes into MDL, the divisor stays in a GPR.  DIV leaves the
    // quotient in MDL and the remainder in MDH.
    Emit(C166::MOVtoMDL).addReg(Src1);
    Emit(Unsigned ? C166::DIVUr : C166::DIVr).addReg(Src2);
    Emit(Remainder ? C166::MOVfromMDH : C166::MOVfromMDL).addDef(Dst);
    break;
  }
  case C166::SDIVREM16rr:
  case C166::UDIVREM16rr: {
    // The one divide leaves both halves of the answer behind.
    Register Quot = MI.getOperand(0).getReg();
    Register Rem = MI.getOperand(1).getReg();
    Register Src1 = MI.getOperand(2).getReg();
    Register Src2 = MI.getOperand(3).getReg();
    bool Unsigned = MI.getOpcode() == C166::UDIVREM16rr;

    Emit(C166::MOVtoMDL).addReg(Src1);
    Emit(Unsigned ? C166::DIVUr : C166::DIVr).addReg(Src2);
    if (!MI.getOperand(0).isDead())
      Emit(C166::MOVfromMDL).addDef(Quot);
    if (!MI.getOperand(1).isDead())
      Emit(C166::MOVfromMDH).addDef(Rem);
    break;
  }
  case C166::MULMAC16rr:
  case C166::MULMACHS16rr:
  case C166::MULMACHU16rr: {
    // One CoMUL and one word back out of the accumulator.  Nothing loads it
    // first: CoMUL replaces what is there rather than adding to it, so the
    // accumulator is dead going in.
    Register Dst = MI.getOperand(0).getReg();
    Register Src1 = MI.getOperand(1).getReg();
    Register Src2 = MI.getOperand(2).getReg();
    bool Unsigned = MI.getOpcode() == C166::MULMACHU16rr;
    bool High = MI.getOpcode() != C166::MULMAC16rr;

    Emit(Unsigned ? C166::CoMULu_rr : C166::CoMUL_rr).addReg(Src1).addReg(Src2);
    Emit(C166::CoSTORE_sr).addDef(Dst).addReg(High ? C166::MAH : C166::MAL);
    break;
  }
  case C166::SMULMAC16rrLOHI:
  case C166::UMULMAC16rrLOHI: {
    // The same, with both words wanted.  This is the one that pays best: two
    // CoSTOREs cost what the two moves out of MDL and MDH cost, so the whole
    // of the difference between MUL and CoMUL is saved.
    Register Lo = MI.getOperand(0).getReg();
    Register Hi = MI.getOperand(1).getReg();
    Register Src1 = MI.getOperand(2).getReg();
    Register Src2 = MI.getOperand(3).getReg();
    bool Unsigned = MI.getOpcode() == C166::UMULMAC16rrLOHI;

    Emit(Unsigned ? C166::CoMULu_rr : C166::CoMUL_rr).addReg(Src1).addReg(Src2);
    Emit(C166::CoSTORE_sr).addDef(Lo).addReg(C166::MAL);
    Emit(C166::CoSTORE_sr).addDef(Hi).addReg(C166::MAH);
    break;
  }
  case C166::MULRND32rr: {
    // One rounding CoMUL and the high word out.  The instruction adds
    // 00 0000 8000H and clears MAL itself, so there is nothing to load
    // beforehand and nothing but MAH to fetch afterwards.
    Register Hi = MI.getOperand(0).getReg();
    Register A = MI.getOperand(1).getReg();
    Register B = MI.getOperand(2).getReg();
    unsigned Kind = MI.getOperand(3).getImm();

    Emit(C166::getMULRNDOpcode(Kind)).addReg(A).addReg(B);
    Emit(C166::CoSTORE_sr).addDef(Hi).addReg(C166::MAH);
    break;
  }
  case C166::MAC32rr: {
    // The accumulator goes into the unit, the product is accumulated onto it,
    // and the two words come back out.  It does not stay: nothing saves the
    // MAC accumulator across a call or an interrupt, so it is only ever live
    // between these four instructions.
    Register Lo = MI.getOperand(0).getReg();
    Register Hi = MI.getOperand(1).getReg();
    Register AccLo = MI.getOperand(2).getReg();
    Register AccHi = MI.getOperand(3).getReg();
    Register A = MI.getOperand(4).getReg();
    Register B = MI.getOperand(5).getReg();
    unsigned Kind = MI.getOperand(6).getImm();

    Emit(C166::CoLOAD_rr).addReg(AccLo).addReg(AccHi);
    Emit(C166::getMACOpcode(Kind)).addReg(A).addReg(B);
    Emit(C166::CoSTORE_sr).addDef(Lo).addReg(C166::MAL);
    Emit(C166::CoSTORE_sr).addDef(Hi).addReg(C166::MAH);
    break;
  }
  case C166::MACREP32:
  case C166::MACREP32S: {
    // A whole dot product.  IDX0 is pointed at the stream in the dual-port
    // RAM, the accumulator goes into the unit, and one repeated CoMAC walks
    // both streams and adds every product; the two words come back out at the
    // end, the same way MAC32rr takes them out.
    //
    // The CoLOAD between the write of IDX0 and the CoMAC that reads it is not
    // there for spacing - it is what the accumulator needs anyway - but it is
    // the order the hand written sequences in
    // llvm/utils/C166Sim/differential/macrepeat.c use, and putting the two
    // adjacent would be a claim about the pipeline that nothing here checks.
    // The strided form has one more output - the scratch register the offset
    // registers are written through - so everything after it moves along by
    // one.  Both forms are otherwise the same instruction.
    bool Strided = MI.getOpcode() == C166::MACREP32S;
    unsigned N = Strided ? 1 : 0;
    Register Lo = MI.getOperand(0).getReg();
    Register Hi = MI.getOperand(1).getReg();
    // Operand 2 is $ptrend, which is $ptr again: the instruction leaves the
    // stepped pointer there and nothing reads it.
    Register Scratch = Strided ? MI.getOperand(3).getReg() : Register();
    Register Idx = MI.getOperand(3 + N).getReg();
    Register Ptr = MI.getOperand(4 + N).getReg();
    Register AccLo = MI.getOperand(5 + N).getReg();
    Register AccHi = MI.getOperand(6 + N).getReg();
    unsigned Count = MI.getOperand(7 + N).getImm();
    unsigned Kind = MI.getOperand(8 + N).getImm();
    int64_t IdxStep = Strided ? int16_t(MI.getOperand(9 + N).getImm()) : 2;
    int64_t PtrStep = Strided ? int16_t(MI.getOperand(10 + N).getImm()) : 2;

    // The field is five bits and holds 2 to 31; zero is the plain form and one
    // means take the count from MRW, which holds (MRW[12:0]) + 1 - so a longer
    // run costs one move and reaches 8192.  It goes first, which leaves two
    // instructions between it and the repeat, as the hand written sequences in
    // differential/macrepeat.c have.
    unsigned Field = Count;
    if (Count > 31) {
      // MOV to one of these names it in a field rather than by address, so the
      // register is an operand; the description has it as an input, because
      // that is what the field is, so the write is said here.
      Emit(C166::MOV16regi)
          .addReg(C166::MRW)
          .addImm(Count - 1)
          .addDef(C166::MRW, RegState::Implicit);
      Field = 1;
    }

    // IDX0 is written by its address rather than by name, so nothing in the
    // instruction says which register that is; without the implicit definition
    // the scheduler would see no reason to keep this in front of the CoMAC
    // that reads it.
    const TargetRegisterInfo &RI = getRegisterInfo();
    Emit(C166::MOV16ar)
        .addImm(C166::getSFRAddressForReg(RI, C166::IDX0))
        .addReg(Idx)
        .addDef(C166::IDX0, RegState::Implicit);

    // What each pointer does to itself after the read, from PM0036 Table 27.
    // Two forward and two back are update codes of their own; any other fixed
    // distance goes through an offset register, which is what codes 4 and 5
    // add and subtract - QX for the IDX pointer and QR for the general purpose
    // one.  The register is written here rather than by the caller because it
    // belongs to the instruction being expanded: it is dead the moment the
    // repeat ends, and nothing else in the function may assume it survives,
    // which is what the Defs on the pseudo say.
    //
    // There is no third case.  The unit has no circular addressing at all -
    // the C166S V2 Architecture Overview Handbook lists "one FIR filter tap
    // per cycle, with no circular buffer management" among the MAC's features
    // - so a stride is a stride and a stream that wraps is not one of these.
    auto step = [&](int64_t Bytes, MCRegister Offs) -> unsigned {
      if (Bytes == 2)
        return 2;
      if (Bytes == -2)
        return 3;
      Emit(C166::MOV16ri).addDef(Scratch).addImm(std::abs(Bytes));
      Emit(C166::MOV16ar)
          .addImm(C166::getSFRAddressForReg(RI, Offs))
          .addReg(Scratch)
          .addDef(Offs, RegState::Implicit);
      return Bytes > 0 ? 4 : 5;
    };
    unsigned IdxCode = step(IdxStep, C166::QX0);
    unsigned PtrCode = step(PtrStep, C166::QR0);

    Emit(C166::CoLOAD_rr).addReg(AccLo).addReg(AccHi);
    // A coptr and a coidx are each a register and what happens to it after the
    // read, which is the update code beside each one.  Stepping IDX0 writes
    // it, a count of one is the one that reads MRW, and an offset register the
    // codes above chose is read; all of them are said so that nothing moves
    // across them.
    auto MAC = Emit(C166::getMACIdxOpcode(Kind))
                   .addReg(C166::IDX0)
                   .addImm(IdxCode)
                   .addReg(Ptr)
                   .addImm(PtrCode)
                   .addImm(Field)
                   .addDef(C166::IDX0, RegState::Implicit);
    if (Field == 1)
      MAC.addUse(C166::MRW, RegState::Implicit);
    if (IdxCode >= 4)
      MAC.addUse(C166::QX0, RegState::Implicit);
    if (PtrCode >= 4)
      MAC.addUse(C166::QR0, RegState::Implicit);
    Emit(C166::CoSTORE_sr).addDef(Lo).addReg(C166::MAL);
    Emit(C166::CoSTORE_sr).addDef(Hi).addReg(C166::MAH);
    break;
  }
  case C166::MINMAX32rr: {
    // The first operand goes into the accumulator, the comparison takes the
    // larger or smaller of it and the second, and the two words come back out.
    // Four instructions and no branch, against the tree that comparing two
    // words in order otherwise needs.
    Register Lo = MI.getOperand(0).getReg();
    Register Hi = MI.getOperand(1).getReg();
    Register ALo = MI.getOperand(2).getReg();
    Register AHi = MI.getOperand(3).getReg();
    Register BLo = MI.getOperand(4).getReg();
    Register BHi = MI.getOperand(5).getReg();
    unsigned Kind = MI.getOperand(6).getImm();

    Emit(C166::CoLOAD_rr).addReg(ALo).addReg(AHi);
    Emit(C166::getMinMaxOpcode(Kind)).addReg(BLo).addReg(BHi);
    Emit(C166::CoSTORE_sr).addDef(Lo).addReg(C166::MAL);
    Emit(C166::CoSTORE_sr).addDef(Hi).addReg(C166::MAH);
    break;
  }
  case C166::UDIVREM32by16: {
    // Two divides.  The first takes the high word alone, which is what DIVU
    // does, and the second takes its remainder together with the low word,
    // which is what DIVLU does.  Nothing moves the remainder between them:
    // DIVU leaves it in MDH and DIVLU reads it from there, so only MDL is
    // written for the second divide.  The high quotient has to come out of
    // MDL before that happens, which is why it is read where it is.
    Register QLo = MI.getOperand(0).getReg();
    Register QHi = MI.getOperand(1).getReg();
    Register Rem = MI.getOperand(2).getReg();
    Register Lo = MI.getOperand(3).getReg();
    Register Hi = MI.getOperand(4).getReg();
    Register Src = MI.getOperand(5).getReg();

    Emit(C166::MOVtoMDL).addReg(Hi);
    Emit(C166::DIVUr).addReg(Src);
    // A division that only wants the remainder has no use for either half of
    // the quotient, and one that only wants the quotient has none for the
    // remainder.  Nothing after this pass would remove the moves, so they are
    // not made in the first place.
    if (!MI.getOperand(1).isDead())
      Emit(C166::MOVfromMDL).addDef(QHi);
    Emit(C166::MOVtoMDL).addReg(Lo);
    Emit(C166::DIVLUr).addReg(Src);
    if (!MI.getOperand(0).isDead())
      Emit(C166::MOVfromMDL).addDef(QLo);
    if (!MI.getOperand(2).isDead())
      Emit(C166::MOVfromMDH).addDef(Rem);
    break;
  }
  }

  MI.eraseFromParent();
  return true;
}

//===----------------------------------------------------------------------===//
// Branch analysis
//===----------------------------------------------------------------------===//
//
// A conditional branch is represented in two different ways depending on how
// far code generation has progressed.  Up to and including the post register
// allocation pseudo expansion it is a single BRCC* instruction that carries
// both the comparison operands and the condition code; afterwards it is a
// plain JMPAcc preceded by a compare.  The condition returned by
// analyzeBranch() therefore always starts with the branch opcode, followed by
// the condition code and - for the fused form - the two compare operands.
//
//===----------------------------------------------------------------------===//

/// The bit branches, whose condition is which bit of what rather than a
/// condition code, and which are inverted by swapping the two opcodes.
static bool isBitBranchOpcode(unsigned Opc) {
  return Opc == C166::JBr || Opc == C166::JNBr || Opc == C166::JBm ||
         Opc == C166::JNBm;
}

static unsigned getInvertedBitBranch(unsigned Opc) {
  switch (Opc) {
  case C166::JBr:
    return C166::JNBr;
  case C166::JNBr:
    return C166::JBr;
  case C166::JBm:
    return C166::JNBm;
  case C166::JNBm:
    return C166::JBm;
  }
  llvm_unreachable("not a bit branch");
}

static bool isCondBranchOpcode(unsigned Opc) {
  switch (Opc) {
  case C166::JBr:
  case C166::JNBr:
  case C166::JBm:
  case C166::JNBm:
  case C166::BRCC16rr:
  case C166::BRCC16ri:
  case C166::BRCC8rr:
  case C166::BRCC8ri:
  case C166::JMPAcc:
  case C166::JMPRcc:
    return true;
  default:
    return false;
  }
}

static void parseCondBranch(MachineInstr &MI, MachineBasicBlock *&Target,
                            SmallVectorImpl<MachineOperand> &Cond) {
  assert(isCondBranchOpcode(MI.getOpcode()) && "Not a conditional branch");

  Target = MI.getOperand(0).getMBB();
  Cond.push_back(MachineOperand::CreateImm(MI.getOpcode()));

  // A bit branch carries which word and which bit; the opcode itself says
  // whether it goes on set or on clear.
  if (isBitBranchOpcode(MI.getOpcode())) {
    Cond.push_back(MI.getOperand(1));
    Cond.push_back(MI.getOperand(2));
    return;
  }

  if (MI.getOpcode() == C166::JMPAcc || MI.getOpcode() == C166::JMPRcc) {
    Cond.push_back(MI.getOperand(1));
    return;
  }

  Cond.push_back(MI.getOperand(3));
  Cond.push_back(MI.getOperand(1));
  Cond.push_back(MI.getOperand(2));
}

bool C166InstrInfo::reverseBranchCondition(
    SmallVectorImpl<MachineOperand> &Cond) const {
  assert((Cond.size() == 2 || Cond.size() == 3 || Cond.size() == 4) &&
         "Invalid branch condition!");

  // Testing the same bit the other way round is a different instruction
  // rather than a different condition code.
  if (isBitBranchOpcode(Cond[0].getImm())) {
    Cond[0].setImm(getInvertedBitBranch(Cond[0].getImm()));
    return false;
  }

  auto CC = static_cast<C166CC::CondCode>(Cond[1].getImm());
  C166CC::CondCode Opposite = C166CC::getOppositeCondition(CC);
  if (Opposite == C166CC::COND_INVALID)
    return true;

  Cond[1].setImm(Opposite);
  return false;
}

bool C166InstrInfo::analyzeBranch(MachineBasicBlock &MBB,
                                  MachineBasicBlock *&TBB,
                                  MachineBasicBlock *&FBB,
                                  SmallVectorImpl<MachineOperand> &Cond,
                                  bool AllowModify) const {
  TBB = nullptr;
  FBB = nullptr;
  Cond.clear();

  MachineBasicBlock::iterator I = MBB.getLastNonDebugInstr();
  if (I == MBB.end() || !isUnpredicatedTerminator(*I))
    return false;

  // A tail call ends the block like a return does, but it names a callee
  // rather than a basic block, so there is nothing here to describe.
  if (I->getOpcode() == C166::TCRETURNa || I->getOpcode() == C166::TCRETURNi ||
      I->getOpcode() == C166::TCRETURNs)
    return true;

  // Count the terminators and remember the first branch that ends the block
  // unconditionally.
  MachineBasicBlock::iterator FirstUncondOrIndirectBr = MBB.end();
  int NumTerminators = 0;
  for (auto J = I.getReverse();
       J != MBB.rend() && isUnpredicatedTerminator(*J); ++J) {
    NumTerminators++;
    if (J->getDesc().isUnconditionalBranch() ||
        J->getDesc().isIndirectBranch())
      FirstUncondOrIndirectBr = J.getReverse();
  }

  // Anything after an unconditional branch is unreachable.
  if (AllowModify && FirstUncondOrIndirectBr != MBB.end()) {
    while (std::next(FirstUncondOrIndirectBr) != MBB.end()) {
      std::next(FirstUncondOrIndirectBr)->eraseFromParent();
      NumTerminators--;
    }
    I = FirstUncondOrIndirectBr;
  }

  // An indirect branch names no basic blocks we could reason about.
  if (I->getDesc().isIndirectBranch())
    return true;

  // Anything with more than a conditional plus an unconditional branch is
  // beyond what this hook can describe.
  if (NumTerminators > 2)
    return true;

  if (NumTerminators == 1) {
    if (I->getDesc().isUnconditionalBranch()) {
      TBB = I->getOperand(0).getMBB();
      return false;
    }
    if (isCondBranchOpcode(I->getOpcode())) {
      parseCondBranch(*I, TBB, Cond);
      return false;
    }
    return true;
  }

  if (NumTerminators == 2 && isCondBranchOpcode(std::prev(I)->getOpcode()) &&
      I->getDesc().isUnconditionalBranch()) {
    parseCondBranch(*std::prev(I), TBB, Cond);
    FBB = I->getOperand(0).getMBB();
    return false;
  }

  return true;
}

unsigned C166InstrInfo::removeBranch(MachineBasicBlock &MBB,
                                     int *BytesRemoved) const {
  if (BytesRemoved)
    *BytesRemoved = 0;

  MachineBasicBlock::iterator I = MBB.getLastNonDebugInstr();
  if (I == MBB.end())
    return 0;

  if (I->getOpcode() != C166::JMPA && I->getOpcode() != C166::JMPR &&
      !isCondBranchOpcode(I->getOpcode()))
    return 0;

  if (BytesRemoved)
    *BytesRemoved += getInstSizeInBytes(*I);
  I->eraseFromParent();

  I = MBB.getLastNonDebugInstr();
  if (I == MBB.end() || !isCondBranchOpcode(I->getOpcode()))
    return 1;

  if (BytesRemoved)
    *BytesRemoved += getInstSizeInBytes(*I);
  I->eraseFromParent();
  return 2;
}

unsigned C166InstrInfo::insertBranch(MachineBasicBlock &MBB,
                                     MachineBasicBlock *TBB,
                                     MachineBasicBlock *FBB,
                                     ArrayRef<MachineOperand> Cond,
                                     const DebugLoc &DL, int *BytesAdded) const {
  assert(TBB && "insertBranch must not be told to insert a fallthrough");
  assert((Cond.size() == 0 || Cond.size() == 2 || Cond.size() == 3 ||
          Cond.size() == 4) &&
         "Invalid branch condition!");

  if (BytesAdded)
    *BytesAdded = 0;

  if (Cond.empty()) {
    assert(!FBB && "Unconditional branch with multiple successors!");
    MachineInstr &MI = *BuildMI(&MBB, DL, get(C166::JMPR)).addMBB(TBB);
    if (BytesAdded)
      *BytesAdded += getInstSizeInBytes(MI);
    return 1;
  }

  unsigned Opc = Cond[0].getImm();
  MachineInstrBuilder MIB = BuildMI(&MBB, DL, get(Opc)).addMBB(TBB);
  if (isBitBranchOpcode(Opc)) {
    MIB.add(Cond[1]);
    MIB.add(Cond[2]);
  } else {
    if (Opc != C166::JMPAcc && Opc != C166::JMPRcc) {
      MIB.add(Cond[2]);
      MIB.add(Cond[3]);
    }
    MIB.add(Cond[1]);
  }

  if (BytesAdded)
    *BytesAdded += getInstSizeInBytes(*MIB);

  if (!FBB)
    return 1;

  MachineInstr &MI = *BuildMI(&MBB, DL, get(C166::JMPR)).addMBB(FBB);
  if (BytesAdded)
    *BytesAdded += getInstSizeInBytes(MI);
  return 2;
}

unsigned C166InstrInfo::getInstSizeInBytes(const MachineInstr &MI) const {
  // A bundle holds an EXTend and the access it covers, and carries no size of
  // its own; what it costs is what is inside it.
  if (MI.isBundle())
    return getInstBundleSize(MI);

  // Debug values, labels and the like are written down rather than assembled.
  if (MI.isMetaInstruction())
    return 0;

  // Inline assembly is however long the text turns out to be.
  if (MI.isInlineAsm()) {
    const MachineFunction &MF = *MI.getParent()->getParent();
    return getInlineAsmLength(MI.getOperand(0).getSymbolName(),
                              MF.getTarget().getMCAsmInfo());
  }

  // JMPR is two bytes, but the assembler grows it to a four byte JMPA when the
  // target turns out to be too far, and that happens after everything here has
  // measured the code.  Reporting the larger size makes every offset the
  // branch relaxation works from an upper bound, so a bit branch it judges to
  // be in range really is one.
  switch (MI.getOpcode()) {
  case C166::JMPR:
  case C166::JMPRcc:
    return 4;
  default:
    return MI.getDesc().getSize();
  }
}

MachineBasicBlock *
C166InstrInfo::getBranchDestBlock(const MachineInstr &MI) const {
  // Every branch here keeps its target in operand 0, including the bit
  // branches, which is why their operands are not in the order they print in.
  return MI.getOperand(0).getMBB();
}

bool C166InstrInfo::isBranchOffsetInRange(unsigned BranchOpc,
                                          int64_t BrOffset) const {
  // The bit branches are the only ones with a range to run out of: they take a
  // signed 8 bit count of words from the instruction after them, and the
  // instruction set has no long form to grow them into.  Everything else
  // either names an address outright or is a JMPR, which the assembler turns
  // into a JMPA when it has to.
  if (!isBitBranchOpcode(BranchOpc))
    return true;

  // BrOffset is measured from the start of the four byte branch.
  int64_t FromNext = BrOffset - 4;
  assert(!(FromNext & 1) && "instructions are an even number of bytes");
  return isInt<8>(FromNext / 2);
}

void C166InstrInfo::insertIndirectBranch(MachineBasicBlock &MBB,
                                         MachineBasicBlock &DestBB,
                                         MachineBasicBlock &RestoreBB,
                                         const DebugLoc &DL, int64_t BrOffset,
                                         RegScavenger *RS) const {
  // Reached only when a plain jump cannot get there either, and JMPA covers
  // the whole 64K segment a function lives in, so a function big enough to
  // need this has other problems.
  report_fatal_error("C166: a branch target too far away for JMPA");
}

std::pair<unsigned, unsigned>
C166InstrInfo::decomposeMachineOperandsTargetFlags(unsigned TF) const {
  // Every C166 target flag is a plain value; none of them are bitmasks.
  return {TF, 0u};
}

ArrayRef<std::pair<unsigned, const char *>>
C166InstrInfo::getSerializableDirectMachineOperandTargetFlags() const {
  using namespace C166II;
  static const std::pair<unsigned, const char *> Flags[] = {
      {MO_SEG, "c166-seg"},
      {MO_SOF, "c166-sof"},
  };
  return Flags;
}
