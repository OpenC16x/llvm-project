//===-- C166MACChain.cpp - Keep a running total in the coprocessor --------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Selection turns "acc += a * b" into a MAC32rr, which expands to the whole
// round trip: CoLOAD puts the accumulator into the unit, CoMAC adds the
// product, and two CoSTOREs take the answer back out.  That is right for one
// product on its own and wasteful for a run of them, because the value put
// back is the value the next one loads again.  A dot product spends three of
// its seven instructions moving the accumulator in and out of a register pair
// it never otherwise touches.
//
// This leaves the total in the unit instead.  Where a loop's only use of the
// accumulator is to add a product to it each time round, the CoLOAD moves to
// the preheader and the CoSTOREs past the exit, and what remains going round
// is the CoMAC alone: a dot product's inner loop drops from eight instructions
// to five and from eighteen states to twelve.
//
// What makes that safe is that nothing else can be using the accumulator in
// between.  It is one register pair shared by the whole part, saved by nothing
// - so the loop must contain no call, whose callee is free to multiply, and no
// other instruction that reaches the unit.  An interrupt is the other way it
// could be lost, and the answer there is not a restriction here but the save
// in the prologue of any handler that touches the coprocessor, which is what
// the C166S V2 manual asks for and is now emitted.
//
// The loop is required to be a single block with one way out.  A run of
// products spread over a condition would need the accumulate to be shown to
// happen exactly once per iteration, which is a dominance question rather than
// the syntactic one here, and more than one exit would need the values coming
// out of the unit on each of them joined by a phi.  The shape this does catch
// is the one a summing loop compiles to.
//
// Holding forty bits across the loop rather than thirty two across each trip
// does not change the answer.  The round trip truncates at every step because
// that is all a register pair holds, and staying in the unit truncates once at
// the end; the accumulator wraps rather than saturating, and 2^32 divides
// 2^40, so both come to the true total modulo 2^32.
//
//===----------------------------------------------------------------------===//

#include "C166.h"
#include "C166InstrInfo.h"
#include "C166Subtarget.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineLoopInfo.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/InitializePasses.h"

using namespace llvm;

#define DEBUG_TYPE "c166-mac-chain"

STATISTIC(NumChained, "Number of loops left accumulating in the MAC unit");

namespace {

class C166MACChain : public MachineFunctionPass {
public:
  static char ID;
  C166MACChain() : MachineFunctionPass(ID) {}

  bool runOnMachineFunction(MachineFunction &MF) override;

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.addRequired<MachineLoopInfoWrapperPass>();
    AU.addPreserved<MachineLoopInfoWrapperPass>();
    MachineFunctionPass::getAnalysisUsage(AU);
  }

  StringRef getPassName() const override {
    return "C166 keep a running total in the MAC unit";
  }

private:
  MachineRegisterInfo *MRI = nullptr;
  const C166InstrInfo *TII = nullptr;
  const TargetRegisterInfo *TRI = nullptr;

  bool chainLoop(MachineLoop *L);
};

} // end anonymous namespace

char C166MACChain::ID = 0;

/// True when Phi is the accumulator arriving in the header: one value from the
/// preheader, which is what the total starts at, and the accumulate's own
/// result from round the back edge.  Init is set to the first.
static bool isAccumulatorPhi(const MachineInstr &Phi, MachineBasicBlock *Header,
                             MachineBasicBlock *Preheader, Register BackEdge,
                             Register &Init) {
  if (Phi.getNumOperands() != 5)
    return false;

  Init = Register();
  for (unsigned I = 1; I != 5; I += 2) {
    Register Reg = Phi.getOperand(I).getReg();
    MachineBasicBlock *From = Phi.getOperand(I + 1).getMBB();
    if (From == Preheader)
      Init = Reg;
    else if (From == Header && Reg == BackEdge)
      continue;
    else
      return false;
  }
  return Init.isValid();
}

