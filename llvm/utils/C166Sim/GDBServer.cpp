//===-- GDBServer.cpp - Serve the machine to a debugger -------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// The GDB remote serial protocol.  A packet is "$" then the body then "#" then
// two hex digits of checksum, and each side acknowledges what it receives with
// "+" or asks again with "-".  That is the whole framing; the rest of this file
// is the handful of packets a debugger needs to look at a stopped machine and
// let it go again.
//
// It is spoken over stdin and stdout rather than a socket, which is what
// "target remote | c166-sim --gdb prog.elf" wants: no port to choose, nothing
// left listening if the debugger goes away, and it works wherever a pipe does.
//
// A debugger that does not know the C166 cannot do much with this yet - none
// does - but the protocol is not architecture specific, so an ordinary remote
// client can read memory, read and write registers, set breakpoints and step.
// The register numbering it reports is the one in C166RegisterInfo.td, so that
// there is one numbering across the assembler, the debug information and this.
//
//===----------------------------------------------------------------------===//

#include "GDBServer.h"
#include "Machine.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Errno.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/raw_ostream.h"
#include <cstdio>
#include <optional>
#include <set>
#include <string>

#ifdef LLVM_ON_UNIX
#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

using namespace c166sim;
using namespace llvm;

namespace {

/// What the debugger sees, and in what order the "g" packet holds it.
///
/// The numbers are the DWARF ones from C166RegisterInfo.td, which is also what
/// the unwind information is written in terms of, so a debugger that learns
/// this target has one numbering to learn.  They are not consecutive - the byte
/// registers occupy 16 to 31 and are left out here, being halves of R0 to R7
/// rather than registers of their own - so the "g" packet is laid out in the
/// order below and the numbers are told to the debugger separately, in the
/// target description.
enum class Slot {
#define C166_GDB_REGS                                                          \
  X(R0, "r0", 16, 0) X(R1, "r1", 16, 1) X(R2, "r2", 16, 2) X(R3, "r3", 16, 3)  \
  X(R4, "r4", 16, 4) X(R5, "r5", 16, 5) X(R6, "r6", 16, 6) X(R7, "r7", 16, 7)  \
  X(R8, "r8", 16, 8) X(R9, "r9", 16, 9) X(R10, "r10", 16, 10)                  \
  X(R11, "r11", 16, 11) X(R12, "r12", 16, 12) X(R13, "r13", 16, 13)            \
  X(R14, "r14", 16, 14) X(R15, "r15", 16, 15) X(PSW, "psw", 16, 32)            \
  X(MDL, "mdl", 16, 33) X(MDH, "mdh", 16, 34) X(MDC, "mdc", 16, 35)            \
  X(SP, "sp", 16, 36) X(CP, "cp", 16, 37) X(STKOV, "stkov", 16, 38)            \
  X(STKUN, "stkun", 16, 39) X(DPP0, "dpp0", 16, 40) X(DPP1, "dpp1", 16, 41)    \
  X(DPP2, "dpp2", 16, 42) X(DPP3, "dpp3", 16, 43) X(CSP, "csp", 16, 44)        \
  X(PC, "pc", 32, 45)
#define X(Enum, Name, Bits, Dwarf) Enum,
  C166_GDB_REGS
#undef X
      NumSlots
};

struct RegDesc {
  const char *Name;
  unsigned Bits;
  unsigned Dwarf;
};

static const RegDesc Regs[] = {
#define X(Enum, Name, Bits, Dwarf) {Name, Bits, Dwarf},
    C166_GDB_REGS
#undef X
};

/// The program counter is the 24 bit CSP:IP pair, which is what an address in
/// the debug information is, so it is reported as one 32 bit register rather
/// than as its two halves.
static uint32_t readReg(const Machine &M, Slot S) {
  switch (S) {
  case Slot::PSW:
    return M.PSW;
  case Slot::MDL:
    return M.MDL;
  case Slot::MDH:
    return M.MDH;
  case Slot::MDC:
    return M.MDC;
  case Slot::SP:
    return M.SP;
  case Slot::CP:
    return M.CP;
  case Slot::STKOV:
    return M.STKOV;
  case Slot::STKUN:
    return M.STKUN;
  case Slot::DPP0:
    return M.DPP[0];
  case Slot::DPP1:
    return M.DPP[1];
  case Slot::DPP2:
    return M.DPP[2];
  case Slot::DPP3:
    return M.DPP[3];
  case Slot::CSP:
    return M.CSP;
  case Slot::PC:
    return (uint32_t(M.CSP) << 16) | M.IP;
  default:
    return M.getWordReg(unsigned(S) - unsigned(Slot::R0));
  }
}

static void writeReg(Machine &M, Slot S, uint32_t V) {
  switch (S) {
  case Slot::PSW:
    M.PSW = uint16_t(V);
    return;
  case Slot::MDL:
    M.MDL = uint16_t(V);
    return;
  case Slot::MDH:
    M.MDH = uint16_t(V);
    return;
  case Slot::MDC:
    M.MDC = uint16_t(V);
    return;
  case Slot::SP:
    M.SP = uint16_t(V);
    return;
  case Slot::CP:
    M.CP = uint16_t(V);
    return;
  case Slot::STKOV:
    M.STKOV = uint16_t(V);
    return;
  case Slot::STKUN:
    M.STKUN = uint16_t(V);
    return;
  case Slot::DPP0:
    M.DPP[0] = V & 0x3FF;
    return;
  case Slot::DPP1:
    M.DPP[1] = V & 0x3FF;
    return;
  case Slot::DPP2:
    M.DPP[2] = V & 0x3FF;
    return;
  case Slot::DPP3:
    M.DPP[3] = V & 0x3FF;
    return;
  case Slot::CSP:
    M.CSP = uint16_t(V);
    return;
  case Slot::PC:
    M.CSP = uint16_t(V >> 16);
    M.IP = uint16_t(V);
    return;
  default:
    M.setWordReg(unsigned(S) - unsigned(Slot::R0), uint16_t(V));
    return;
  }
}

/// A register's value, little endian, as the hex the protocol carries.
static void appendRegHex(std::string &Out, uint32_t V, unsigned Bits) {
  for (unsigned I = 0; I != Bits / 8; ++I) {
    Out.push_back(hexdigit((V >> (8 * I + 4)) & 0xF, /*LowerCase=*/true));
    Out.push_back(hexdigit((V >> (8 * I)) & 0xF, /*LowerCase=*/true));
  }
}

/// The reverse: little endian hex back to a value, or nothing if it is not the
/// right number of digits or not hex at all.
static std::optional<uint32_t> parseRegHex(StringRef Hex, unsigned Bits) {
  if (Hex.size() != Bits / 4)
    return std::nullopt;
  uint32_t V = 0;
  for (unsigned I = 0; I != Bits / 8; ++I) {
    unsigned Hi = hexDigitValue(Hex[2 * I]);
    unsigned Lo = hexDigitValue(Hex[2 * I + 1]);
    if (Hi == unsigned(-1) || Lo == unsigned(-1))
      return std::nullopt;
    V |= uint32_t((Hi << 4) | Lo) << (8 * I);
  }
  return V;
}

/// What the debugger asks for with qXfer:features:read.  It describes the
/// registers above: their names, their widths and the numbers this target
/// gives them.
static std::string targetDescription() {
  std::string XML = "<?xml version=\"1.0\"?>\n"
                    "<!DOCTYPE target SYSTEM \"gdb-target.dtd\">\n"
                    "<target version=\"1.0\">\n"
                    "  <architecture>c166</architecture>\n"
                    "  <feature name=\"org.llvm.c166.core\">\n";
  for (unsigned I = 0; I != unsigned(Slot::NumSlots); ++I) {
    const RegDesc &R = Regs[I];
    XML += "    <reg name=\"";
    XML += R.Name;
    // regnum is the protocol's own numbering, and the protocol says that is
    // the position in the "g" packet: a client that has not read this
    // description at all - which is any client without an XML parser, LLDB
    // among them - numbers the registers that way, and the two have to agree
    // or "p" reads the wrong register.  The DWARF number is a different
    // numbering for a different purpose, the one the unwind information is
    // written in, and goes in the attribute meant for it.
    XML += "\" bitsize=\"" + utostr(R.Bits) + "\" regnum=\"" + utostr(I) +
           "\" dwarf_regnum=\"" + utostr(R.Dwarf) + "\"";
    // The program counter is what a debugger needs told apart from the rest.
    if (StringRef(R.Name) == "pc")
      XML += " type=\"code_ptr\"";
    else if (StringRef(R.Name) == "r0")
      XML += " type=\"data_ptr\"";
    XML += "/>\n";
  }
  XML += "  </feature>\n</target>\n";
  return XML;
}

/// The conversation.
class GDBServer {
public:
  GDBServer(Machine &M, int InFD, int OutFD)
      : M(M), InFD(InFD), OutFD(OutFD) {}

