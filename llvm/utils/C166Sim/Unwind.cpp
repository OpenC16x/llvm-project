//===-- Unwind.cpp - Walk the stack with the program's own CFI ------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "Unwind.h"
#include "Machine.h"
#include "llvm/DebugInfo/DWARF/DWARFContext.h"
#include "llvm/DebugInfo/DWARF/DWARFDebugFrame.h"
#include "llvm/MC/MCRegisterInfo.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/FormatVariadic.h"
#include "llvm/TargetParser/Triple.h"
#include "llvm/Support/Format.h"

using namespace c166sim;
using namespace llvm;
using namespace llvm::dwarf;

namespace {

/// The registers the unwinder works in, keyed by DWARF register number.  A
/// frame is these plus the canonical frame address, and unwinding is turning
/// one set of them into the caller's.
struct RegState {
  DenseMap<unsigned, uint32_t> Values;

  std::optional<uint32_t> get(unsigned Reg) const {
    auto It = Values.find(Reg);
    return It == Values.end() ? std::nullopt : std::optional<uint32_t>(It->second);
  }
  void set(unsigned Reg, uint32_t V) { Values[Reg] = V; }
};

/// What the DWARF numbers mean here.  The mapping itself is in
/// C166RegisterInfo.td, and this asks that file for it by name rather than
/// repeating the numbers, so the two cannot disagree about which one is the
/// stack pointer.  The names are the register file's own - MCRegisterInfo
/// gives the record name rather than the assembly one, which is why "SYSSP"
/// and not "sp".
class DwarfRegs {
public:
  DwarfRegs(const MCRegisterInfo &MRI) {
    for (unsigned Reg = 1, E = MRI.getNumRegs(); Reg != E; ++Reg) {
      int N = MRI.getDwarfRegNum(MCRegister(Reg), /*isEH=*/false);
      if (N < 0)
        continue;
      StringRef Name = MRI.getName(MCRegister(Reg));
      if (Name == "PC")
        PC = unsigned(N);
      else if (Name == "SYSSP")
        SP = unsigned(N);
      else if (Name == "CSP")
        CSP = unsigned(N);
      else if (Name.size() >= 2 && Name[0] == 'R') {
        unsigned Index;
        if (!Name.drop_front().getAsInteger(10, Index) && Index < 16)
          Word[Index] = unsigned(N);
      }
    }
    R0 = Word[0];
  }

  /// Whether every register the unwinder needs was found.
  bool complete() const {
    if (PC == Missing || SP == Missing || CSP == Missing)
      return false;
    for (unsigned I = 0; I != 16; ++I)
      if (Word[I] == Missing)
        return false;
    return true;
  }

  static constexpr unsigned Missing = ~0u;
  unsigned PC = Missing, SP = Missing, CSP = Missing, R0 = Missing;
  unsigned Word[16] = {Missing, Missing, Missing, Missing, Missing, Missing,
                       Missing, Missing, Missing, Missing, Missing, Missing,
                       Missing, Missing, Missing, Missing};
};

/// The whole state of one frame while it is being unwound.
struct FrameState {
  RegState Regs;
  uint32_t CFA = 0;
  bool HasCFA = false;
};

} // end anonymous namespace

/// Read a word of the machine's memory, which is what a DW_OP_deref in a rule
/// means: everything the hardware stack holds is one word wide.
static uint32_t derefWord(Machine &M, uint32_t Addr) {
  return M.read16(Addr);
}

