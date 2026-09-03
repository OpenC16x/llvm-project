//===-- C166MACRepeat.cpp - A dot product as one instruction --------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// The coprocessor's repeat prefix runs one instruction as many times as it is
// told, and the instruction it is worth running is the one that reads a word
// through each of two pointers, multiplies them, adds the product to the
// accumulator and steps both pointers.  That is a dot product, and the whole
// of it is four bytes.  Measured against the loop this replaces, over eight
// taps, 455 states become 59.
//
// A loop is not something a selection pattern can match, so the facts an
// instruction cannot check are established here and carried to selection as an
// intrinsic.  There are five of them, and all five are the loop's rather than
// any instruction's:
//
//   - the trip count is a constant the repeat field can hold;
//   - both streams walk one word per iteration, in step;
//   - one of them is in the dual-port RAM, because IDX0 reaches nothing else
//     (ST10 Family Programming Manual PM0036 section 2.1);
//   - nothing else in the loop touches memory, so nothing can alias either
//     stream and there is nothing left to do once the products are added;
//   - the only thing the loop computes for anything after it is the total.
//
// The third is the one that decides how much of the world this reaches.  What
// says an object is in that memory is __dpram, which is an attribute on a
// global; a pointer carries nothing, deliberately - the reasoning is under
// __dpram in llvm/lib/Target/C166/README.txt - so a filter that takes its
// delay line as an argument can never qualify, however it is written.  What
// does qualify is a loop over a __dpram global, which is a real program and a
// narrow one.
//
// The fourth reads as a restriction and is mostly a consequence.  The prefix
// repeats exactly one instruction, so anything else the loop did would have
// nowhere to happen; this replaces the loop rather than transforming it.  With
// no other memory access in it there is also no aliasing question to answer,
// which is why no alias analysis is asked for here.
//
// Holding the total in the unit across the whole run rather than in a register
// pair across each iteration does not change the answer, for the reason
// C166MACChain gives at length: the accumulator is forty bits and wraps, the
// register pair is thirty two and wraps, and 2^32 divides 2^40.
//
// The four kinds this accepts are the four C166MAC::Kind has, and all four are
// symmetric in their operands - both signed or both unsigned.  That is what
// makes it free to put whichever stream is in the dual-port RAM behind IDX0.
// CoMACsu and CoMACus would not be free that way, and there is no kind for
// them; combineMAC in C166ISelLowering.cpp says why the DAG never builds one.
//
//
// A stream need not move one word a repetition.  The unit has four offset
// registers and pointer update codes 4 to 7 step a pointer by one of them, so
// any fixed distance the loop keeps to is a stride the instruction walks by
// itself - a decimating filter, one channel of an interleaved stream, a column
// of a matrix.  What that is not is a wraparound: PM0036 Table 27 enumerates
// the post-modifications and every one is an add or a subtract, and the C166S
// V2 Architecture Overview Handbook says so outright in the MAC's feature list
// - "one, Finite Impulse Response (FIR) filter tap per cycle, with no circular
// buffer management".  So a stream that wraps is still a loop; a stream that
// strides is one instruction.
//
//===----------------------------------------------------------------------===//

#include "C166.h"
#include "C166Subtarget.h"
#include "C166TargetMachine.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Analysis/ScalarEvolution.h"
#include "llvm/Analysis/ScalarEvolutionExpressions.h"
#include "llvm/Analysis/ValueTracking.h"
#include "llvm/CodeGen/TargetPassConfig.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/IntrinsicsC166.h"
#include "llvm/InitializePasses.h"
#include "llvm/Pass.h"
#include "llvm/Transforms/Utils.h"
#include "llvm/Transforms/Utils/LoopUtils.h"
#include "llvm/Transforms/Utils/ScalarEvolutionExpander.h"

using namespace llvm;

#define DEBUG_TYPE "c166-mac-repeat"

STATISTIC(NumRepeated, "Number of dot products turned into one instruction");

/// The largest count there is.  The repeat field itself holds 2 to 31 - zero
/// is the plain form and one means "take it from MRW" - and MRW holds
/// (MRW[12:0]) + 1, which is where a longer run goes.  Which of the two a
/// count uses is the expansion's business; what matters here is the ceiling.
static constexpr uint64_t MaxCount = 8192;

