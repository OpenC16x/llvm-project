//===- C166.cpp -----------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "ABIInfoImpl.h"
#include "TargetInfo.h"

using namespace clang;
using namespace clang::CodeGen;

//===----------------------------------------------------------------------===//
// C166 ABI Implementation
//===----------------------------------------------------------------------===//

namespace {

class C166ABIInfo : public DefaultABIInfo {
public:
  C166ABIInfo(CodeGenTypes &CGT) : DefaultABIInfo(CGT) {}
};

class C166TargetCodeGenInfo : public TargetCodeGenInfo {
public:
  C166TargetCodeGenInfo(CodeGenTypes &CGT)
      : TargetCodeGenInfo(std::make_unique<C166ABIInfo>(CGT)) {}
  void setTargetAttributes(const Decl *D, llvm::GlobalValue *GV,
                           CodeGen::CodeGenModule &M) const override;
};

} // namespace

void C166TargetCodeGenInfo::setTargetAttributes(
    const Decl *D, llvm::GlobalValue *GV, CodeGen::CodeGenModule &M) const {
  const auto *FD = dyn_cast_or_null<FunctionDecl>(D);
  if (!FD)
    return;
  auto *F = cast<llvm::Function>(GV);

  // The backend keys off plain string attributes rather than a calling
  // convention: an interrupt handler gets the wider callee-saved set and a
  // RETI, and a far function is placed outside the near segment and entered
  // with CALLS/RETS.
  //
  // 'far' has to go on declarations as well as definitions, because it is what
  // tells a caller in another translation unit to use the wide call sequence.
  if (FD->hasAttr<C166FarAttr>())
    F->addFnAttr("far");

  if (GV->isDeclaration())
    return;

  if (FD->hasAttr<C166InterruptAttr>()) {
    F->addFnAttr(llvm::Attribute::NoInline);
    F->addFnAttr("interrupt");
  }
}

std::unique_ptr<TargetCodeGenInfo>
CodeGen::createC166TargetCodeGenInfo(CodeGenModule &CGM) {
  return std::make_unique<C166TargetCodeGenInfo>(CGM.getTypes());
}