/// Evaluate one of the small expressions the C166 backend emits.  Only the
/// operations those use are implemented, and anything else is an error rather
/// than a guess - a rule this cannot read is a rule that has not been checked.
static Expected<uint32_t> evalExpr(Machine &M, const DWARFExpression &Expr,
                                   const FrameState &F) {
  SmallVector<uint32_t, 4> Stack;
  auto Pop = [&]() -> uint32_t {
    uint32_t V = Stack.empty() ? 0 : Stack.back();
    if (!Stack.empty())
      Stack.pop_back();
    return V;
  };

  for (const DWARFExpression::Operation &Op : Expr) {
    if (Op.isError())
      return createStringError(std::errc::invalid_argument,
                               "unreadable DWARF expression");
    switch (Op.getCode()) {
    case dwarf::DW_OP_bregx: {
      unsigned Reg = unsigned(Op.getRawOperand(0));
      int64_t Off = int64_t(Op.getRawOperand(1));
      std::optional<uint32_t> V = F.Regs.get(Reg);
      if (!V)
        return createStringError(std::errc::invalid_argument,
                                 "rule needs DWARF register %u, which this "
                                 "frame does not have",
                                 Reg);
      Stack.push_back(uint32_t(*V + Off));
      break;
    }
    case dwarf::DW_OP_deref_size: {
      uint64_t Size = Op.getRawOperand(0);
      if (Size != 2)
        return createStringError(std::errc::invalid_argument,
                                 "DW_OP_deref_size of %" PRIu64 " bytes",
                                 Size);
      Stack.push_back(derefWord(M, Pop()));
      break;
    }
    case dwarf::DW_OP_const1u:
      Stack.push_back(uint32_t(Op.getRawOperand(0)));
      break;
    case dwarf::DW_OP_shl: {
      uint32_t Amount = Pop();
      Stack.push_back(Amount >= 32 ? 0 : (Pop() << Amount));
      break;
    }
    case dwarf::DW_OP_or: {
      uint32_t B = Pop();
      Stack.push_back(Pop() | B);
      break;
    }
    default:
      return createStringError(std::errc::invalid_argument,
                               "DWARF operation 0x%02x is not one this "
                               "understands",
                               Op.getCode());
    }
  }

  if (Stack.empty())
    return createStringError(std::errc::invalid_argument,
                             "DWARF expression left nothing behind");
  return Stack.back();
}

/// Apply one unwind rule, which says where a register's caller value is.
static Expected<uint32_t> applyRule(Machine &M, const UnwindLocation &Loc,
                                    const FrameState &F, unsigned Reg) {
  uint32_t V;
  switch (Loc.getLocation()) {
  case UnwindLocation::Same: {
    std::optional<uint32_t> Cur = F.Regs.get(Reg);
    if (!Cur)
      return createStringError(std::errc::invalid_argument,
                               "rule says unchanged, but this frame does not "
                               "have DWARF register %u",
                               Reg);
    return *Cur;
  }
  case UnwindLocation::CFAPlusOffset:
    if (!F.HasCFA)
      return createStringError(std::errc::invalid_argument,
                               "rule is relative to a CFA that is not known");
    V = uint32_t(F.CFA + Loc.getOffset());
    break;
  case UnwindLocation::RegPlusOffset: {
    std::optional<uint32_t> Base = F.Regs.get(Loc.getRegister());
    if (!Base)
      return createStringError(std::errc::invalid_argument,
                               "rule is relative to DWARF register %u, which "
                               "this frame does not have",
                               Loc.getRegister());
    V = uint32_t(*Base + Loc.getOffset());
    break;
  }
  case UnwindLocation::DWARFExpr: {
    std::optional<DWARFExpression> E = Loc.getDWARFExpressionBytes();
    if (!E)
      return createStringError(std::errc::invalid_argument,
                               "rule has no expression in it");
    Expected<uint32_t> R = evalExpr(M, *E, F);
    if (!R)
      return R.takeError();
    V = *R;
    break;
  }
  case UnwindLocation::Constant:
    V = uint32_t(Loc.getOffset());
    break;
  case UnwindLocation::Undefined:
  case UnwindLocation::Unspecified:
    return createStringError(std::errc::invalid_argument,
                             "no rule for DWARF register %u", Reg);
  }

  return Loc.getDereference() ? derefWord(M, V) : V;
}

