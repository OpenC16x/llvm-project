//===-- c166-sim.cpp - Run a C166 program --------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// An instruction set simulator for the C166, so that what the backend emits
// can be executed and not only looked at.  It decodes with the target's own
// disassembler, which means it can only run instructions the backend models,
// and that the two cannot drift apart.
//
//===----------------------------------------------------------------------===//

#include "Machine.h"
#include "GDBServer.h"
#include "Unwind.h"
#include "llvm/MC/MCDisassembler/MCDisassembler.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Object/ELFObjectFile.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Format.h"
#include "llvm/Support/FormatVariadic.h"
#include "llvm/Support/InitLLVM.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Support/WithColor.h"

using namespace llvm;
using namespace c166sim;

static cl::opt<std::string> InputFile(cl::Positional, cl::Required,
                                      cl::desc("<c166 elf executable>"));
static cl::opt<std::string>
    MCPU("mcpu", cl::init("xc16x"),
         cl::desc("The part being simulated, which the decoder needs: the same "
                  "short address is a different special function register on "
                  "different derivatives.  Defaults to the XC164CM, which is "
                  "what this simulator models."),
         cl::value_desc("cpu"));

static cl::opt<std::string>
    ExitSymbol("exit-symbol", cl::init("__c166_exit"),
               cl::desc("halt when execution reaches this symbol "
                        "(default __c166_exit)"));
static cl::opt<unsigned long long>
    MaxSteps("max-steps", cl::init(100000000),
             cl::desc("give up after this many instructions (0 = no limit)"));
static cl::opt<bool>
    Stats("count-states",
          cl::desc("print the instruction and state counts when the program "
                   "stops; a state is one CPU clock period, and the counts are "
                   "the instruction set manual's for execution out of the "
                   "internal program memory"));
static cl::opt<bool> Trace("trace",
                           cl::desc("print each instruction executed"));
static cl::opt<bool>
    DumpState("dump-state", cl::desc("print registers when the program stops"));
static cl::opt<bool>
    GDB("gdb", cl::desc("speak the GDB remote serial protocol on stdin and "
                        "stdout instead of running the program, so that a "
                        "debugger can drive it: target remote | c166-sim "
                        "--gdb prog.elf"));
static cl::opt<bool> Backtrace(
    "backtrace",
    cl::desc("walk the stack when the program stops, using the call frame "
             "information in the executable"));
static cl::opt<bool>
    Binary("binary", cl::desc("the input is a flat image rather than an ELF "
                              "executable; it is loaded at --load-address and "
                              "run from there"));
static cl::opt<unsigned long long>
    LoadAddress("load-address", cl::init(0),
                cl::desc("where to load a flat image (default 0)"));

