/* c166-flags: -mcpu=xc16x */
/* Compiled twice: once for c166 and run in the simulator, once for the host
   and run natively.  The two outputs must match exactly.

   "acc += a * b" with signed words onto a 32 bit accumulator, which on a part
   with the coprocessor is CoMAC and everywhere else is a multiply, two reads
   out of MDL and MDH, and an add with a carry into the second word.

   What is being checked is that the two agree, and the interesting cases are
   the ones where the 40 bit accumulator and a 32 bit one differ - or would,
   if the accumulator were allowed to keep anything.  So the sums below are
   walked up to and past where a 32 bit total wraps, with terms of both signs
   and both extremes of the operand range, since the product of the two most
   negative words is the one value a signed 16 by 16 multiply cannot represent
   in 31 bits. */
#ifdef __c166__
static void put(char c) { *(__far volatile unsigned char *)0xFF0000 = c; }
#else
#include <stdio.h>
static void put(char c) { putchar(c); }
#endif

typedef __UINT32_TYPE__ u32;
typedef __INT32_TYPE__ s32;
typedef __INT16_TYPE__ s16;

static void puthex(u32 v, int d) {
  for (int i = d - 1; i >= 0; i--) put("0123456789abcdef"[(v >> (i*4)) & 0xF]);
  put('\n');
}

static const s16 vals[] = {
  0, 1, -1, 2, -2, 127, -128, 255, -256, 1000, -1000,
  32767, -32768, 21845, -21846, 4660, -4661,
};
#define N ((int)(sizeof vals / sizeof vals[0]))

static volatile s16 va, vb;
static volatile s32 vacc;

/* Every pair, accumulated onto a running total that is carried between them,
   so a wrong high word shows up in the next term rather than only at the end. */
static void pairs(void) {
  s32 acc = 0;
  for (int i = 0; i < N; i++)
    for (int j = 0; j < N; j++) {
      va = vals[i];
      vb = vals[j];
      acc += (s32)va * (s32)vb;
      if (((i * N + j) & 31) == 0) puthex((u32)acc, 8);
    }
  puthex((u32)acc, 8);
}

/* The same one term at a time, with the accumulator going through memory so
   that each is a separate multiply-accumulate rather than a chain. */
static void singles(void) {
  for (int i = 0; i < N; i++) {
    vacc = (s32)i * 1000000;
    for (int j = 0; j < N; j++) {
      va = vals[i];
      vb = vals[j];
      vacc = vacc + (s32)va * (s32)vb;
    }
    puthex((u32)vacc, 8);
  }
}

/* A dot product, which is the shape the unit exists for. */
static s32 dot(const s16 *a, const s16 *b, int n) {
  s32 acc = 0;
  for (int i = 0; i < n; i++) acc += (s32)a[i] * (s32)b[i];
  return acc;
}

static void dots(void) {
  for (int k = 1; k <= N; k++)
    puthex((u32)dot(vals, vals + (N - k), k), 8);
}

int main(void) {
  pairs();
  singles();
  dots();
  return 0;
}
