//===--- CodeGenTypeCache.h - Commonly used LLVM types and info -*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This structure provides a set of common types useful during IR emission.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_LIB_CODEGEN_CODEGENTYPECACHE_H
#define LLVM_CLANG_LIB_CODEGEN_CODEGENTYPECACHE_H

#include "clang/AST/CharUnits.h"
#include "clang/Basic/AddressSpaces.h"
#include "llvm/IR/CallingConv.h"

namespace llvm {
  class Type;
  class IntegerType;
  class PointerType;
}

namespace clang {
namespace CodeGen {

/// This structure provides a set of types that are commonly used
/// during IR emission.  It's initialized once in CodeGenModule's
/// constructor and then copied around into new CodeGenFunctions.
struct CodeGenTypeCache {
  /// void
  llvm::Type *VoidTy;

  /// i8, i16, i32, and i64
  llvm::IntegerType *Int8Ty, *Int16Ty, *Int32Ty, *Int64Ty;
  /// half, bfloat, float, double
  llvm::Type *HalfTy, *BFloatTy, *FloatTy, *DoubleTy;

  /// int
  llvm::IntegerType *IntTy;

  /// char
  llvm::IntegerType *CharTy;

  /// intptr_t, size_t, and ptrdiff_t, which we assume are the same size.
  union {
    llvm::IntegerType *IntPtrTy;
    llvm::IntegerType *SizeTy;
    llvm::IntegerType *PtrDiffTy;
  };

  /// size_t as the language defines it, which is not always SizeTy above:
  /// that is built from the target's widest pointer, and a target can have a
  /// size_t narrower than that (C166, whose far pointers are twice the width
  /// of its near ones) or wider (DXIL, whose pointers are half the width of
  /// its size_t).  Use this, and not SizeTy, wherever the value is a size_t to
  /// the language and so has to have the width the language gives it: the
  /// argument of an operator new, the count in an array cookie.
  llvm::IntegerType *LangSizeTy;

  /// ptrdiff_t as the language defines it, which is not always PtrDiffTy
  /// above, and for the same reason: that is built from the target's widest
  /// pointer.  The difference of two pointers is an expression of this type,
  /// so this is the width it has to be computed in -- on C166 a far pointer
  /// is 32 bits and ptrdiff_t is 16, and subtracting in 32 produces a value
  /// two bytes wider than the slot the caller has for it.
  llvm::IntegerType *LangPtrDiffTy;

  /// void*, void** in the target's default address space (often 0)
  union {
    llvm::PointerType *DefaultPtrTy;
    llvm::PointerType *VoidPtrTy;
    llvm::PointerType *Int8PtrTy;
    llvm::PointerType *VoidPtrPtrTy;
    llvm::PointerType *Int8PtrPtrTy;
  };

  /// void* in alloca address space
  union {
    llvm::PointerType *AllocaVoidPtrTy;
    llvm::PointerType *AllocaInt8PtrTy;
  };

  /// void* in default globals address space
  union {
    llvm::PointerType *GlobalsVoidPtrTy;
    llvm::PointerType *GlobalsInt8PtrTy;
  };

  /// Pointer in program address space
  llvm::PointerType *ProgramPtrTy;

  /// void* in the address space for constant globals
  llvm::PointerType *ConstGlobalsPtrTy;

  /// The size and alignment of the builtin C type 'int'.  This comes
  /// up enough in various ABI lowering tasks to be worth pre-computing.
  union {
    unsigned char IntSizeInBytes;
    unsigned char IntAlignInBytes;
  };
  CharUnits getIntSize() const {
    return CharUnits::fromQuantity(IntSizeInBytes);
  }
  CharUnits getIntAlign() const {
    return CharUnits::fromQuantity(IntAlignInBytes);
  }

  /// The width of a pointer into the generic address space.
  unsigned char PointerWidthInBits;

  /// The size and alignment of a pointer into the generic address space.
  union {
    unsigned char PointerAlignInBytes;
    unsigned char PointerSizeInBytes;
  };

  /// The size and alignment of size_t.
  union {
    unsigned char SizeSizeInBytes; // sizeof(size_t)
    unsigned char SizeAlignInBytes;
  };

  /// sizeof(size_t) as the language defines it; see LangSizeTy above for why
  /// that is not always SizeSizeInBytes.
  unsigned char LangSizeSizeInBytes;

  CharUnits getSizeSize() const {
    return CharUnits::fromQuantity(SizeSizeInBytes);
  }
  CharUnits getLangSizeSize() const {
    return CharUnits::fromQuantity(LangSizeSizeInBytes);
  }
  /// The alignment of the language's size_t, which this assumes is its size,
  /// the same way SizeAlignInBytes assumes it of SizeSizeInBytes above.
  CharUnits getLangSizeAlign() const {
    return CharUnits::fromQuantity(LangSizeSizeInBytes);
  }
  CharUnits getSizeAlign() const {
    return CharUnits::fromQuantity(SizeAlignInBytes);
  }
  CharUnits getPointerSize() const {
    return CharUnits::fromQuantity(PointerSizeInBytes);
  }
  CharUnits getPointerAlign() const {
    return CharUnits::fromQuantity(PointerAlignInBytes);
  }

  llvm::CallingConv::ID RuntimeCC;
  llvm::CallingConv::ID getRuntimeCC() const { return RuntimeCC; }
};

}  // end namespace CodeGen
}  // end namespace clang

#endif
