// RUN: %clang_cc1 -triple c166 -I %S/../../../../llvm/lib/Target/C166/startup \
// RUN:   -emit-llvm -o - %s | FileCheck %s

// The Peripheral Event Controller's half of startup/xc164cm.h, which is the
// part of that header that is arithmetic rather than a list of addresses and
// so is the part that can be wrong.  Nothing else compiles this header, so
// this is also what says it still parses.

#include "xc164cm.h"

// The pointers are in the internal RAM rather than in the register map -
// C167CR Derivatives User's Manual V3.1, Figure 5-2 - so these addresses are
// the only place in the toolchain they are written down.
_Static_assert((unsigned)&C166_SRCP(0) == 0xFCE0U, "SRCP0");
_Static_assert((unsigned)&C166_DSTP(0) == 0xFCE2U, "DSTP0");
_Static_assert((unsigned)&C166_SRCP(1) == 0xFCE4U, "SRCP1");
_Static_assert((unsigned)&C166_SRCP(7) == 0xFCFCU, "SRCP7");
_Static_assert((unsigned)&C166_DSTP(7) == 0xFCFEU, "DSTP7");

// The control word's fields: COUNT in the low byte, BWT at bit 8, INC at
// 10-9 (Table 5-4's layout).
_Static_assert(PECC_COUNT(0x1234) == 0x34, "COUNT is the low byte");
_Static_assert(PECC_WORD == 0x000 && PECC_BYTE == 0x100, "BWT");
_Static_assert(PECC_INC_NONE == 0x000, "INC 00");
_Static_assert(PECC_INC_DST == 0x200, "INC 01");
_Static_assert(PECC_INC_SRC == 0x400, "INC 10");

// And which channel an interrupt control register asks for.  "Programming a
// source to priority level 15 (ILVL = 1111B) selects the PEC channel group
// 7 … 4, programming a source to priority level 14 (ILVL = 1110B) selects the
// PEC channel group 3 … 0.  The actual PEC channel number is then determined
// by the group priority field GLVL."  So the low nibble is 14 or 15 and the
// two bits above it are the channel within the group, over the enable bit.
_Static_assert(PEC_IC(0) == 0x4E, "channel 0: level 14, group 0");
_Static_assert(PEC_IC(3) == 0x7E, "channel 3: level 14, group 3");
_Static_assert(PEC_IC(4) == 0x4F, "channel 4: level 15, group 0");
_Static_assert(PEC_IC(7) == 0x7F, "channel 7: level 15, group 3");

// A channel configured the way llvm/utils/C166Sim/differential/pec.c does it,
// so that the header and that program cannot drift apart: the same words have
// to come out.
void configure(void) {
  C166_SRCP(0) = 0xC010;
  C166_DSTP(0) = 0xC020;
  PECC0 = PECC_COUNT(12) | PECC_WORD | PECC_INC_DST;
  T3IC = PEC_IC(0);
}
// CHECK-LABEL: define {{.*}}void @configure()
// CHECK: store volatile i16 -16368, ptr inttoptr (i16 -800 to ptr)
// CHECK: store volatile i16 -16352, ptr inttoptr (i16 -798 to ptr)
// CHECK: store volatile i16 524, ptr inttoptr (i16 -320 to ptr)
// CHECK: store volatile i16 78, ptr inttoptr (i16 -158 to ptr)