/// The row of the unwind table that covers \p PC, which is the last one at or
/// before it.
static const UnwindRow *rowFor(const UnwindTable &Table, uint32_t PC) {
  const UnwindRow *Best = nullptr;
  for (const UnwindRow &Row : Table) {
    if (!Row.hasAddress() || Row.getAddress() > PC)
      continue;
    if (!Best || Row.getAddress() >= Best->getAddress())
      Best = &Row;
  }
  return Best;
}

/// The FDE describing \p PC, if the program has one.
static const FDE *fdeFor(const DWARFDebugFrame &Frames, uint32_t PC) {
  for (const FrameEntry &Entry : Frames.entries()) {
    const auto *F = dyn_cast<FDE>(&Entry);
    if (!F)
      continue;
    if (PC >= F->getInitialLocation() &&
        PC < F->getInitialLocation() + F->getAddressRange())
      return F;
  }
  return nullptr;
}

/// What the symbol table calls the function containing \p PC.
static std::string symbolFor(const object::ObjectFile &Obj, uint32_t PC) {
  std::string Best;
  uint64_t BestAddr = 0;
  for (const object::SymbolRef &S : Obj.symbols()) {
    Expected<object::SymbolRef::Type> TyOrErr = S.getType();
    if (!TyOrErr) {
      consumeError(TyOrErr.takeError());
      continue;
    }
    if (*TyOrErr != object::SymbolRef::ST_Function)
      continue;
    Expected<uint64_t> AddrOrErr = S.getAddress();
    Expected<StringRef> NameOrErr = S.getName();
    if (!AddrOrErr || !NameOrErr) {
      consumeError(AddrOrErr.takeError());
      consumeError(NameOrErr.takeError());
      continue;
    }
    if (*AddrOrErr > PC)
      continue;
    if (Best.empty() || *AddrOrErr >= BestAddr) {
      Best = NameOrErr->str();
      BestAddr = *AddrOrErr;
    }
  }
  return Best;
}

