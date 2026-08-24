/* Compiled twice: once for c166 and run in the simulator, once for the host
   and run natively.  The two outputs must match exactly.

   Division, which on this core is the one operation with a register pair
   behind it.  Three things here are worth more than the arithmetic itself:

   A 32 bit dividend with a 16 bit divisor is done as two divides rather than
   a call, and the pair is only safe because neither half can overflow - the
   first is a 16 by 16 divide, and the second divides r:lo by d with r below
   d.  Nothing about that reasoning is visible in the generated code, so the
   whole range of interesting divisors is walked against dividends that sit on
   either side of the word boundary, including the ones where the quotient
   needs both words and the ones where it needs neither.

   A divisor of one is the case where the quotient is as large as it can be,
   and 0xFFFF is the case where the first divide's quotient is zero and the
   entire answer comes out of the second.  Both are the shapes an overflow
   would appear at first.

   And "a / b" together with "a % b" is one divide, not two, with the second
   answer taken out of MDH.  The pair is written both ways round and with each
   half used alone, so that a wrong choice of which register holds which shows
   up rather than cancelling out. */
#ifdef __c166__
static void put(char c) { *(__far volatile unsigned char *)0xFF0000 = c; }
#else
#include <stdio.h>
static void put(char c) { putchar(c); }
#endif

typedef __UINT8_TYPE__ u8;
typedef __UINT16_TYPE__ u16;
typedef __INT16_TYPE__ s16;
typedef __UINT32_TYPE__ u32;
typedef __INT32_TYPE__ s32;

static void puthex(u32 v, int d) {
  for (int i = d - 1; i >= 0; i--) put("0123456789abcdef"[(v >> (i*4)) & 0xF]);
  put('\n');
}

/* Volatile so that nothing here is folded into a constant division, which
   would be the multiply-high sequence and not the code under test. */
static volatile u16 vd16;
static volatile u32 vn32;

static const u32 dividends[] = {
  0u, 1u, 2u, 0xFFFFu, 0x10000u, 0x10001u, 0x1FFFFu, 0xFFFFFFFFu,
  0xFFFF0000u, 0x0000FFFFu, 0x12345678u, 0x80000000u, 0x7FFFFFFFu,
  0xABCD0000u, 0x0001FFFEu, 0x00FF00FFu,
};
#define NDIV ((int)(sizeof dividends / sizeof dividends[0]))

static const u16 divisors[] = {
  1u, 2u, 3u, 7u, 10u, 255u, 256u, 257u, 1000u,
  0x7FFFu, 0x8000u, 0xFFFEu, 0xFFFFu,
};
#define NDVR ((int)(sizeof divisors / sizeof divisors[0]))

/* A 32 bit dividend over a divisor that started life 16 bits wide.  This is
   the shape that becomes DIVU followed by DIVLU. */
static void wide(void) {
  for (int i = 0; i < NDIV; i++)
    for (int j = 0; j < NDVR; j++) {
      vn32 = dividends[i];
      vd16 = divisors[j];
      u32 n = vn32;
      u16 d = vd16;
      puthex(n / d, 8);
      puthex(n % d, 8);
      /* Both halves of the answer at once, which is the whole pseudo. */
      puthex((n / d) ^ (n % d), 8);
    }
}

/* The same divisor reached through a signed 16 bit value, and through a
   variable the compiler cannot see the width of, to check that only the
   genuinely 16 bit case takes the two divide path and the rest still get a
   correct answer from wherever they get it. */
static void widths(void) {
  for (int i = 0; i < NDIV; i++)
    for (int j = 0; j < NDVR; j++) {
      vn32 = dividends[i];
      vd16 = divisors[j];
      u32 n = vn32;
      u32 d32 = vd16;          /* zero extended, so the two divide path */
      u8 d8 = (u8)(vd16 | 1u); /* narrower still */
      puthex(n / d32, 8);
      puthex(n % d32, 8);
      puthex(n / d8, 8);
      puthex(n % d8, 8);
      if (vn32 != 0x80000000u || (s16)vd16 != -1) {
        s32 sn = (s32)vn32;
        s16 sd = (s16)vd16;
        if (sd) {
          puthex((u32)(sn / sd), 8);
          puthex((u32)(sn % sd), 8);
        }
      }
    }
}

/* One divide answering both questions.  Written in both orders, and with each
   half used on its own, so that swapping MDL for MDH cannot pass. */
static void pairs(void) {
  for (int i = 0; i < NDVR; i++)
    for (int j = 0; j < NDVR; j++) {
      vd16 = divisors[i];
      u16 a = vd16;
      vd16 = divisors[j];
      u16 b = vd16;

      puthex(a / b, 4);
      puthex(a % b, 4);
      puthex((u16)(a % b), 4);
      puthex((u16)(a / b), 4);
      puthex((u16)(a / b) + (u16)(a % b), 4);

      s16 sa = (s16)a, sb = (s16)b;
      if (sb && !(sa == (s16)0x8000 && sb == -1)) {
        puthex((u16)(sa / sb), 4);
        puthex((u16)(sa % sb), 4);
        puthex((u16)(sa % sb), 4);
        puthex((u16)(sa / sb), 4);
      }

      /* Only one of the two used, which must still be one divide and must
         still read the right register. */
      puthex((u16)(a / b), 4);
      puthex((u16)(a % b), 4);
    }
}

int main(void) {
  wide();
  widths();
  pairs();
  return 0;
}
