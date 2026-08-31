/* c166-flags: -mcpu=xc16x */
/* Compiled twice: once for c166 and run in the simulator, once for the host
   and run natively.  The two outputs must match exactly.

   32 bit signed minimum and maximum, which the coprocessor does in one
   comparison against its 40 bit accumulator: CoLOAD puts one operand in,
   CoMIN or CoMAX replaces it with whichever is wanted, and two CoSTOREs bring
   the answer back.  Without the unit the type legalizer builds a tree of five
   basic blocks - the two words compared in order with the equal case carried
   between them - so this is four straight-line instructions against about
   twenty and a branch on every one.

   What is checked here is that the 40 bit comparison is the 32 bit one the
   program asked for.  CoLOAD sign extends into 40 bits and the CoSTOREs
   truncate back to 32, so the interesting cases are the ones where the sign
   extension is what decides: a negative against a positive, values that differ
   only in the high word, values that differ only in the low word with the high
   words equal, and the two ends of the range. */
#ifdef __c166__
static void put(char c) { *(__far volatile unsigned char *)0xFF0000 = c; }
#else
#include <stdio.h>
static void put(char c) { putchar(c); }
#endif

typedef __UINT32_TYPE__ u32;
typedef __INT32_TYPE__ s32;

static void puthex(u32 v, int d) {
  for (int i = d - 1; i >= 0; i--) put("0123456789abcdef"[(v >> (i * 4)) & 0xF]);
  put('\n');
}

/* Volatile so that nothing is folded at compile time: the answer has to come
   out of the instructions rather than out of the constant folder. */
static volatile s32 a[] = {
    0,          1,           -1,          2,          -2,
    32767,      -32768,      32768,       -32769,     65535,
    -65536,     65536,       123456,      -123456,    2000000000,
    -2000000000, 2147483647, (-2147483647 - 1), 305419896, -305419896,
    /* Words that agree in one half and differ in the other, which is where a
       comparison done word at a time gets it wrong if the order is wrong. */
    0x00010000, 0x0001FFFF, 0x0001FFFE, (s32)0xFFFF0000, (s32)0xFFFF0001,
    (s32)0x80000000, (s32)0x8000FFFF, 0x7FFF0000, 0x7FFFFFFF, (s32)0x00008000,
};
#define N ((int)(sizeof a / sizeof a[0]))

int main(void) {
  /* Every ordered pair, both ways round, both operations.  That covers equal
     operands too, which is the case where either answer is the same value and
     a wrong one would still look plausible on its own. */
  for (int i = 0; i < N; i++)
    for (int j = 0; j < N; j++) {
      s32 x = a[i], y = a[j];
      s32 hi = x > y ? x : y;
      s32 lo = x < y ? x : y;
      /* Fold the pair into one line so the output stays a manageable size and
         a single wrong answer still changes it. */
      puthex((u32)hi ^ ((u32)lo << 1), 8);
    }
  return 0;
}
