//===--- C166.cpp - C166 Helpers for Tools ----------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "C166.h"
#include "clang/Driver/CommonArgs.h"
#include "clang/Driver/Compilation.h"
#include "clang/Driver/Driver.h"
#include "clang/Driver/DriverDiagnostic.h"
#include "clang/Options/Options.h"
#include "llvm/Option/ArgList.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/Support/Path.h"
#include "llvm/TargetParser/C166TargetParser.h"

using namespace clang::driver;
using namespace clang::driver::toolchains;
using namespace clang::driver::tools;
using namespace clang;
using namespace llvm::opt;

/// The part -mmcu= names, or nullptr when it was not given or is not one.
/// Saying so is the constructor's job, below, so that it is said once however
/// many jobs the driver goes on to build.
static const llvm::C166::Part *getPart(const ArgList &Args) {
  const Arg *A = Args.getLastArg(options::OPT_mmcu_EQ);
  return A ? llvm::C166::getPart(A->getValue()) : nullptr;
}

/// The core being built for, which is what picks the startup file.
///
/// -mcpu= names it outright and a part implies it, in that order, which is the
/// order getCPUName resolves them in - the startup file has to agree with what
/// the compiler was told, and a program built for a core with no part named is
/// still built for that core.
static StringRef getCore(const ArgList &Args) {
  if (const Arg *A = Args.getLastArg(options::OPT_mcpu_EQ))
    return A->getValue();
  if (const llvm::C166::Part *P = getPart(Args))
    return P->Core;
  return StringRef();
}

void c166::getC166TargetFeatures(const Driver &D, const ArgList &Args,
                                 std::vector<StringRef> &Features) {
  // The coprocessor is a feature rather than a core: -mcpu=st10 covers parts
  // that have it and parts that do not, and ST's own programming manual says
  // to consult the device data sheet.  So it has to be askable for on top of
  // whichever core was named, and -mcpu=xc16x implies it without being asked.
  if (const Arg *A = Args.getLastArg(options::OPT_mmac, options::OPT_mno_mac))
    Features.push_back(A->getOption().matches(options::OPT_mmac) ? "+mac"
                                                                 : "-mac");
}

C166ToolChain::C166ToolChain(const Driver &D, const llvm::Triple &Triple,
                             const ArgList &Args)
    : Generic_ELF(D, Triple, Args) {
  // Where crt0.o and a C library are expected to be, alongside the headers
  // AddClangSystemIncludeArgs() looks for.
  SmallString<128> Dir(computeSysRoot());
  llvm::sys::path::append(Dir, "c166-elf", "lib");
  getFilePaths().push_back(std::string(Dir));

  // A part that does not exist is an error rather than one with no memory: the
  // point of naming a part is not to have to know its memory map, so getting
  // the name wrong has to say so rather than build something that will not
  // run.  Here rather than in the jobs, because a compile and a link would
  // otherwise each say it.
  const Arg *A = Args.getLastArg(options::OPT_mmcu_EQ);
  if (!A || llvm::C166::getPart(A->getValue()))
    return;

  std::string Names;
  for (const llvm::C166::Part &P : llvm::C166::getAllParts()) {
    if (!Names.empty())
      Names += ", ";
    Names += P.Name.str();
  }
  D.Diag(diag::err_drv_c166_unknown_mcu) << A->getValue() << Names;
}

std::string C166ToolChain::computeSysRoot() const {
  if (!getDriver().SysRoot.empty())
    return getDriver().SysRoot;

  SmallString<128> Dir;
  llvm::sys::path::append(Dir, getDriver().Dir, "..");
  return std::string(Dir);
}

void C166ToolChain::AddClangSystemIncludeArgs(const ArgList &DriverArgs,
                                              ArgStringList &CC1Args) const {
  if (DriverArgs.hasArg(options::OPT_nostdinc) ||
      DriverArgs.hasArg(options::OPT_nostdlibinc))
    return;

  SmallString<128> Dir(computeSysRoot());
  llvm::sys::path::append(Dir, "c166-elf", "include");
  addSystemInclude(DriverArgs, CC1Args, Dir.str());
}

