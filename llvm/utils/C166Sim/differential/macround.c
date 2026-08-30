/* c166-flags: -mcpu=xc16x */
/* Compiled twice: once for c166 and run in the simulator, once for the host
   and run natively.  The two outputs must match exactly.

   "(a * b + 0x8000) >> 16" is a fixed point product rounded to its high word,
   and on a part with the coprocessor it is one instruction: the rounding
   forms add 00 0000 8000H to the accumulator and clear MAL, so MAH alone is
   the answer.

   What is being checked is that the two agree, and the case to worry about is
   the addend carrying out of the product's top bit.  A signed 16 by 16
   product reaches 2^30 and an unsigned one 2^32 - 2^17, so adding 0x8000
   should never wrap either - which is the argument that lets the forty bit
   accumulator stand in for a thirty two bit one here.  The operands below
   walk both extremes of the range so that if it did wrap, it would show. */
#ifdef __c166__
static void put(char c) { *(__far volatile unsigned char *)0xFF0000 = c; }
#else
#include <stdio.h>
static void put(char c) { putchar(c); }
#endif

typedef __UINT32_TYPE__ u32;
typedef __INT32_TYPE__ s32;
typedef __INT16_TYPE__ s16;
typedef __UINT16_TYPE__ u16;

static void puthex(u32 v, int d) {
  for (int i = d - 1; i >= 0; i--) put("0123456789abcdef"[(v >> (i * 4)) & 0xF]);
  put('\n');
}

/* Both extremes of the operand range, the values either side of them, and a
   few that land near a rounding boundary. */
static const s16 svals[] = {
  0, 1, -1, 2, -2, 3, -3, 127, -128, 255, -256,
  32767, -32768, 32766, -32767, 16384, -16384, 16383, -16383,
  21845, -21846, 4660, -4661, 1000, -1000,
};
static const u16 uvals[] = {
  0, 1, 2, 3, 127, 128, 255, 256, 16383, 16384, 16385,
  32767, 32768, 32769, 43690, 60000, 65534, 65535,
};
#define NS ((int)(sizeof svals / sizeof svals[0]))
#define NU ((int)(sizeof uvals / sizeof uvals[0]))

/* Through volatiles, so that the operands are not constant folded and the
   instruction actually runs. */
static volatile s16 sa, sb;
static volatile u16 ua, ub;

static s16 round_signed(s16 a, s16 b) {
  return (s16)((((s32)a * (s32)b) + 0x8000) >> 16);
}

static u16 round_unsigned(u16 a, u16 b) {
  return (u16)((((u32)a * (u32)b) + 0x8000) >> 16);
}

/* The same rounding where the low word is also wanted, which is not the
   idiom and must stay an ordinary multiply: clearing MAL would lose it. */
static s32 round_keeping_low(s16 a, s16 b) {
  s32 m = ((s32)a * (s32)b) + 0x8000;
  return m;
}

int main(void) {
  for (int i = 0; i < NS; i++) {
    for (int j = 0; j < NS; j++) {
      sa = svals[i];
      sb = svals[j];
      puthex((u32)(s32)round_signed(sa, sb), 4);
    }
  }

  for (int i = 0; i < NU; i++) {
    for (int j = 0; j < NU; j++) {
      ua = uvals[i];
      ub = uvals[j];
      puthex(round_unsigned(ua, ub), 4);
    }
  }

  for (int i = 0; i < NS; i++) {
    sa = svals[i];
    sb = svals[NS - 1 - i];
    puthex((u32)round_keeping_low(sa, sb), 8);
  }

  return 0;
}
