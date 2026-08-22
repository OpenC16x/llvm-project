//===-- C166TargetMachine.cpp - Define TargetMachine for C166 -------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Top-level implementation for the C166 target.
//
//===----------------------------------------------------------------------===//

#include "C166TargetMachine.h"
#include "C166.h"
#include "C166MachineFunctionInfo.h"
#include "C166TargetObjectFile.h"
#include "TargetInfo/C166TargetInfo.h"
#include "llvm/CodeGen/Passes.h"
#include "llvm/CodeGen/TargetLoweringObjectFileImpl.h"
#include "llvm/CodeGen/TargetPassConfig.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/Compiler.h"
#include <optional>

using namespace llvm;

extern "C" LLVM_ABI LLVM_EXTERNAL_VISIBILITY void LLVMInitializeC166Target() {
  RegisterTargetMachine<C166TargetMachine> X(getTheC166Target());
  PassRegistry &PR = *PassRegistry::getPassRegistry();
  initializeC166AsmPrinterPass(PR);
  initializeC166DAGToDAGISelLegacyPass(PR);
  initializeC166MergeExtendPass(PR);
}

static Reloc::Model getEffectiveRelocModel(std::optional<Reloc::Model> RM) {
  return RM.value_or(Reloc::Static);
}

C166TargetMachine::C166TargetMachine(const Target &T, const Triple &TT,
                                     StringRef CPU, StringRef FS,
                                     const TargetOptions &Options,
                                     std::optional<Reloc::Model> RM,
                                     std::optional<CodeModel::Model> CM,
                                     CodeGenOptLevel OL, bool JIT)
    : CodeGenTargetMachineImpl(T, TT.computeDataLayout(), TT, CPU, FS, Options,
                               getEffectiveRelocModel(RM),
                               getEffectiveCodeModel(CM, CodeModel::Small), OL),
      TLOF(std::make_unique<C166TargetObjectFile>()),
      Subtarget(TT, std::string(CPU), std::string(FS), *this) {
  initAsmInfo();
}

C166TargetMachine::~C166TargetMachine() = default;

MachineFunctionInfo *C166TargetMachine::createMachineFunctionInfo(
    BumpPtrAllocator &Allocator, const Function &F,
    const TargetSubtargetInfo *STI) const {
  return C166MachineFunctionInfo::create<C166MachineFunctionInfo>(Allocator, F,
                                                                  STI);
}

namespace {

/// C166 code generator pass configuration options.
class C166PassConfig : public TargetPassConfig {
public:
  C166PassConfig(C166TargetMachine &TM, PassManagerBase &PM)
      : TargetPassConfig(TM, PM) {}

  C166TargetMachine &getC166TargetMachine() const {
    return getTM<C166TargetMachine>();
  }

  void addIRPasses() override;
  bool addInstSelector() override;
  void addPreEmitPass() override;
};

} // end anonymous namespace

TargetPassConfig *C166TargetMachine::createPassConfig(PassManagerBase &PM) {
  return new C166PassConfig(*this, PM);
}

void C166PassConfig::addIRPasses() {
  addPass(createAtomicExpandLegacyPass());
  TargetPassConfig::addIRPasses();
}

bool C166PassConfig::addInstSelector() {
  addPass(createC166ISelDag(getC166TargetMachine(), getOptLevel()));
  return false;
}

void C166PassConfig::addPreEmitPass() {
  // Late, so that it sees the instructions as they will be emitted: what it
  // looks at is which of them touch memory.
  if (getOptLevel() != CodeGenOptLevel::None)
    addPass(createC166MergeExtendPass());
}
