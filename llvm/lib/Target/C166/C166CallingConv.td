//===-- C166CallingConv.td - Calling Conventions for C166 --*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
// This describes the calling conventions for the C166 architecture.
//
// Up to four word sized arguments are passed in R2-R5, everything else goes
// on the ABI stack addressed through R0.  Values are returned in R2-R5.
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
// Return value calling convention
//===----------------------------------------------------------------------===//

def RetCC_C166 : CallingConv<[
  // Promote i1/i8 return values to i16.
  CCIfType<[i1, i8], CCPromoteToType<i16>>,

  CCIfType<[i16], CCAssignToReg<[R2, R3, R4, R5]>>
]>;

//===----------------------------------------------------------------------===//
// Argument calling conventions
//===----------------------------------------------------------------------===//

def CC_C166 : CallingConv<[
  // Pass by value if the byval attribute is given.
  CCIfByVal<CCPassByVal<2, 2>>,

  // Promote i1/i8 arguments to i16.
  CCIfType<[i1, i8], CCPromoteToType<i16>>,

  // The first four words are passed in registers.
  CCIfType<[i16], CCAssignToReg<[R2, R3, R4, R5]>>,

  // Everything else is passed in 2 byte stack slots.
  CCIfType<[i16], CCAssignToStack<2, 2>>
]>;

// Variadic functions pass everything on the stack so that the callee can walk
// the argument list with a plain pointer.
def CC_C166_VarArg : CallingConv<[
  CCIfByVal<CCPassByVal<2, 2>>,
  CCIfType<[i1, i8], CCPromoteToType<i16>>,
  CCIfType<[i16], CCAssignToStack<2, 2>>
]>;

//===----------------------------------------------------------------------===//
// Callee saved registers
//===----------------------------------------------------------------------===//

// R1 is the frame pointer.  It is listed here so that it is preserved across
// calls either way: a function that does not need a frame pointer simply never
// touches it, and one that does gets it saved by the usual machinery.
def CSR_C166 : CalleeSavedRegs<(add R1, R12, R13, R14, R15)>;

// An interrupt handler runs on top of arbitrary code, so it has to put back
// every register it touches.  R0, the ABI stack pointer, is excluded: the
// handler shares the interrupted function's stack.
def CSR_C166_Interrupt : CalleeSavedRegs<(add R1, R2, R3, R4, R5, R6, R7, R8,
                                              R9, R10, R11, R12, R13, R14,
                                              R15)>;