namespace {

class C166MACRepeat : public FunctionPass {
public:
  static char ID;

  C166MACRepeat() : FunctionPass(ID) {}

  bool runOnFunction(Function &F) override;

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.addRequired<LoopInfoWrapperPass>();
    AU.addPreserved<LoopInfoWrapperPass>();
    AU.addRequired<DominatorTreeWrapperPass>();
    AU.addPreserved<DominatorTreeWrapperPass>();
    AU.addRequired<ScalarEvolutionWrapperPass>();
    AU.addPreserved<ScalarEvolutionWrapperPass>();
    // The shape below is described in terms of a preheader, a single latch and
    // a dedicated exit, and LCSSA is what puts the total's escape into a phi
    // this can rewrite in one place.  Asking for both is how they are there.
    AU.addRequiredID(LoopSimplifyID);
    AU.addPreservedID(LoopSimplifyID);
    AU.addRequiredID(LCSSAID);
    AU.addPreservedID(LCSSAID);
  }

  StringRef getPassName() const override {
    return "C166 dot product to a repeated coprocessor instruction";
  }

private:
  LoopInfo *LI = nullptr;
  ScalarEvolution *SE = nullptr;
  DominatorTree *DT = nullptr;

  bool tryLoop(Loop *L);
};

/// One stream: the load, where it starts, how far it moves each time round,
/// and which object it is in.
struct Stream {
  LoadInst *Load = nullptr;
  const SCEV *Start = nullptr;
  int64_t Step = 0;            ///< Bytes between one element and the next.
  bool InDPRam = false;
};

/// Everything the instruction needs, once the loop has been recognised.
struct DotProduct {
  PHINode *Acc = nullptr;      ///< The running total, arriving in the header.
  BinaryOperator *Next = nullptr; ///< And leaving it, one product further on.
  Stream Idx;                  ///< The stream IDX0 walks, in the dual-port RAM.
  Stream Ptr;                  ///< The other one.
  uint64_t Count = 0;          ///< How many products.
  unsigned Kind = 0;           ///< Which multiply-accumulate; a C166MAC::Kind.
};

} // end anonymous namespace

char C166MACRepeat::ID = 0;

/// True where a global carries __dpram, which is what says an object is in the
/// dual-port RAM.  The attribute is put on by clang and read by the object
/// file lowering to choose the section; this is the third reader and they have
/// to agree, so the name is spelled the same way in all three.
static bool isDPRamObject(const Value *V) {
  const auto *GV = dyn_cast<GlobalVariable>(getUnderlyingObject(V));
  return GV && GV->hasAttribute("c166-dpram");
}

/// The stream a load walks, or nothing where it does not walk one.
///
/// What is wanted is an address that moves by the same amount each time round
/// this loop and nothing else - an affine recurrence of this loop.  The step
/// need not be one word: the unit has four offset registers, and pointer
/// update codes 4 to 7 move a pointer by one of them rather than by a word, so
/// any fixed distance the loop keeps to is a stride the instruction can walk
/// by itself.  The start is what the pointer register is set to before the
/// run, and comes back so that it can be materialised there.
static std::optional<Stream> describeStream(LoadInst *LD, Loop *L,
                                            ScalarEvolution &SE) {
  if (!LD->isSimple() || !LD->getType()->isIntegerTy(16))
    return std::nullopt;
  // IDX0 holds even values only - bit 0 always reads as zero - and the general
  // purpose pointer reads a word at an even address in the same way, so a
  // stream that is not word aligned would be read from somewhere else.
  if (LD->getAlign() < Align(2))
    return std::nullopt;

  const auto *AR = dyn_cast<SCEVAddRecExpr>(SE.getSCEV(LD->getPointerOperand()));
  if (!AR || AR->getLoop() != L || !AR->isAffine())
    return std::nullopt;
  // The stride goes into an offset register, which is sixteen bits and is
  // added to the pointer as a signed value; and it has to be even for the same
  // reason the start does.  A step of zero is not an addressing mode here - it
  // would be update code 1, which leaves the pointer alone - and it is not a
  // stream either, since scalar evolution would not have made an add
  // recurrence of a pointer that does not move.
  const auto *Step = dyn_cast<SCEVConstant>(AR->getStepRecurrence(SE));
  if (!Step || !Step->getAPInt().isSignedIntN(16))
    return std::nullopt;
  int64_t Bytes = Step->getAPInt().getSExtValue();
  if (Bytes == 0 || (Bytes & 1))
    return std::nullopt;

  Stream S;
  S.Load = LD;
  S.Start = AR->getStart();
  S.Step = Bytes;
  S.InDPRam = isDPRamObject(LD->getPointerOperand());
  return S;
}

