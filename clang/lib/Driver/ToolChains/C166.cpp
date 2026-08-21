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
#include "llvm/Support/Path.h"

using namespace clang::driver;
using namespace clang::driver::toolchains;
using namespace clang::driver::tools;
using namespace clang;
using namespace llvm::opt;

C166ToolChain::C166ToolChain(const Driver &D, const llvm::Triple &Triple,
                             const ArgList &Args)
    : Generic_ELF(D, Triple, Args) {
  // Where crt0.o and a C library are expected to be, alongside the headers
  // AddClangSystemIncludeArgs() looks for.
  SmallString<128> Dir(computeSysRoot());
  llvm::sys::path::append(Dir, "c166-elf", "lib");
  getFilePaths().push_back(std::string(Dir));
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
}

Tool *C166ToolChain::buildLinker() const {
  return new tools::c166::Linker(*this);
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
  if (!Args.hasArg(options::OPT_nostdlib, options::OPT_r,
                   options::OPT_nostartfiles))
    CmdArgs.push_back(Args.MakeArgString(TC.GetFilePath("crt0.o")));

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
  // to fall back on; llvm/lib/Target/C166/startup has one to start from.
  Args.AddAllArgs(CmdArgs, options::OPT_T);

  C.addCommand(std::make_unique<Command>(
      JA, *this, ResponseFileSupport::AtFileCurCP(),
      Args.MakeArgString(TC.GetLinkerPath()), CmdArgs, Inputs, Output));
}
