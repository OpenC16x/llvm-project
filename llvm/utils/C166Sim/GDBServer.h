//===-- GDBServer.h - Serve the machine to a debugger -----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// The GDB remote serial protocol, spoken over stdin and stdout, so that a
// debugger can drive the simulator instead of the simulator running a program
// to completion on its own.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_UTILS_C166SIM_GDBSERVER_H
#define LLVM_UTILS_C166SIM_GDBSERVER_H

namespace c166sim {

class Machine;

/// Serve \p M over stdin and stdout until the debugger detaches or the program
/// finishes.  Returns the program's exit code, or a negative number if the
/// conversation itself failed.
int serveGDB(Machine &M);

} // namespace c166sim

#endif