void C166ToolChain::addClangTargetOptions(const ArgList &DriverArgs,
                                          ArgStringList &CC1Args, BoundArch BA,
                                          Action::OffloadKind) const {
  // The build machine's /usr/include describes the build machine, not a C166
  // part.  Only what this toolchain adds above, plus clang's own headers, is
  // meaningful here.
  CC1Args.push_back("-nostdsysteminc");

  // Naming a part is also how a program says which one it is being built for,
  // so that code can ask.  Doing this here rather than only when linking is
  // what makes a misspelled part a diagnostic on the compile as well.
  const llvm::C166::Part *P = getPart(DriverArgs);
  if (!P)
    return;

  auto define = [&](const Twine &Text) {
    CC1Args.push_back("-D");
    CC1Args.push_back(DriverArgs.MakeArgString(Text));
  };

  // The part's own name, upper cased with what is not a letter or a digit
  // turned into an underscore, which is what makes it usable in an #ifdef:
  // xc164cm-8f becomes __XC164CM_8F__.
  std::string Macro = "__";
  for (char C : P->Name)
    Macro += llvm::isAlnum(C) ? llvm::toUpper(C) : '_';
  Macro += "__";
  define(Macro);

  // Named for what each memory is rather than for where it is, the way the
  // part table is, because where it is belongs to the core.  __C166_IRAM_SIZE__
  // is the internal RAM window at 00'F600H, which the XC16x manuals call the
  // dual-port RAM and the C167 manuals the IRAM; __C166_XRAM_SIZE__ is
  // whatever RAM is outside it.
  define("__C166_PROGRAM_SIZE__=" + Twine(P->ProgramSize));
  define("__C166_XRAM_SIZE__=" + Twine(P->XRAMSize));
  define("__C166_IRAM_SIZE__=" + Twine(P->IRAMSize));
  define("__C166_PSRAM_SIZE__=" + Twine(P->PSRAMSize));
  if (P->IsROM)
    define("__C166_PROGRAM_IS_ROM__");
}

Tool *C166ToolChain::buildLinker() const {
  return new tools::c166::Linker(*this);
}

/// Hand the part's memory map to the linker script, which reads these rather
/// than having the sizes written into it.  A script that is not written to
/// expect them - somebody's own, for a board with memory outside the chip -
/// simply does not refer to them and is unaffected.
///
/// Which symbols there are is the core's business, because the shape of the
/// map is: an XC16x has program memory at C0'0000H that a near address reaches
/// only the first 48 KByte of, and a program SRAM no near address reaches at
/// all; a C167 has neither of those and an extension RAM instead.  So each
/// core gets the script it has, and the names match it.
static void addPartMemoryMap(const llvm::C166::Part &P, ArgStringList &CmdArgs,
                             const ArgList &Args) {
  auto def = [&](StringRef Name, unsigned long Value) {
    CmdArgs.push_back(
        Args.MakeArgString("--defsym=" + Name + "=" + Twine(Value)));
  };

  if (P.Core == "c167" || P.Core == "st10") {
    // A C167 addresses 16 MByte the same way, but its on-chip program memory
    // is at the bottom of a segment rather than in one of its own, so the
    // whole of it is under a data page pointer and there is no near/far split
    // to make.  A romless part has none of it and the board's script says
    // what is out there instead.
    //
    // An ST10 is a C167 derivative and takes the same three, but its Flash is
    // not one run and its extension RAM is not always at the C167's address,
    // so st10.ld divides the length up by its data sheet's block table and
    // takes the addresses below rather than knowing them.  Passing a length
    // and letting the script place it is what keeps this function from having
    // to know either map.
    def("__c166_rom_length", P.ProgramSize);
    def("__c166_iram_length", P.IRAMSize);
    def("__c166_xram_length", P.XRAMSize);
    if (P.XRAMOrigin)
      def("__c166_xram_origin", P.XRAMOrigin);
    if (P.XRAM2Size) {
      def("__c166_xram2_length", P.XRAM2Size);
      def("__c166_xram2_origin", P.XRAM2Origin);
    }

    // The three the startup code reads at run time are decided here rather
    // than left to the script, and that is not a matter of taste.  A script
    // symbol written "X = DEFINED(X) ? X : default" reads as its default to
    // every later expression in the same script, even where --defsym gave it
    // another value - the symbol itself ends up right and anything derived
    // from it does not.  Region lengths survive that, because MEMORY is
    // evaluated where the override is visible; a symbol an instruction
    // relocates against does not.
    //
    // So a script deriving __c166_enable_xper from __c166_xram_length would
    // answer for the part the script defaults to, and c167.ld's did: a board
    // passing --defsym=__c166_xram_length=0 got SYSCON.XPEN set anyway.
    // Deciding here, where the part is known, is the fix and the reason these
    // are not simply left to the defaults that already exist.
    bool HasXRAM = P.XRAMSize || P.XRAM2Size;
    def("__c166_enable_xper", HasXRAM ? 1 : 0);

    if (P.Core == "st10") {
      // XPERCON comes up as 05H, which is CAN1 and the 2 KByte XRAM1, and
      // cannot be written once SYSCON.XPEN is set - so the second extension
      // RAM is off unless startup sets bit 3 in the one window there is.
      //
      // Both shapes of second RAM count.  An ST10F272's is a region and shows
      // up as XRAM2Size; an ST10F269's adjoins XRAM1 inside the near region
      // and shows up only as that region being larger than XRAM1's 2 KByte.
      bool UsesXRAM2 = P.XRAM2Size || P.XRAMSize > 2 * 1024;
      def("__c166_xpercon", UsesXRAM2 ? 0x0D : 0x05);
      def("__c166_write_xpercon", UsesXRAM2 ? 1 : 0);
    }
    return;
  }

  // Only the first 48 KByte of the program memory is under a data page
  // pointer, so that is as much of it as a near address can reach; the rest is
  // where code and far data go.  A part with less than that has no second
  // region at all, and a zero length one would be a region a section could be
  // placed in by accident.
  unsigned long Near = P.ProgramSize < 48 * 1024 ? P.ProgramSize : 48 * 1024;
  def("__c166_rom_length", Near);

  // What is left of the first segment, and then the second.  A far access
  // carries one segment and a near branch cannot leave one, so the boundary
  // has to be a boundary between regions rather than something a section can
  // sit across.
  unsigned long Rest = P.ProgramSize - Near;
  unsigned long FirstSegment = 64 * 1024 - Near;
  unsigned long Far = Rest < FirstSegment ? Rest : FirstSegment;
  def("__c166_farrom_length", Far);
  def("__c166_farrom2_length", Rest - Far);

  def("__c166_dsram_length", P.XRAMSize);
  def("__c166_dpram_length", P.IRAMSize);
  def("__c166_psram_length", P.PSRAMSize);
}

