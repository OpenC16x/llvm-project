//===- IntrinsicsC166.td - Defines C166 intrinsics ---------*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines all of the C166-specific intrinsics.
//
//===----------------------------------------------------------------------===//

let TargetPrefix = "c166" in { // All intrinsics start with "llvm.c166.".

  // A whole dot product in one repeated coprocessor instruction.
  //
  // Nothing in a source program writes this.  C166MACRepeat builds it out of a
  // loop it has proved is a dot product, and it exists because a loop is not
  // something a SelectionDAG pattern can match: what the instruction needs to
  // know - the trip count, that both streams step one word, that one of them
  // is in the dual-port RAM - are facts about the loop, and this is how they
  // are carried from the pass that established them to the code that emits it.
  //
  // The first pointer is the one behind IDX0 and has to be in the dual-port
  // RAM; the second is an ordinary word pointer.  The accumulator arrives and
  // leaves as two words, low first, because that is what a 16 bit machine's
  // registers hold and what CoLOAD and CoSTORE move.  The count is how many
  // products to add, and the kind is a C166MAC::Kind saying which
  // multiply-accumulate this is.
  //
  // The last two are how far each pointer moves per repetition, in bytes and
  // signed.  Two of them is the ordinary case and is what the pointer's own
  // update code does; anything else goes into one of the unit's offset
  // registers first, which is why the value rather than a flag is carried
  // here.  A stride is not a wraparound - the part has none, and says so - so
  // these only describe a stream that keeps to a fixed distance.
  //
  // All four of those are words rather than the ints they would naturally be,
  // because an i32 constant reaching selection on a 16 bit machine is a type
  // the legalizer would have to take apart, and none of them needs the room.
  //
  // It reads memory and writes none, so it may be moved and duplicated but not
  // dropped where the two words it produces are used.
  def int_c166_comac_repeat :
      Intrinsic<[llvm_i16_ty, llvm_i16_ty],
                [llvm_ptr_ty, llvm_ptr_ty, llvm_i16_ty, llvm_i16_ty,
                 llvm_i16_ty, llvm_i16_ty, llvm_i16_ty, llvm_i16_ty],
                [IntrReadMem, IntrArgMemOnly, ImmArg<ArgIndex<4>>,
                 ImmArg<ArgIndex<5>>, ImmArg<ArgIndex<6>>,
                 ImmArg<ArgIndex<7>>]>;
}
