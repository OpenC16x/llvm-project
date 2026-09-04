//===-- GDBServer.h - Serve the machine to a debugger -----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// The GDB remote serial protocol, so that a debugger can drive the simulator
// instead of the simulator running a program to completion on its own.
//
// It is spoken over stdin and stdout, which is what GDB's "target remote |"
// connects to, or over a TCP socket, which is what a debugger that has no
// pipe form needs - LLDB among them.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_UTILS_C166SIM_GDBSERVER_H
#define LLVM_UTILS_C166SIM_GDBSERVER_H

namespace c166sim {

class Machine;

/// Serve \p M until the debugger detaches or the program finishes.  Returns the
/// program's exit code, or a negative number if the conversation itself failed.
///
/// \p Port of -1 speaks over stdin and stdout.  Otherwise one connection is
/// accepted on that TCP port of the loopback interface and the conversation
/// happens there; a port of 0 asks the system for a free one, and the number it
/// chose is printed to stderr before the wait, so that a script can read it
/// rather than guess.
int serveGDB(Machine &M, int Port = -1);

} // namespace c166sim

#endif