std::vector<Frame> c166sim::backtrace(Machine &M,
                                      const object::ObjectFile &Obj,
                                      unsigned Limit) {
  std::vector<Frame> Result;

  Triple TT("c166");
  std::string Err;
  const Target *T = TargetRegistry::lookupTarget(TT, Err);
  if (!T) {
    Result.push_back({0, "", "", 0, "no C166 target registered: " + Err});
    return Result;
  }
  std::unique_ptr<const MCRegisterInfo> MRI(T->createMCRegInfo(TT));
  if (!MRI) {
    Result.push_back({0, "", "", 0, "no C166 register information"});
    return Result;
  }
  DwarfRegs Num(*MRI);
  if (!Num.complete()) {
    Result.push_back(
        {0, "", "", 0, "the register file is missing a DWARF number the "
                       "unwinder needs"});
    return Result;
  }

  std::unique_ptr<DWARFContext> DICtx = DWARFContext::create(Obj);
  const DWARFDebugFrame *Frames = nullptr;
  if (Expected<const DWARFDebugFrame *> FOrErr = DICtx->getDebugFrame())
    Frames = *FOrErr;
  else
    consumeError(FOrErr.takeError());

  // A program built without debug information has no .debug_frame, but one
  // built with exceptions has .eh_frame, which carries the same rules.  Taking
  // it means the call frame information an exception would actually be thrown
  // through is walked here too, rather than only the copy that debuggers read.
  // The two are emitted from the same rules and there is nowhere else that
  // notices if they stop agreeing.
  if (!Frames || Frames->entries().empty()) {
    if (Expected<const DWARFDebugFrame *> FOrErr = DICtx->getEHFrame())
      Frames = *FOrErr;
    else
      consumeError(FOrErr.takeError());
  }

  // Frame zero is the machine as it stands.
  FrameState F;
  for (unsigned I = 0; I != 16; ++I)
    F.Regs.set(Num.Word[I], M.getWordReg(I));
  F.Regs.set(Num.SP, M.SP);
  F.Regs.set(Num.CSP, M.CSP);
  F.Regs.set(Num.PC, (uint32_t(M.CSP) << 16) | M.IP);

  for (unsigned Depth = 0; Depth != Limit; ++Depth) {
    uint32_t PC = *F.Regs.get(Num.PC);

    Frame Out;
    Out.PC = PC;
    Out.Function = symbolFor(Obj, PC);
    if (std::optional<DILineInfo> Line = DICtx->getLineInfoForAddress(
            {PC, object::SectionedAddress::UndefSection});
        Line && Line->Line) {
      Out.File = Line->FileName;
      Out.Line = Line->Line;
      if (Out.Function.empty() && Line->FunctionName != DILineInfo::BadString)
        Out.Function = Line->FunctionName;
    }

    // The outermost frame is the one whose caller cannot be found, which on a
    // bare part is the reset vector: nothing called it.
    const FDE *Fde = Frames ? fdeFor(*Frames, PC) : nullptr;
    if (!Fde) {
      Result.push_back(Out);
      break;
    }

    Expected<UnwindTable> TableOrErr = createUnwindTable(Fde);
    if (!TableOrErr) {
      Out.Error = toString(TableOrErr.takeError());
      Result.push_back(Out);
      break;
    }
    const UnwindRow *Row = rowFor(*TableOrErr, PC);
    if (!Row) {
      Out.Error = "no unwind row covers this address";
      Result.push_back(Out);
      break;
    }

    // The canonical frame address first: rules may be written in terms of it.
    FrameState Next;
    if (Expected<uint32_t> CFA =
            applyRule(M, Row->getCFAValue(), F, Num.R0)) {
      Next.CFA = *CFA;
      Next.HasCFA = true;
      F.CFA = *CFA;
      F.HasCFA = true;
    } else {
      Out.Error = "cannot work out the frame address: " +
                  toString(CFA.takeError());
      Result.push_back(Out);
      break;
    }

    // Then every register the frame has a rule for.  A register with no rule
    // is one this frame did not disturb, which is the convention the C166 ABI
    // uses for the callee saved registers.
    const RegisterLocations &Locs = Row->getRegisterLocations();
    bool Failed = false;
    for (auto &KV : F.Regs.Values) {
      unsigned Reg = KV.first;
      if (std::optional<UnwindLocation> Loc = Locs.getRegisterLocation(Reg)) {
        Expected<uint32_t> V = applyRule(M, *Loc, F, Reg);
        if (!V) {
          if (Reg == Num.PC || Reg == Num.SP) {
            Out.Error = toString(V.takeError());
            Failed = true;
            break;
          }
          consumeError(V.takeError());
          continue;
        }
        Next.Regs.set(Reg, *V);
      } else {
        Next.Regs.set(Reg, KV.second);
      }
    }
    // The ABI stack pointer of the caller is the canonical frame address, by
    // the definition of one.
    Next.Regs.set(Num.R0, Next.CFA);

    Result.push_back(Out);
    if (Failed)
      break;

    // A return address of zero is where crt0 stops having a caller.
    if (!Next.Regs.get(Num.PC) || *Next.Regs.get(Num.PC) == 0)
      break;
    F = Next;
  }

  return Result;
}

void c166sim::printBacktrace(raw_ostream &OS, const std::vector<Frame> &Frames) {
  unsigned N = 0;
  for (const Frame &F : Frames) {
    OS << formatv("#{0} {1:x-6}", N++, F.PC);
    if (!F.Function.empty())
      OS << " in " << F.Function;
    if (F.Line)
      OS << " at " << F.File << ":" << F.Line;
    if (!F.Error.empty())
      OS << " [" << F.Error << "]";
    OS << "\n";
  }
}