/// The two loads a widening product multiplies, and which kind it is.
///
/// The shape is the one a dot product written in C compiles to: two words
/// widened the same way and multiplied in the wider type, which is the only
/// way to write a product of words that does not throw half of it away.
static bool matchProduct(Value *V, LoadInst *&X, LoadInst *&Y, bool &Unsigned) {
  auto *Mul = dyn_cast<BinaryOperator>(V);
  if (!Mul || Mul->getOpcode() != Instruction::Mul)
    return false;
  if (!Mul->getType()->isIntegerTy(32) || !Mul->hasOneUse())
    return false;

  auto widened = [&](Value *Op, LoadInst *&LD, bool &IsZExt) {
    auto *Ext = dyn_cast<CastInst>(Op);
    if (!Ext || (!isa<SExtInst>(Ext) && !isa<ZExtInst>(Ext)))
      return false;
    IsZExt = isa<ZExtInst>(Ext);
    LD = dyn_cast<LoadInst>(Ext->getOperand(0));
    return LD != nullptr;
  };

  bool XZExt = false, YZExt = false;
  if (!widened(Mul->getOperand(0), X, XZExt) ||
      !widened(Mul->getOperand(1), Y, YZExt))
    return false;
  // Mixed signedness is a different instruction, and one whose two operands
  // are not interchangeable; there is no kind for it here.
  if (XZExt != YZExt)
    return false;
  Unsigned = XZExt;
  return true;
}