int main(int argc, char **argv) {
  InitLLVM X(argc, argv);
  LLVMInitializeC166TargetInfo();
  LLVMInitializeC166TargetMC();
  LLVMInitializeC166Disassembler();
  cl::ParseCommandLineOptions(argc, argv, "C166 instruction set simulator\n");

  // Before anything is decoded: the decoder is built once, on first use, and
  // which derivative's special function registers it knows is part of it.
  c166sim::setSimCPU(MCPU);

  auto Fail = [](const Twine &Msg) -> int {
    WithColor::error(errs(), "c166-sim") << Msg << "\n";
    return 1;
  };

  auto BufOrErr = MemoryBuffer::getFile(InputFile);
  if (!BufOrErr)
    return Fail("cannot open '" + InputFile +
                "': " + BufOrErr.getError().message());
  Machine M;
  M.MaxSteps = MaxSteps;
  M.Trace = Trace;
  M.TraceOS = &errs();
  // The debugger owns stdout while it is connected, so the program's console
  // output goes the other way rather than into the middle of a packet.
  M.ConsoleOS = GDB ? &errs() : &outs();

  const object::ObjectFile *BacktraceObj = nullptr;
  auto Finish = [&]() -> int {
    if (Backtrace && BacktraceObj)
      printBacktrace(errs(), backtrace(M, *BacktraceObj));
    if (DumpState) {
      errs() << formatv("IP={0:x-4} CSP={1:x-2} PSW={2:x-4} SP={3:x-4} "
                        "CP={4:x-4}\n",
                        M.IP, M.CSP, M.PSW, M.SP, M.CP);
      for (unsigned I = 0; I != 16; ++I)
        errs() << formatv("R{0}={1:x-4}{2}", I, M.getWordReg(I),
                          (I % 8 == 7) ? "\n" : " ");
      errs() << formatv("MDL={0:x-4} MDH={1:x-4} steps={2}\n", M.MDL, M.MDH,
                        M.Steps);
    }
    if (Stats)
      errs() << formatv("instructions {0}  states {1}\n", M.Steps, M.States);
    switch (M.Stop) {
    case StopReason::Exited:
      return M.ExitCode;
    case StopReason::Running:
      return Fail("stopped for no reason");
    case StopReason::Unsupported:
    case StopReason::BadAccess:
    case StopReason::StackFault:
    case StopReason::BadSequence:
    case StopReason::Halted:
      return Fail(M.StopDetail);
    case StopReason::StepLimit:
      return Fail("ran for " + Twine(M.Steps) +
                  " instructions without finishing");
    }
    return 0;
  };

  // A flat image has no symbols and nothing to link, which is what the
  // simulator's own tests use: it stops by writing to the exit port.
  if (Binary) {
    if (GDB)
      return Fail("--gdb needs an executable, so that the debugger has symbols "
                  "and debug information to go with the machine");
    if (Backtrace)
      return Fail("--backtrace needs an executable to read the call frame "
                  "information out of, and a flat image has none");
    StringRef Data = (*BufOrErr)->getBuffer();
    for (size_t I = 0; I != Data.size(); ++I)
      M.poke8(uint32_t(LoadAddress + I), uint8_t(Data[I]));
    M.IP = uint16_t(LoadAddress);
    M.CSP = uint16_t(LoadAddress >> 16);
    while (M.step())
      ;
    return Finish();
  }

  auto ObjOrErr = object::ObjectFile::createELFObjectFile(**BufOrErr);
  if (!ObjOrErr)
    return Fail("not an ELF object: " + toString(ObjOrErr.takeError()));
  auto *Obj = dyn_cast<object::ELF32LEObjectFile>(ObjOrErr->get());
  if (!Obj)
    return Fail("not a 32 bit little endian ELF file");
  const auto &ELF = Obj->getELFFile();
  if (ELF.getHeader().e_machine != ELF::EM_C166)
    return Fail("not a C166 executable");
  BacktraceObj = Obj;

  // Load the PT_LOAD segments at their physical addresses, which is where the
  // image really is: .data is linked to run in RAM but loaded from ROM.
  auto PhdrsOrErr = ELF.program_headers();
  if (!PhdrsOrErr)
    return Fail("cannot read program headers: " +
                toString(PhdrsOrErr.takeError()));
  bool LoadedAny = false;
  for (const auto &P : *PhdrsOrErr) {
    if (P.p_type != ELF::PT_LOAD || P.p_filesz == 0)
      continue;
    if (P.p_offset + P.p_filesz > ELF.getBufSize())
      return Fail("segment runs past the end of the file");
    const uint8_t *Src = ELF.base() + P.p_offset;
    for (uint64_t I = 0; I < P.p_filesz; ++I)
      M.poke8(uint32_t(P.p_paddr + I), Src[I]);
    LoadedAny = true;
  }
  if (!LoadedAny)
    return Fail("no loadable segments");

  // The reset vector is the entry point, and the exit symbol is where a
  // finished program ends up.
  M.IP = uint16_t(ELF.getHeader().e_entry);
  M.CSP = uint16_t(ELF.getHeader().e_entry >> 16);
  // The reset vector is the first entry of the vector table, so the segment it
  // was fetched from is what VECSEG holds.  On a part this stands in for, that
  // is arranged by the reset configuration rather than by any code.
  M.VECSEG = M.CSP;

  if (!ExitSymbol.empty()) {
    for (const object::SymbolRef &S : Obj->symbols()) {
      Expected<StringRef> NameOrErr = S.getName();
      if (!NameOrErr) {
        consumeError(NameOrErr.takeError());
        continue;
      }
      if (*NameOrErr != ExitSymbol)
        continue;
      Expected<uint64_t> AddrOrErr = S.getAddress();
      if (!AddrOrErr) {
        consumeError(AddrOrErr.takeError());
        continue;
      }
      M.ExitAddress = uint32_t(*AddrOrErr);
      M.HasExitAddress = true;
      break;
    }
    // Without one there is nothing to say a program has finished, which only
    // matters when the simulator is the one deciding to stop.  A debugger
    // decides for itself, so it is allowed to attach to a program that has no
    // such symbol.
    if (!M.HasExitAddress && !GDB)
      return Fail("no symbol '" + ExitSymbol +
                  "': the simulator needs to know where a finished program "
                  "ends up, see --exit-symbol");
  }

  if (GDB)
    return serveGDB(M);

  while (M.step())
    ;

  return Finish();
}