bool C166MACChain::chainLoop(MachineLoop *L) {
  if (L->getNumBlocks() != 1)
    return false;

  MachineBasicBlock *H = L->getHeader();
  MachineBasicBlock *PH = L->getLoopPreheader();
  if (!PH)
    return false;

  // Exactly one accumulate, and nothing else that could disturb the unit.  A
  // call is the case that matters: the callee is free to multiply, and on a
  // part with the coprocessor that is what multiplying uses.
  MachineInstr *MAC = nullptr;
  for (MachineInstr &MI : *H) {
    if (MI.getOpcode() == C166::MAC32rr) {
      if (MAC)
        return false;
      MAC = &MI;
      continue;
    }
    if (MI.isCall() || MI.isInlineAsm())
      return false;
    for (MCRegister Reg : {C166::MAL, C166::MAH, C166::MAS, C166::MSW,
                           C166::MCW, C166::MRW})
      if (MI.readsRegister(Reg, TRI) || MI.modifiesRegister(Reg, TRI))
        return false;
  }
  if (!MAC)
    return false;

  // One way out, so there is one place to take the total back out of the unit
  // and one value to put in the accumulate's place.  More than one would need
  // the exits joined by a phi, and this is the shape a summing loop has.
  MachineBasicBlock *Exit = nullptr;
  for (MachineBasicBlock *S : H->successors()) {
    if (S == H)
      continue;
    if (Exit && Exit != S)
      return false;
    Exit = S;
  }
  if (!Exit || H->succ_size() != 2)
    return false;

  Register OutLo = MAC->getOperand(0).getReg();
  Register OutHi = MAC->getOperand(1).getReg();
  Register InLo = MAC->getOperand(2).getReg();
  Register InHi = MAC->getOperand(3).getReg();

  MachineInstr *PhiLo = MRI->getVRegDef(InLo);
  MachineInstr *PhiHi = MRI->getVRegDef(InHi);
  if (!PhiLo || !PhiHi || !PhiLo->isPHI() || !PhiHi->isPHI())
    return false;
  if (PhiLo->getParent() != H || PhiHi->getParent() != H)
    return false;

  Register InitLo, InitHi;
  if (!isAccumulatorPhi(*PhiLo, H, PH, OutLo, InitLo) ||
      !isAccumulatorPhi(*PhiHi, H, PH, OutHi, InitHi))
    return false;

  // The accumulate has to be the only reader of what comes round the back
  // edge, or the register pair still has to hold the total.
  if (!MRI->hasOneNonDBGUse(InLo) || !MRI->hasOneNonDBGUse(InHi))
    return false;

  // And nothing in the loop may read what it produces: the value is in the
  // unit from here on, and it only comes out on the way past the exit.
  for (Register Reg : {OutLo, OutHi})
    for (MachineInstr &Use : MRI->use_nodbg_instructions(Reg)) {
      if (&Use == PhiLo || &Use == PhiHi)
        continue;
      if (L->contains(Use.getParent()))
        return false;
    }

  // Everything holds, so move the accumulator into the unit and leave it
  // there.  CoLOAD takes the low word first.
  BuildMI(*PH, PH->getFirstTerminator(), MAC->getDebugLoc(),
          TII->get(C166::CoLOAD_rr))
      .addReg(InitLo)
      .addReg(InitHi)
      .addDef(C166::MAL, RegState::Implicit)
      .addDef(C166::MAH, RegState::Implicit)
      .addDef(C166::MSW, RegState::Implicit);

  BuildMI(*H, MAC, MAC->getDebugLoc(),
          TII->get(C166::getMACOpcode(MAC->getOperand(6).getImm())))
      .add(MAC->getOperand(4))
      .add(MAC->getOperand(5))
      .addDef(C166::MAL, RegState::Implicit)
      .addDef(C166::MAH, RegState::Implicit)
      .addDef(C166::MSW, RegState::Implicit)
      .addUse(C166::MAL, RegState::Implicit)
      .addUse(C166::MAH, RegState::Implicit)
      .addUse(C166::MSW, RegState::Implicit);

  // Take the total back out on the way past the exit.  An exit reached from
  // somewhere else as well cannot hold the CoSTOREs, because the accumulator
  // is only in the unit on the edge from here, so that edge is split first.
  MachineBasicBlock *Land = Exit;
  MachineBasicBlock::iterator At;
  if (Exit->pred_size() != 1) {
    Land = H->SplitCriticalEdge(Exit, *this);
    if (!Land)
      return false;
    At = Land->getFirstTerminator();
  } else {
    At = Land->SkipPHIsAndLabels(Land->begin());
  }

  Register NewLo = MRI->createVirtualRegister(&C166::GR16RegClass);
  Register NewHi = MRI->createVirtualRegister(&C166::GR16RegClass);
  BuildMI(*Land, At, MAC->getDebugLoc(), TII->get(C166::CoSTORE_sr), NewLo)
      .addReg(C166::MAL);
  BuildMI(*Land, At, MAC->getDebugLoc(), TII->get(C166::CoSTORE_sr), NewHi)
      .addReg(C166::MAH);

  // Everything that read the accumulate's result was outside the loop, and
  // every path to it leaves by the one edge these two sit on, so they reach
  // all of it.
  MAC->eraseFromParent();
  MRI->replaceRegWith(OutLo, NewLo);
  MRI->replaceRegWith(OutHi, NewHi);

  MRI->markUsesInDebugValueAsUndef(InLo);
  MRI->markUsesInDebugValueAsUndef(InHi);
  PhiLo->eraseFromParent();
  PhiHi->eraseFromParent();
  ++NumChained;
  return true;
}

bool C166MACChain::runOnMachineFunction(MachineFunction &MF) {
  const C166Subtarget &STI = MF.getSubtarget<C166Subtarget>();
  if (!STI.hasMAC())
    return false;

  MRI = &MF.getRegInfo();
  TII = STI.getInstrInfo();
  TRI = STI.getRegisterInfo();

  MachineLoopInfo &MLI = getAnalysis<MachineLoopInfoWrapperPass>().getLI();

  // Innermost first: the single block loop this can do anything with is
  // always the innermost one of its nest.
  SmallVector<MachineLoop *, 8> Worklist(MLI.begin(), MLI.end());
  SmallVector<MachineLoop *, 8> Loops;
  while (!Worklist.empty()) {
    MachineLoop *L = Worklist.pop_back_val();
    Loops.push_back(L);
    Worklist.append(L->begin(), L->end());
  }

  bool Changed = false;
  for (MachineLoop *L : reverse(Loops))
    Changed |= chainLoop(L);
  return Changed;
}

INITIALIZE_PASS_BEGIN(C166MACChain, DEBUG_TYPE,
                      "C166 keep a running total in the MAC unit", false, false)
INITIALIZE_PASS_DEPENDENCY(MachineLoopInfoWrapperPass)
INITIALIZE_PASS_END(C166MACChain, DEBUG_TYPE,
                    "C166 keep a running total in the MAC unit", false, false)

FunctionPass *llvm::createC166MACChainPass() { return new C166MACChain(); }