void c166::Linker::ConstructJob(Compilation &C, const JobAction &JA,
                                const InputInfo &Output,
                                const InputInfoList &Inputs,
                                const ArgList &Args,
                                const char *LinkingOutput) const {
  const ToolChain &TC = getToolChain();
  const Driver &D = TC.getDriver();
  ArgStringList CmdArgs;

  // A part with a few kilobytes of ROM cares about every section it does not
  // need.
  if (!Args.hasArg(options::OPT_r, options::OPT_g_Group))
    CmdArgs.push_back("--gc-sections");

  Args.addAllArgs(CmdArgs, {options::OPT_e, options::OPT_n, options::OPT_s,
                            options::OPT_t, options::OPT_u});

  // crt0.o holds the reset vector, so it has to come first: the linker script
  // puts whatever lands in .reset at address zero and the part starts there.
  //
  // Which one is the core's: a C167 has no PLL to program and no RAM outside
  // the data page pointers, so its startup is a different file rather than the
  // same one with branches in it.
  if (!Args.hasArg(options::OPT_nostdlib, options::OPT_r,
                   options::OPT_nostartfiles)) {
    // An ST10 takes the C167's startup: it has no PLL of the kind crt0.S
    // programs, its data page pointers come up holding what its script wants,
    // and the one thing it needs beyond a C167 - XPERCON written before
    // SYSCON.XPEN - is in that file already, driven by symbols its own script
    // defines.
    StringRef Core = getCore(Args);
    const char *Crt0 =
        (Core == "c167" || Core == "st10") ? "c167-crt0.o" : "crt0.o";
    CmdArgs.push_back(Args.MakeArgString(TC.GetFilePath(Crt0)));
  }

  Args.AddAllArgs(CmdArgs, options::OPT_L);
  TC.AddFilePathLibArgs(Args, CmdArgs);
  AddLinkerInputs(TC, Inputs, Args, CmdArgs, JA);

  if (!Args.hasArg(options::OPT_nostdlib, options::OPT_r,
                   options::OPT_nodefaultlibs)) {
    if (!Args.hasArg(options::OPT_nolibc))
      CmdArgs.push_back("-lc");
    AddRunTimeLibs(TC, D, CmdArgs, Args);
  }

  CmdArgs.push_back("-o");
  CmdArgs.push_back(Output.getFilename());

  // The memory map is a property of the board, so there is no default script
  // to fall back on; llvm/lib/Target/C166/startup has one to start from.  What
  // -mmcu= supplies is the sizes that script asks for, so that a part is a
  // name rather than a map somebody has edited.
  if (const llvm::C166::Part *P = getPart(Args)) {
    // Naming a part and then a core the part does not have is refused here
    // and not when compiling, because it is only a link that has to reconcile
    // them: the startup file above follows -mcpu= and the map below follows
    // the part, so a disagreement builds one part's startup against another
    // part's memory.  That links and does not run.  Compiling for a core
    // while knowing a part's sizes is a different thing and stays allowed.
    if (const Arg *C = Args.getLastArg(options::OPT_mcpu_EQ))
      if (P->Core != C->getValue()) {
        D.Diag(diag::err_drv_c166_mcu_cpu_mismatch)
            << P->Name << P->Core << C->getValue();
        return;
      }
    addPartMemoryMap(*P, CmdArgs, Args);
  }

  Args.AddAllArgs(CmdArgs, options::OPT_T);

  C.addCommand(std::make_unique<Command>(
      JA, *this, ResponseFileSupport::AtFileCurCP(),
      Args.MakeArgString(TC.GetLinkerPath()), CmdArgs, Inputs, Output));
}
