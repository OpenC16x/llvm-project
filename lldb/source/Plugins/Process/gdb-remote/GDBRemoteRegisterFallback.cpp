//===-- GDBRemoteRegisterFallback.cpp -------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "GDBRemoteRegisterFallback.h"

namespace lldb_private {
namespace process_gdb_remote {

#define REG(name, size)                                                        \
  DynamicRegisterInfo::Register {                                              \
    ConstString(#name), empty_alt_name, reg_set, size, LLDB_INVALID_INDEX32,   \
        lldb::eEncodingUint, lldb::eFormatHex, LLDB_INVALID_REGNUM,            \
        LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, {}, {}  \
  }
#define R64(name) REG(name, 8)
#define R32(name) REG(name, 4)
#define R16(name) REG(name, 2)

// The same, with the DWARF number the debug information uses for it.  Without
// one, a local described as an offset from a register - which is every local -
// cannot be found: LLDB is handed a DWARF register number and has nothing to
// turn it into.  The other lists here do not need this because the platforms
// they are for arrive over XML, where the numbers travel with the registers.
#define DREG(name, size, dwarf)                                                \
  DynamicRegisterInfo::Register {                                              \
    ConstString(#name), empty_alt_name, reg_set, size, LLDB_INVALID_INDEX32,   \
        lldb::eEncodingUint, lldb::eFormatHex, dwarf, dwarf,                   \
        LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, {}, {}                       \
  }
#define D16(name, dwarf) DREG(name, 2, dwarf)
#define D32(name, dwarf) DREG(name, 4, dwarf)

// And with a second name, for the one register that needs one - see the note
// on "sp" below.
#define D16A(name, alt, dwarf)                                                 \
  DynamicRegisterInfo::Register {                                              \
    ConstString(#name), ConstString(#alt), reg_set, 2, LLDB_INVALID_INDEX32,   \
        lldb::eEncodingUint, lldb::eFormatHex, dwarf, dwarf,                   \
        LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, {}, {}                       \
  }

static std::vector<DynamicRegisterInfo::Register> GetRegisters_aarch64() {
  ConstString empty_alt_name;
  ConstString reg_set{"general purpose registers"};

  std::vector<DynamicRegisterInfo::Register> registers{
      R64(x0),  R64(x1),  R64(x2),  R64(x3),  R64(x4),  R64(x5),   R64(x6),
      R64(x7),  R64(x8),  R64(x9),  R64(x10), R64(x11), R64(x12),  R64(x13),
      R64(x14), R64(x15), R64(x16), R64(x17), R64(x18), R64(x19),  R64(x20),
      R64(x21), R64(x22), R64(x23), R64(x24), R64(x25), R64(x26),  R64(x27),
      R64(x28), R64(x29), R64(x30), R64(sp),  R64(pc),  R32(cpsr),
  };

  return registers;
}

static std::vector<DynamicRegisterInfo::Register> GetRegisters_msp430() {
  ConstString empty_alt_name;
  ConstString reg_set{"general purpose registers"};

  std::vector<DynamicRegisterInfo::Register> registers{
      R16(pc),  R16(sp),  R16(r2),  R16(r3), R16(fp),  R16(r5),
      R16(r6),  R16(r7),  R16(r8),  R16(r9), R16(r10), R16(r11),
      R16(r12), R16(r13), R16(r14), R16(r15)};

  return registers;
}

static std::vector<DynamicRegisterInfo::Register> GetRegisters_c166() {
  ConstString empty_alt_name;
  ConstString reg_set{"general purpose registers"};

  // The order is the one the "g" packet uses, because that is what this
  // fallback is for: without it every register would be read at the wrong
  // offset.  llvm/utils/C166Sim/GDBServer.cpp has the same list and is where
  // it is decided; the two have to agree, and the stub also serves it as a
  // target description for a debugger that can read one.
  //
  // The program counter is four bytes where everything else is two.  It is the
  // 24 bit CSP:IP pair, which is what an address in the debug information is,
  // so it is one register rather than its two halves.
  //
  // The second number on each row is the DWARF one, which is not the position:
  // the sixteen general purpose registers are 0 to 15, the byte halves of the
  // first eight take 16 to 31, and everything else is numbered from 32.  The
  // debug information is written in those numbers - a local is an offset from
  // DWARF register 0 and a return address is an expression over DWARF register
  // 36 - so a list without them describes registers that can be printed and
  // nothing that can be found.  llvm/lib/Target/C166/C166RegisterInfo.td is
  // where they are decided and llvm/utils/C166Sim/GDBServer.cpp repeats them;
  // all three have to agree.
  //
  // "sp" has a second name because on this part that name is taken twice.
  // There are two stacks: the hardware one this register addresses, which
  // holds return addresses, and the R0 one that holds everything else.  LLDB's
  // idea of a stack pointer is the second - a frame is measured from it - so
  // r0 is what carries the generic SP and what "register read sp" answers
  // with.  Without another name the register the manual calls SP could not be
  // read at all; with it, "register read syssp" reaches it.
  std::vector<DynamicRegisterInfo::Register> registers{
      D16(r0, 0),     D16(r1, 1),     D16(r2, 2),     D16(r3, 3),
      D16(r4, 4),     D16(r5, 5),     D16(r6, 6),     D16(r7, 7),
      D16(r8, 8),     D16(r9, 9),     D16(r10, 10),   D16(r11, 11),
      D16(r12, 12),   D16(r13, 13),   D16(r14, 14),   D16(r15, 15),
      D16(psw, 32),   D16(mdl, 33),   D16(mdh, 34),   D16(mdc, 35),
      D16A(sp, syssp, 36),            D16(cp, 37),
      D16(stkov, 38), D16(stkun, 39),
      D16(dpp0, 40),  D16(dpp1, 41),  D16(dpp2, 42),  D16(dpp3, 43),
      D16(csp, 44),   D32(pc, 45)};

  return registers;
}

static std::vector<DynamicRegisterInfo::Register> GetRegisters_x86() {
  ConstString empty_alt_name;
  ConstString reg_set{"general purpose registers"};

  std::vector<DynamicRegisterInfo::Register> registers{
      R32(eax), R32(ecx), R32(edx), R32(ebx),    R32(esp), R32(ebp),
      R32(esi), R32(edi), R32(eip), R32(eflags), R32(cs),  R32(ss),
      R32(ds),  R32(es),  R32(fs),  R32(gs),
  };

  return registers;
}

static std::vector<DynamicRegisterInfo::Register> GetRegisters_x86_64() {
  ConstString empty_alt_name;
  ConstString reg_set{"general purpose registers"};

  std::vector<DynamicRegisterInfo::Register> registers{
      R64(rax), R64(rbx), R64(rcx), R64(rdx), R64(rsi), R64(rdi),
      R64(rbp), R64(rsp), R64(r8),  R64(r9),  R64(r10), R64(r11),
      R64(r12), R64(r13), R64(r14), R64(r15), R64(rip), R32(eflags),
      R32(cs),  R32(ss),  R32(ds),  R32(es),  R32(fs),  R32(gs),
  };

  return registers;
}

#undef D16
#undef D16A
#undef D32
#undef DREG
#undef R16
#undef R32
#undef R64
#undef REG

std::vector<DynamicRegisterInfo::Register>
GetFallbackRegisters(const ArchSpec &arch_to_use) {
  switch (arch_to_use.GetMachine()) {
  case llvm::Triple::aarch64:
    return GetRegisters_aarch64();
  case llvm::Triple::c166:
    return GetRegisters_c166();
  case llvm::Triple::msp430:
    return GetRegisters_msp430();
  case llvm::Triple::x86:
    return GetRegisters_x86();
  case llvm::Triple::x86_64:
    return GetRegisters_x86_64();
  default:
    break;
  }

  return {};
}

} // namespace process_gdb_remote
} // namespace lldb_private