/// Recognise the loop, without changing anything.
///
/// This is also what the unrolling preferences ask, because a loop that is
/// about to become one instruction must not be unrolled into forty: see
/// C166TTIImpl::getUnrollingPreferences.
static bool matchDotProduct(Loop *L, ScalarEvolution &SE, DotProduct &DP) {
  // One block, which is therefore the header, the latch and the only way out.
  // The prefix repeats one instruction, so a loop with anything to branch over
  // is not one of these however it is written.
  if (L->getNumBlocks() != 1)
    return false;
  BasicBlock *BB = L->getHeader();
  BasicBlock *PH = L->getLoopPreheader();
  BasicBlock *Exit = L->getUniqueExitBlock();
  if (!PH || !Exit || !L->hasDedicatedExits())
    return false;
  if (L->getLoopLatch() != BB || L->getExitingBlock() != BB)
    return false;

  // A count the field can hold, which needs it to be a constant: "repeat this
  // many times" is the whole instruction, and there is nowhere in it for a
  // count that is only known when the program runs.
  const auto *BTC = dyn_cast<SCEVConstant>(SE.getBackedgeTakenCount(L));
  if (!BTC || BTC->getAPInt().getActiveBits() > 32)
    return false;
  uint64_t Count = BTC->getAPInt().getZExtValue() + 1;
  // One product is the plain form and needs none of this.
  if (Count < 2 || Count > MaxCount)
    return false;

  // The accumulator: a phi that starts at what the preheader hands it and
  // comes back round the latch as itself plus or minus a product.
  PHINode *Acc = nullptr;
  BinaryOperator *Next = nullptr;
  LoadInst *LoadX = nullptr, *LoadY = nullptr;
  bool Unsigned = false, Negate = false;
  for (PHINode &Phi : BB->phis()) {
    if (!Phi.getType()->isIntegerTy(32) || Phi.getNumIncomingValues() != 2)
      continue;
    auto *Cand = dyn_cast<BinaryOperator>(Phi.getIncomingValueForBlock(BB));
    if (!Cand)
      continue;
    // Inside the loop the running total goes back to the phi and nowhere
    // else; what reads it after the loop is the answer, and is checked below.
    if (any_of(Cand->users(), [&](const User *U) {
          const auto *UI = dyn_cast<Instruction>(U);
          return !UI || (L->contains(UI->getParent()) && UI != &Phi);
        }))
      continue;
    Value *Product = nullptr;
    if (Cand->getOpcode() == Instruction::Add) {
      Product = Cand->getOperand(0) == &Phi ? Cand->getOperand(1)
                                            : Cand->getOperand(0);
      if (Cand->getOperand(0) != &Phi && Cand->getOperand(1) != &Phi)
        continue;
      Negate = false;
    } else if (Cand->getOpcode() == Instruction::Sub) {
      // Taking a product away does not commute: the accumulator has to be the
      // left operand or this is something else entirely.
      if (Cand->getOperand(0) != &Phi)
        continue;
      Product = Cand->getOperand(1);
      Negate = true;
    } else {
      continue;
    }
    if (!matchProduct(Product, LoadX, LoadY, Unsigned))
      continue;
    if (Acc)
      return false; // Two of them, so neither is the loop's whole business.
    Acc = &Phi;
    Next = Cand;
  }
  if (!Acc)
    return false;

  // Both loads have to be here rather than somewhere the phi reached from.
  if (LoadX->getParent() != BB || LoadY->getParent() != BB || LoadX == LoadY)
    return false;

  // Nothing else may touch memory or have an effect.  That is what leaves the
  // two streams the only memory in play - so no alias question to ask - and
  // what makes the loop something this can delete rather than shorten.
  for (Instruction &I : *BB) {
    if (isa<PHINode>(&I) || &I == LoadX || &I == LoadY || I.isTerminator())
      continue;
    if (I.mayReadOrWriteMemory() || I.mayHaveSideEffects())
      return false;
  }

  // And the total has to be the only thing anything after the loop wants.  The
  // index it counted with is gone with it, so a program that reads that
  // afterwards is not one of these.
  for (Instruction &I : *BB)
    for (User *U : I.users())
      if (auto *UI = dyn_cast<Instruction>(U))
        if (!L->contains(UI->getParent()) && &I != Next)
          return false;

  auto SX = describeStream(LoadX, L, SE);
  auto SY = describeStream(LoadY, L, SE);
  if (!SX || !SY)
    return false;

  // One of the two goes behind IDX0, and it has to be the one IDX0 can reach.
  // Where both are in that memory either will do; where neither is, this is a
  // dot product the instruction cannot express.
  Stream Idx = *SX, Ptr = *SY;
  if (!Idx.InDPRam) {
    if (!Ptr.InDPRam)
      return false;
    std::swap(Idx, Ptr);
  }

  // The starts are addresses the preheader has to hold in registers, so they
  // have to be values it can have.
  SCEVExpander Expander(SE, "c166mac");
  if (!Expander.isSafeToExpandAt(Idx.Start, PH->getTerminator()) ||
      !Expander.isSafeToExpandAt(Ptr.Start, PH->getTerminator()))
    return false;

  DP.Acc = Acc;
  DP.Next = Next;
  DP.Idx = Idx;
  DP.Ptr = Ptr;
  DP.Count = Count;
  DP.Kind = Negate ? (Unsigned ? C166MAC::UnsignedNegate : C166MAC::SignedNegate)
                   : (Unsigned ? C166MAC::Unsigned : C166MAC::Signed);
  return true;
}

