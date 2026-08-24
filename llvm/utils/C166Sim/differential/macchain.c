/* c166-flags: -mcpu=xc16x */
/* Compiled twice: once for c166 and run in the simulator, once for the host
   and run natively.  The two outputs must match exactly.

   A dot product long enough for its running total to leave 32 bits behind.

   That is the one place where holding the total in the coprocessor across a
   loop could differ from taking it out and putting it back every time round.
   The round trip truncates to 32 bits at each step, because that is all a
   register pair holds; staying in the unit keeps 40 and truncates once at the
   end.  Both come to the true sum modulo 2^32 - the accumulator wraps at 40
   bits rather than saturating, and 2^32 divides 2^40 - so the two agree, and
   this is what checks that rather than leaving it as an argument.

   The totals are unsigned so that the wrap is defined on the reference side
   as well; the products are signed, which is what the unit multiplies.

   The longest run here is 800 terms of 32767 squared, which comes to about
   2^39.6.  That is past the top bit of the 40 bit accumulator, so the unit is
   holding what it reads as a large negative number while the true total is
   positive - the case where the extension byte matters most.  Going a further
   step, past 2^40 and round again, would take about 1024 terms and more than
   the 2 KByte of data RAM this links against; the argument covers it, and
   what is checked here is everything up to it. */
#ifdef __c166__
static void put(char c) { *(__far volatile unsigned char *)0xFF0000 = c; }
#else
#include <stdio.h>
static void put(char c) { putchar(c); }
#endif

typedef __UINT32_TYPE__ u32;
typedef __INT32_TYPE__ s32;
typedef __INT16_TYPE__ s16;

static void puthex(u32 v) {
  for (int i = 7; i >= 0; i--) put("0123456789abcdef"[(v >> (i * 4)) & 0xF]);
  put('\n');
}

/* One array used as both operands, which buys twice the run length for the
   same data RAM. */
#define N 800
static s16 a[N];
static volatile unsigned n = N;

/* Not inlined and given its length at run time, so that it stays a loop with
   the total carried round the back edge - which is the shape being tested. */
__attribute__((noinline))
static u32 dot(const s16 *x, const s16 *y, unsigned m) {
  u32 acc = 0;
  for (unsigned i = 0; i < m; i++) acc += (u32)((s32)x[i] * (s32)y[i]);
  return acc;
}

/* And with a starting value, so the total does not begin at zero. */
__attribute__((noinline))
static u32 dot_from(const s16 *x, const s16 *y, unsigned m, u32 init) {
  u32 acc = init;
  for (unsigned i = 0; i < m; i++) acc += (u32)((s32)x[i] * (s32)y[i]);
  return acc;
}

int main(void) {
  /* The largest positive term there is, so the total climbs as fast as it
     can: it passes 2^32 within five of them and 2^39 before the end. */
  for (int i = 0; i < N; i++) a[i] = 32767;

  /* Every prefix, so a total that has just crossed a boundary is printed as
     well as the ones far past it. */
  for (unsigned m = 0; m <= N; m += 7) puthex(dot(a, a, m));

  puthex(dot_from(a, a, n, 0x7FFFFFFFu));
  puthex(dot_from(a, a, n, 0x80000000u));
  puthex(dot_from(a, a, n, 1u));

  /* And terms of both signs, so the total climbs and falls across the
     boundary rather than only climbing through it. */
  for (int i = 0; i < N; i++) a[i] = (s16)(i & 1 ? -32768 : 32767);
  for (unsigned m = 0; m <= N; m += 7) puthex(dot(a, a, m));
  return 0;
}
