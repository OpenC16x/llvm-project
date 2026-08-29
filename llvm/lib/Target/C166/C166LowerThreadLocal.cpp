//===-- C166LowerThreadLocal.cpp - thread_local on a part with one thread -===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This part runs one thread, so per-thread storage and static storage are the
// same storage.  This pass says so, by taking the thread-local marker off every
// global that carries one; from there the rest of the compiler treats them as
// ordinary globals - they land in .data and .bss rather than .tdata and .tbss,
// their symbols are STT_OBJECT rather than STT_TLS, and instruction selection
// sees a GlobalAddress it already knows how to lower.  Nothing downstream has
// to know that thread-local storage was ever involved.
//
// An interrupt handler is the reason this is the right answer rather than
// merely the cheap one.  A handler is not a thread: it runs on the interrupted
// code's stack, in the middle of that code, and has to see the same objects
// that code sees.  Any scheme that gave a handler storage of its own would be
// wrong here, and the alternative that keeps the per-thread shape - emulated
// thread-local storage, where every access is a call to __emutls_get_address -
// would pay a call per access to arrive back at one object anyway.
//
// What this does not do is make a second thread work.  There is no thread
// library on this target and no thread pointer for an ABI to use, so a program
// that manages tasks of its own gets one object shared between them.  That is
// the same bargain errno already makes here, and startup/README.txt says so
// where a reader will be looking for it.
//
//===----------------------------------------------------------------------===//

#include "C166.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/Module.h"
#include "llvm/InitializePasses.h"
#include "llvm/Pass.h"

using namespace llvm;

#define DEBUG_TYPE "c166-lower-thread-local"

namespace {

class C166LowerThreadLocal : public ModulePass {
public:
  static char ID;

  C166LowerThreadLocal() : ModulePass(ID) {}

  bool runOnModule(Module &M) override;

  StringRef getPassName() const override {
    return "C166 thread-local to static lowering";
  }
};

} // end anonymous namespace

char C166LowerThreadLocal::ID = 0;

INITIALIZE_PASS(C166LowerThreadLocal, DEBUG_TYPE,
                "C166 thread-local to static lowering", false, false)

ModulePass *llvm::createC166LowerThreadLocalPass() {
  return new C166LowerThreadLocal();
}

bool C166LowerThreadLocal::runOnModule(Module &M) {
  SmallVector<GlobalVariable *, 8> ThreadLocals;
  for (GlobalVariable &GV : M.globals())
    if (GV.isThreadLocal())
      ThreadLocals.push_back(&GV);

  if (ThreadLocals.empty())
    return false;

  // llvm.threadlocal.address takes the address of a thread-local global for the
  // running thread, and the verifier requires its operand to still be one.  On
  // this target it is the address of the global itself, so the calls go before
  // the markers do - the other order leaves the module invalid in between.
  SmallVector<CallInst *, 8> Calls;
  for (GlobalVariable *GV : ThreadLocals)
    for (User *U : GV->users())
      if (auto *CI = dyn_cast<CallInst>(U))
        if (CI->getIntrinsicID() == Intrinsic::threadlocal_address)
          Calls.push_back(CI);

  for (CallInst *CI : Calls) {
    CI->replaceAllUsesWith(CI->getArgOperand(0));
    CI->eraseFromParent();
  }

  for (GlobalVariable *GV : ThreadLocals)
    GV->setThreadLocalMode(GlobalValue::NotThreadLocal);

  return true;
}