bool C166MACRepeat::tryLoop(Loop *L) {
  DotProduct DP;
  if (!matchDotProduct(L, *SE, DP))
    return false;

  BasicBlock *PH = L->getLoopPreheader();
  const Stream &Idx = DP.Idx;
  const Stream &Ptr = DP.Ptr;

  SCEVExpander Expander(*SE, "c166mac");
  Value *IdxBase =
      Expander.expandCodeFor(Idx.Start, Idx.Load->getPointerOperandType(),
                             PH->getTerminator()->getIterator());
  Value *PtrBase =
      Expander.expandCodeFor(Ptr.Start, Ptr.Load->getPointerOperandType(),
                             PH->getTerminator()->getIterator());

  IRBuilder<> B(PH->getTerminator());
  Type *I16 = B.getInt16Ty();
  Type *I32 = B.getInt32Ty();

  // The accumulator arrives and leaves as two words, because that is what the
  // machine's registers hold and what CoLOAD and CoSTORE move.  Both halves of
  // taking it apart and putting it back together cost nothing: a 32 bit value
  // is already a pair of registers by the time selection sees one.
  Value *Init = DP.Acc->getIncomingValueForBlock(PH);
  Value *InitLo = B.CreateTrunc(Init, I16);
  Value *InitHi = B.CreateTrunc(B.CreateLShr(Init, 16), I16);

  Value *Call = B.CreateIntrinsic(
      Intrinsic::c166_comac_repeat, {},
      {IdxBase, PtrBase, InitLo, InitHi, ConstantInt::get(I16, DP.Count),
       ConstantInt::get(I16, DP.Kind),
       ConstantInt::getSigned(I16, Idx.Step),
       ConstantInt::getSigned(I16, Ptr.Step)});
  Value *Lo = B.CreateExtractValue(Call, 0);
  Value *Hi = B.CreateExtractValue(Call, 1);
  Value *Total = B.CreateOr(B.CreateZExt(Lo, I32),
                            B.CreateShl(B.CreateZExt(Hi, I32), 16));

  // Everything after the loop that wanted the total gets it from here.  In
  // LCSSA form that is the one phi in the exit block, whose incoming value
  // deleteDeadLoop then re-labels as coming from the preheader.
  for (Use &U : make_early_inc_range(DP.Next->uses()))
    if (auto *UI = dyn_cast<Instruction>(U.getUser()))
      if (!L->contains(UI->getParent()))
        U.set(Total);

  deleteDeadLoop(L, DT, SE, LI);
  ++NumRepeated;
  return true;
}

bool llvm::C166::isRepeatedCoMACLoop(Loop *L, ScalarEvolution &SE) {
  DotProduct DP;
  return matchDotProduct(L, SE, DP);
}

bool C166MACRepeat::runOnFunction(Function &F) {
  if (skipFunction(F))
    return false;

  auto *TPC = getAnalysisIfAvailable<TargetPassConfig>();
  if (!TPC)
    return false;
  const auto &TM = TPC->getTM<C166TargetMachine>();
  if (!TM.getSubtargetImpl(F)->hasMAC())
    return false;

  LI = &getAnalysis<LoopInfoWrapperPass>().getLoopInfo();
  SE = &getAnalysis<ScalarEvolutionWrapperPass>().getSE();
  DT = &getAnalysis<DominatorTreeWrapperPass>().getDomTree();

  // Innermost first, and by value: the loop this replaces is deleted, so the
  // list cannot be walked while it is being changed.
  SmallVector<Loop *, 8> Worklist(LI->begin(), LI->end());
  SmallVector<Loop *, 8> Loops;
  while (!Worklist.empty()) {
    Loop *L = Worklist.pop_back_val();
    Loops.push_back(L);
    Worklist.append(L->begin(), L->end());
  }

  bool Changed = false;
  for (Loop *L : reverse(Loops))
    if (L->getNumBlocks() == 1)
      Changed |= tryLoop(L);
  return Changed;
}

INITIALIZE_PASS_BEGIN(C166MACRepeat, DEBUG_TYPE,
                      "C166 dot product to a repeated coprocessor instruction",
                      false, false)
INITIALIZE_PASS_DEPENDENCY(DominatorTreeWrapperPass)
INITIALIZE_PASS_DEPENDENCY(LoopInfoWrapperPass)
INITIALIZE_PASS_DEPENDENCY(LoopSimplify)
INITIALIZE_PASS_DEPENDENCY(LCSSAWrapperPass)
INITIALIZE_PASS_DEPENDENCY(ScalarEvolutionWrapperPass)
INITIALIZE_PASS_END(C166MACRepeat, DEBUG_TYPE,
                    "C166 dot product to a repeated coprocessor instruction",
                    false, false)

FunctionPass *llvm::createC166MACRepeatPass() { return new C166MACRepeat(); }
