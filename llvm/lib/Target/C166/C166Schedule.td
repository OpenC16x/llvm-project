//===-- C166Schedule.td - C166 scheduling model ------------*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// What each instruction costs, from Table 11 of the C166 Family Instruction
// Set Manual (V2.0, 2001-03) - the same table llvm/utils/C166Sim reads to count
// what a program executed.
//
// The unit is a state, which is one CPU clock period, and not a cycle.  The
// manual's own table is in states and every measurement in this backend is in
// states, so converting would mean carrying a ratio nothing here has checked
// past the one instruction the C166S V2 handbook gives both figures for.  A
// scheduling model's unit is arbitrary as long as it is consistent; this one
// says which it is rather than leaving a reader to assume the usual.
//
// Two things follow from the machine rather than from the table.  One
// instruction issues at a time and nothing is buffered - this is an in-order
// core with a four stage pipeline and no second issue slot - so IssueWidth is
// one, MicroOpBufferSize is zero, and there is a single resource that
// everything holds for as long as it runs.  And the post-RA scheduler stays
// off: there is nothing for it to gain here, and the gaps section 4.2 of the
// C167CR manual requires between writing CP, a DPP or SP and using it are not
// dependencies it can see, so it would be free to close them.  The simulator
// would catch that now - see llvm/test/tools/c166-sim/pipeline.s - which is a
// reason to be confident rather than a reason to risk it.
//
// This does not add evidence.  The simulator reads the same table, so what
// this makes is two readers of one manual agreeing; what it buys is that the
// cost model can be derived from those numbers instead of from counting
// instructions, and that llvm-mca and anything else asking the target how long
// something takes gets an answer.
//
//===----------------------------------------------------------------------===//

def C166Model : SchedMachineModel {
  let IssueWidth = 1;
  let MicroOpBufferSize = 0;

  // An ordinary access is two states, like an ordinary instruction; the four
  // state one is MOV[B] Rn, [Rm + #data16], which is WriteLoadDisp below.
  let LoadLatency = 2;

  // The divide, which is the slowest thing the part does.
  let HighLatency = 20;

  // Not modelled.  The prediction bit of JMPA and CALLA is always left at 0
  // and nothing here can measure what setting it would buy, which is the same
  // gap seen from the other side; llvm/lib/Target/C166/README.txt says so.
  let MispredictPenalty = 0;

  let PostRAScheduler = false;

  // Only the instructions whose time is not the table's last row are listed
  // below, and that row - "all other instructions: 2" - is what the default
  // gives them.  Listing all several hundred to say "two" would be a table
  // with one value in it.
  let CompleteModel = false;
}

let SchedModel = C166Model in {

// One of everything.  There is no second issue slot, no separate load unit and
// no pipelining of one instruction against another that this could describe,
// so an instruction holds the machine for exactly as long as it runs.
def C166Unit : ProcResource<1>;

/// The table's last row: "all other instructions: 2".
def WriteOrdinary : SchedWriteRes<[C166Unit]> {
  let Latency = 2;
  let ReleaseAtCycles = [2];
}

/// MUL and MULU.
def WriteMul : SchedWriteRes<[C166Unit]> {
  let Latency = 10;
  let ReleaseAtCycles = [10];
}

/// DIV, DIVL, DIVU and DIVLU.
def WriteDiv : SchedWriteRes<[C166Unit]> {
  let Latency = 20;
  let ReleaseAtCycles = [20];
}

/// A branch or call that goes somewhere, and the returns, which always do.
/// Table 11 gives the same forms two states when they fall through, which is
/// a property of the run rather than of the instruction and so is not here;
/// the simulator charges it because it knows which way the branch went.
def WriteBranch : SchedWriteRes<[C166Unit]> {
  let Latency = 4;
  let ReleaseAtCycles = [4];
}

/// MOV[B] Rn, [Rm + #data16], the one move that is not two states.
def WriteLoadDisp : SchedWriteRes<[C166Unit]> {
  let Latency = 4;
  let ReleaseAtCycles = [4];
}

/// The coprocessor's rounding multiplies, which the C166S V2 Architecture
/// Overview Handbook's instruction set summary gives two cycles where it gives
/// the plain forms one.  Everything else of the unit's is one cycle there and
/// two states here, which is what the default already says - that the two
/// agree is worth having read rather than assumed.
def WriteCoRound : SchedWriteRes<[C166Unit]> {
  let Latency = 4;
  let ReleaseAtCycles = [4];
}

def : InstRW<[WriteMul], (instrs MULrr, MULUrr)>;
def : InstRW<[WriteDiv], (instrs DIVr, DIVUr, DIVLr, DIVLUr)>;
def : InstRW<[WriteLoadDisp], (instrs MOV16rm, MOVB8rm)>;
def : InstRW<[WriteCoRound], (instrs CoMUL_rr_rnd, CoMULu_rr_rnd)>;
def : InstRW<[WriteBranch],
             (instrs CALLS, CALLR, PCALL, JMPS, TRAP,
                     RET, RETI, RETP, RETS,
                     CALLI, CALLA, JMPA, JMPAcc, JMPI, JMPR, JMPRcc,
                     JB, JBC, JNB, JNBS)>;

} // SchedModel = C166Model
