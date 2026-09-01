//===-- C166CallingConv.td - Calling Conventions for C166 --*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
// This describes the calling conventions for the C166 architecture.
//
// Up to eight word sized arguments are passed in R2-R9, everything else goes
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

  // The first eight words are passed in registers.  Measured across the
  // differential programs, against the four this used to be:
  //
  //   registers      text            states
  //     4          62286           13659159
  //     6          61702 (-0.9%)   13494156 (-1.2%)
  //     8          61248 (-1.7%)   13400158 (-1.9%)
  //    10          61248 (-1.7%)   13400158 (-1.9%)
  //
  // Nothing regressed at any of them, and ten is byte for byte identical to
  // eight because nothing there passes more than eight words - so eight is
  // where the curve stops, and the last free pair of caller saved registers
  // is left to the allocator rather than spent for nothing.
  //
  // Most of the gain is in the soft float programs, which is what one would
  // expect: a double is four words, so an operation on two of them wants
  // exactly eight.  Fifteen of the nineteen programs are unchanged.
  //
  // R6 to R11 were already caller saved and already clobbered by a call, so
  // widening into R6-R9 costs nothing in what a call destroys; and an
  // interrupt handler saved all sixteen either way.
  CCIfType<[i16], CCAssignToReg<[R2, R3, R4, R5, R6, R7, R8, R9]>>,

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

// A handler with a register bank of its own has nothing to put back: SCXT
// gave it sixteen registers nobody else was using, and POP CP hands the
// interrupted code its own back untouched.  The multiply/divide unit and the
// coprocessor are not part of a bank and are saved by the frame lowering
// either way.
def CSR_C166_Bank : CalleeSavedRegs<(add)>;
