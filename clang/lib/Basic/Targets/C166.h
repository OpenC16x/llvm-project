//===--- C166.h - Declare C166 target feature support -----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares C166 TargetInfo objects.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_LIB_BASIC_TARGETS_C166_H
#define LLVM_CLANG_LIB_BASIC_TARGETS_C166_H

#include "clang/Basic/TargetInfo.h"
#include "clang/Basic/TargetOptions.h"
#include "llvm/Support/Compiler.h"
#include "llvm/TargetParser/Triple.h"

namespace clang {
namespace targets {

class LLVM_LIBRARY_VISIBILITY C166TargetInfo : public TargetInfo {
  static const char *const GCCRegNames[];

public:
  C166TargetInfo(const llvm::Triple &Triple, const TargetOptions &)
      : TargetInfo(Triple) {
    TLSSupported = false;

    // A 16 bit machine: int is a register, and nothing needs more than word
    // alignment because the bus is a word wide.
    IntWidth = 16;
    IntAlign = 16;
    LongWidth = 32;
    LongLongWidth = 64;
    LongAlign = LongLongAlign = 16;
    FloatWidth = 32;
    FloatAlign = 16;
    DoubleWidth = LongDoubleWidth = 64;
    DoubleAlign = LongDoubleAlign = 16;
    PointerWidth = 16;
    PointerAlign = 16;
    SuitableAlign = 16;

    SizeType = UnsignedInt;
    IntMaxType = SignedLongLong;
    IntPtrType = SignedInt;
    PtrDiffType = SignedInt;
    // A word is what this machine reads and writes in one go, so a word is
    // what a signal handler can safely touch.
    SigAtomicType = SignedInt;

    // A byte or an aligned word is one bus cycle, and ATOMIC covers the read,
    // the change and the write back of one, so up to a word is lock free.
    // Anything wider is a library call, which is what the default of zero was
    // making even a word into.
    MaxAtomicPromoteWidth = MaxAtomicInlineWidth = 16;

    resetDataLayout();
  }

  void getTargetDefines(const LangOptions &Opts,
                        MacroBuilder &Builder) const override;

private:
  std::string CPU = "generic";
  bool HasMAC = false;

public:

  llvm::SmallVector<Builtin::InfosShard> getTargetBuiltins() const override {
    return {};
  }

  bool allowsLargerPreferedTypeAlignment() const override { return false; }

  bool hasFeature(StringRef Feature) const override {
    return Feature == "c166" || (Feature == "mac" && HasMAC);
  }

  /// The processors the backend knows, which is what -mcpu names.
  /// _BitInt of any width.  The backend legalises an odd width the way it
  /// legalises anything else that is not sixteen bits, so there is nothing
  /// here that has to be true for the target and is not.
  bool hasBitIntType() const override { return true; }

  bool isValidCPUName(StringRef Name) const override {
    return llvm::StringSwitch<bool>(Name)
        .Cases({"generic", "c166", "c167", "st10", "xc16x"}, true)
        .Default(false);
  }

  void fillValidCPUList(SmallVectorImpl<StringRef> &Values) const override {
    Values.append({"generic", "c166", "c167", "st10", "xc16x"});
  }

  bool setCPU(StringRef Name) override {
    if (!isValidCPUName(Name))
      return false;
    CPU = Name.str();
    return true;
  }

  /// Which features each processor has.  Only the XC16x has the
  /// multiply-accumulate coprocessor; the rest of the family does not, so
  /// nothing may be selected for it unless the CPU says it is there.
  bool initFeatureMap(llvm::StringMap<bool> &Features, DiagnosticsEngine &Diags,
                      StringRef CPUName,
                      const std::vector<std::string> &FeatureVec) const override {
    if (CPUName == "xc16x") {
      Features["mac"] = true;
      Features["ext-instr"] = true;
    } else if (CPUName == "c167" || CPUName == "st10") {
      Features["ext-instr"] = true;
    }
    return TargetInfo::initFeatureMap(Features, Diags, CPUName, FeatureVec);
  }

  bool handleTargetFeatures(std::vector<std::string> &Features,
                            DiagnosticsEngine &Diags) override {
    for (StringRef F : Features) {
      if (F == "+mac")
        HasMAC = true;
      else if (F == "-mac")
        HasMAC = false;
    }
    return true;
  }

  /// Address space 1 holds far pointers, which are a linear 24 bit address
  /// zero extended into 32 bits.  Everything else is a 16 bit near pointer.
  uint64_t getPointerWidthV(LangAS AS) const override {
    return getTargetAddressSpace(AS) == 1 ? 32 : 16;
  }

  uint64_t getPointerAlignV(LangAS AS) const override { return 16; }

  /// Every near object also has a far address -- the far space is the whole
  /// 24 bit bus and the near space is one page of it -- so a near pointer
  /// converts to a far pointer without a cast.  The reverse needs one, because
  /// the top eight bits have nowhere to go.
  bool isAddressSpaceSupersetOf(LangAS A, LangAS B) const override {
    if (A == B)
      return true;
    return getTargetAddressSpace(A) == 1 && B == LangAS::Default;
  }

  uint64_t getMaxPointerWidth() const override { return 32; }

  ArrayRef<const char *> getGCCRegNames() const override;

  ArrayRef<TargetInfo::GCCRegAlias> getGCCRegAliases() const override {
    return {};
  }

  bool validateAsmConstraint(const char *&Name,
                             TargetInfo::ConstraintInfo &Info) const override {
    return false;
  }

  std::string_view getClobbers() const override { return ""; }

  BuiltinVaListKind getBuiltinVaListKind() const override {
    // Variadic arguments all arrive on the ABI stack, so walking them is
    // walking a plain pointer.
    return TargetInfo::CharPtrBuiltinVaList;
  }
};

} // namespace targets
} // namespace clang
#endif // LLVM_CLANG_LIB_BASIC_TARGETS_C166_H