  /// Talk until the debugger goes away.  Returns what the program exited with,
  /// or -1 if the protocol broke down.
  int run();

private:
  Machine &M;
  std::set<uint32_t> Breakpoints;
  /// Set once the program has finished, since a debugger may keep asking
  /// afterwards and every answer from then on is "it exited".
  std::optional<uint16_t> ExitCode;
  bool Detached = false;

  // -- framing ---------------------------------------------------------

  /// Where the debugger is.  Two descriptors rather than one because stdin and
  /// stdout are two; a socket is the same number twice.
  int InFD;
  int OutFD;

  void writeAll(StringRef Bytes);
  std::optional<char> readByte();
  /// The body of the next packet, or nothing at end of input.  Acknowledges
  /// what it reads, and re-reads a packet whose checksum does not match.
  std::optional<std::string> readPacket();
  void sendPacket(StringRef Body);
  void sendEmpty() { sendPacket(""); }
  /// Whether the debugger has sent an interrupt while we were running.
  bool interrupted();

  // -- packets ---------------------------------------------------------

  void handle(StringRef Body);
  void handleQuery(StringRef Body);
  void handleBreakpoint(StringRef Body, bool Insert);
  void readMemory(StringRef Args);
  void writeMemory(StringRef Args);
  void readRegisters();
  void writeRegisters(StringRef Hex);
  void readOneRegister(StringRef Args);
  void writeOneRegister(StringRef Args);
  /// Run until a breakpoint, an interrupt, or the program stopping.  \p Steps
  /// of zero means one instruction and then stop, which is what "s" is.
  void resume(bool SingleStep);
  /// The reply that says why we stopped.
  void reportStop();
};

std::optional<char> GDBServer::readByte() {
#ifdef LLVM_ON_UNIX
  char C;
  ssize_t N = sys::RetryAfterSignal(ssize_t(-1), ::read, InFD, &C, size_t(1));
  if (N != 1)
    return std::nullopt;
  return C;
#else
  int C = std::getchar();
  if (C == EOF)
    return std::nullopt;
  return char(C);
#endif
}

/// Everything written to the debugger goes through here, so that there is one
/// place that knows whether the far end is stdout or a socket.
void GDBServer::writeAll(StringRef Bytes) {
#ifdef LLVM_ON_UNIX
  while (!Bytes.empty()) {
    ssize_t N = sys::RetryAfterSignal(ssize_t(-1), ::write, OutFD,
                                      Bytes.data(), Bytes.size());
    if (N <= 0)
      return;
    Bytes = Bytes.drop_front(size_t(N));
  }
#else
  std::fwrite(Bytes.data(), 1, Bytes.size(), stdout);
  std::fflush(stdout);
#endif
}

std::optional<std::string> GDBServer::readPacket() {
  for (;;) {
    // Skip anything that is not the start of a packet.  An interrupt outside a
    // run has nothing to interrupt, so it is dropped here.
    std::optional<char> C;
    do {
      C = readByte();
      if (!C)
        return std::nullopt;
    } while (*C != '$');

    std::string Body;
    unsigned Sum = 0;
    for (;;) {
      C = readByte();
      if (!C)
        return std::nullopt;
      if (*C == '#')
        break;
      Sum += uint8_t(*C);
      Body.push_back(*C);
    }

    std::optional<char> Hi = readByte(), Lo = readByte();
    if (!Hi || !Lo)
      return std::nullopt;
    unsigned Want = (hexDigitValue(*Hi) << 4) | hexDigitValue(*Lo);
    if ((Sum & 0xFF) != Want) {
      writeAll("-");
      continue;
    }
    writeAll("+");
    return Body;
  }
}

void GDBServer::sendPacket(StringRef Body) {
  unsigned Sum = 0;
  for (char C : Body)
    Sum += uint8_t(C);
  std::string Out = "$";
  Out += Body;
  Out += "#";
  Out += hexdigit((Sum >> 4) & 0xF, /*LowerCase=*/true);
  Out += hexdigit(Sum & 0xF, /*LowerCase=*/true);
  writeAll(Out);
}

bool GDBServer::interrupted() {
#ifdef LLVM_ON_UNIX
  struct pollfd P = {InFD, POLLIN, 0};
  if (::poll(&P, 1, 0) <= 0)
    return false;
  std::optional<char> C = readByte();
  // 0x03 is the interrupt; anything else during a run is not ours to act on.
  return C && *C == '\x03';
#else
  // Without a way to look at stdin without blocking there is no way to notice
  // an interrupt, so a run ends only at a breakpoint or a step limit.
  return false;
#endif
}

void GDBServer::reportStop() {
  if (ExitCode) {
    sendPacket("W" + utohexstr(*ExitCode & 0xFF, /*LowerCase=*/true));
    return;
  }
  // Everything that is not the program finishing is reported as a trap, which
  // is what a breakpoint, a step and an interrupt all are to a debugger.
  sendPacket("S05");
}

void GDBServer::resume(bool SingleStep) {
  if (ExitCode) {
    reportStop();
    return;
  }

  // Always step once before looking at the breakpoints, so that continuing
  // from a breakpoint does not stop on it again.
  do {
    if (!M.step()) {
      if (M.Stop == StopReason::Exited)
        ExitCode = M.ExitCode;
      reportStop();
      return;
    }
    if (SingleStep)
      break;
    if (interrupted())
      break;
  } while (!Breakpoints.count((uint32_t(M.CSP) << 16) | M.IP));

  reportStop();
}

void GDBServer::readRegisters() {
  std::string Out;
  for (unsigned I = 0; I != unsigned(Slot::NumSlots); ++I)
    appendRegHex(Out, readReg(M, Slot(I)), Regs[I].Bits);
  sendPacket(Out);
}

void GDBServer::writeRegisters(StringRef Hex) {
  for (unsigned I = 0; I != unsigned(Slot::NumSlots); ++I) {
    unsigned Digits = Regs[I].Bits / 4;
    if (Hex.size() < Digits) {
      sendPacket("E01");
      return;
    }
    std::optional<uint32_t> V = parseRegHex(Hex.take_front(Digits), Regs[I].Bits);
    if (!V) {
      sendPacket("E01");
      return;
    }
    writeReg(M, Slot(I), *V);
    Hex = Hex.drop_front(Digits);
  }
  sendPacket("OK");
}

void GDBServer::readOneRegister(StringRef Args) {
  unsigned Num;
  if (Args.getAsInteger(16, Num)) {
    sendPacket("E01");
    return;
  }
  if (Num >= unsigned(Slot::NumSlots)) {
    sendPacket("E01");
    return;
  }
  std::string Out;
  appendRegHex(Out, readReg(M, Slot(Num)), Regs[Num].Bits);
  sendPacket(Out);
}

void GDBServer::writeOneRegister(StringRef Args) {
  auto [NumText, Hex] = Args.split('=');
  unsigned Num;
  if (NumText.getAsInteger(16, Num)) {
    sendPacket("E01");
    return;
  }
  if (Num >= unsigned(Slot::NumSlots)) {
    sendPacket("E01");
    return;
  }
  std::optional<uint32_t> V = parseRegHex(Hex, Regs[Num].Bits);
  if (!V) {
    sendPacket("E01");
    return;
  }
  writeReg(M, Slot(Num), *V);
  sendPacket("OK");
}

void GDBServer::readMemory(StringRef Args) {
  auto [AddrText, LenText] = Args.split(',');
  uint64_t Addr, Len;
  if (AddrText.getAsInteger(16, Addr) || LenText.getAsInteger(16, Len)) {
    sendPacket("E01");
    return;
  }
  // The addresses are physical, which is what the debug information holds and
  // what the whole 24 bit bus is addressed by.
  std::string Out;
  for (uint64_t I = 0; I != Len; ++I) {
    uint8_t B = M.read8(uint32_t(Addr + I));
    Out.push_back(hexdigit(B >> 4, /*LowerCase=*/true));
    Out.push_back(hexdigit(B & 0xF, /*LowerCase=*/true));
  }
  sendPacket(Out);
}

void GDBServer::writeMemory(StringRef Args) {
  auto [Head, Data] = Args.split(':');
  auto [AddrText, LenText] = Head.split(',');
  uint64_t Addr, Len;
  if (AddrText.getAsInteger(16, Addr) || LenText.getAsInteger(16, Len) ||
      Data.size() != 2 * Len) {
    sendPacket("E01");
    return;
  }
  for (uint64_t I = 0; I != Len; ++I) {
    unsigned Hi = hexDigitValue(Data[2 * I]);
    unsigned Lo = hexDigitValue(Data[2 * I + 1]);
    if (Hi == unsigned(-1) || Lo == unsigned(-1)) {
      sendPacket("E01");
      return;
    }
    M.write8(uint32_t(Addr + I), uint8_t((Hi << 4) | Lo));
  }
  sendPacket("OK");
}

void GDBServer::handleBreakpoint(StringRef Body, bool Insert) {
  // "Z0,addr,kind" - a software breakpoint.  Nothing is written into the
  // program: the simulator checks the address itself, which is what a
  // simulator can do and a part cannot.
  if (!Body.starts_with("0,") && !Body.starts_with("1,")) {
    sendEmpty();
    return;
  }
  StringRef Rest = Body.drop_front(2);
  StringRef AddrText = Rest.split(',').first;
  uint64_t Addr;
  if (AddrText.getAsInteger(16, Addr)) {
    sendPacket("E01");
    return;
  }
  if (Insert)
    Breakpoints.insert(uint32_t(Addr));
  else
    Breakpoints.erase(uint32_t(Addr));
  sendPacket("OK");
}

void GDBServer::handleQuery(StringRef Body) {
  if (Body.starts_with("qSupported")) {
    sendPacket("PacketSize=4000;qXfer:features:read+");
    return;
  }
  if (Body.starts_with("qXfer:features:read:")) {
    StringRef Rest = Body.drop_front(strlen("qXfer:features:read:"));
    auto [Annex, Range] = Rest.split(':');
    if (Annex != "target.xml") {
      sendPacket("E00");
      return;
    }
    auto [OffText, LenText] = Range.split(',');
    uint64_t Off, Len;
    if (OffText.getAsInteger(16, Off) || LenText.getAsInteger(16, Len)) {
      sendPacket("E01");
      return;
    }
    std::string XML = targetDescription();
    if (Off >= XML.size()) {
      sendPacket("l");
      return;
    }
    StringRef Chunk = StringRef(XML).substr(Off, Len);
    // "m" means there is more after this, "l" that this is the last of it.
    sendPacket(((Off + Chunk.size() < XML.size() ? "m" : "l") + Chunk).str());
    return;
  }
  if (Body == "qAttached") {
    // The program was started by us rather than attached to, so a kill is what
    // a detach means.
    sendPacket("0");
    return;
  }
  if (Body == "qC") {
    sendPacket("QC1");
    return;
  }
  if (Body == "qfThreadInfo") {
    sendPacket("m1");
    return;
  }
  if (Body == "qsThreadInfo") {
    sendPacket("l");
    return;
  }
  // Anything unrecognised is answered with the empty packet, which is how the
  // protocol says "not supported".
  sendEmpty();
}

void GDBServer::handle(StringRef Body) {
  if (Body.empty()) {
    sendEmpty();
    return;
  }

  switch (Body[0]) {
  case '?':
    reportStop();
    return;
  case 'g':
    readRegisters();
    return;
  case 'G':
    writeRegisters(Body.drop_front());
    return;
  case 'p':
    readOneRegister(Body.drop_front());
    return;
  case 'P':
    writeOneRegister(Body.drop_front());
    return;
  case 'm':
    readMemory(Body.drop_front());
    return;
  case 'M':
    writeMemory(Body.drop_front());
    return;
  case 'c':
    resume(/*SingleStep=*/false);
    return;
  case 's':
    resume(/*SingleStep=*/true);
    return;
  case 'Z':
    handleBreakpoint(Body.drop_front(), /*Insert=*/true);
    return;
  case 'z':
    handleBreakpoint(Body.drop_front(), /*Insert=*/false);
    return;
  case 'H':
    // There is one thread and it is always the one being talked about.
    sendPacket("OK");
    return;
  case 'k':
  case 'D':
    Detached = true;
    if (Body[0] == 'D')
      sendPacket("OK");
    return;
  case 'q':
  case 'Q':
    handleQuery(Body);
    return;
  case 'v':
    if (Body.starts_with("vCont?")) {
      sendPacket("vCont;c;s");
      return;
    }
    if (Body.starts_with("vCont;c")) {
      resume(/*SingleStep=*/false);
      return;
    }
    if (Body.starts_with("vCont;s")) {
      resume(/*SingleStep=*/true);
      return;
    }
    sendEmpty();
    return;
  default:
    sendEmpty();
    return;
  }
}

int GDBServer::run() {
  while (!Detached) {
    std::optional<std::string> Body = readPacket();
    if (!Body)
      break;
    handle(*Body);
  }
  return ExitCode ? int(*ExitCode) : 0;
}

} // end anonymous namespace

#ifdef LLVM_ON_UNIX
/// Wait for one debugger on \p Port of the loopback interface and hand back the
/// socket, or -1 with the reason printed.
static int acceptOneDebugger(int Port) {
  int Listen = ::socket(AF_INET, SOCK_STREAM, 0);
  if (Listen < 0) {
    errs() << "c166-sim: cannot make a socket: " << sys::StrError() << "\n";
    return -1;
  }
  int One = 1;
  ::setsockopt(Listen, SOL_SOCKET, SO_REUSEADDR, &One, sizeof(One));

  struct sockaddr_in Addr = {};
  Addr.sin_family = AF_INET;
  Addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  Addr.sin_port = htons(uint16_t(Port));
  if (::bind(Listen, (struct sockaddr *)&Addr, sizeof(Addr)) < 0 ||
      ::listen(Listen, 1) < 0) {
    errs() << "c166-sim: cannot listen on port " << Port << ": "
           << sys::StrError() << "\n";
    ::close(Listen);
    return -1;
  }

  // A port of zero asks the system for a free one, and then the number is only
  // knowable from here - so it is printed, and a script reads it rather than
  // guessing a port and racing whatever else wanted it.
  socklen_t Len = sizeof(Addr);
  if (::getsockname(Listen, (struct sockaddr *)&Addr, &Len) == 0)
    errs() << "listening on port " << ntohs(Addr.sin_port) << "\n";
  errs().flush();

  int Conn = sys::RetryAfterSignal(-1, ::accept, Listen,
                                   (struct sockaddr *)nullptr,
                                   (socklen_t *)nullptr);
  ::close(Listen);
  if (Conn < 0) {
    errs() << "c166-sim: nothing connected: " << sys::StrError() << "\n";
    return -1;
  }
  return Conn;
}
#endif

int c166sim::serveGDB(Machine &M, int Port) {
  if (Port < 0) {
    GDBServer Server(M, STDIN_FILENO, STDOUT_FILENO);
    return Server.run();
  }
#ifdef LLVM_ON_UNIX
  int Conn = acceptOneDebugger(Port);
  if (Conn < 0)
    return -1;
  GDBServer Server(M, Conn, Conn);
  int Result = Server.run();
  ::close(Conn);
  return Result;
#else
  errs() << "c166-sim: --gdb-port needs sockets, which this build has not\n";
  return -1;
#endif
}
