/* c166-flags: -mcpu=xc16x */
/* Compiled twice: once for c166 and run in the simulator, once for the host
   and run natively.  The two outputs must match exactly.

   An FIR filter's delay line, which is what CoMACM exists for: it multiplies,
   accumulates, and writes the word it read one step back down the buffer, so
   the line shifts while the taps are summed.  The design note in
   lib/Target/C166/README.txt is about whether to select that; this is the
   shape it would have to get right, and the measurement it quotes was taken
   on these two functions.

   The direction matters and is the instruction's rather than a choice: each
   word moves toward index 0, so the newest sample goes at the end and the
   oldest leaves from the front.  A filter written the other way round is not
   this instruction.

   The delay line is __dpram because IDX0 reaches the dual-port RAM and
   nothing else; the taps are read through an ordinary pointer and can live
   anywhere.  */

#ifdef __c166__
static void put(char c) { *(__far volatile unsigned char *)0xFF0000 = c; }
#else
#include <stdio.h>
static void put(char c) { putchar(c); }
#define __dpram
#endif

typedef __UINT16_TYPE__ u16;
typedef __UINT32_TYPE__ u32;
typedef __INT32_TYPE__ s32;
typedef short s16;

static void puthex(u32 v, int d) {
  for (int i = d - 1; i >= 0; i--) put("0123456789abcdef"[(v >> (i * 4)) & 0xF]);
  put('\n');
}

#define N 16
static const s16 taps[N] = {3,   -7,  11,  -13, 17,  -19, 23,  -29,
                            31,  -37, 41,  -43, 47,  -53, 59,  -61};

/* Two lines rather than one, so that the two ways of filtering are each other's
   check rather than the same buffer twice. */
static __dpram s16 lineC[N], lineA[N];

/* What the compiler produces today: the store is in the loop, which is the
   only shape CoMACM could ever be formed from - written the textbook way, the
   shift becomes a memmove long before the backend sees it. */
static s32 fir_c(s16 sample) {
  s32 acc = (s32)taps[0] * lineC[0];
  for (int i = 1; i < N; i++) {
    acc += (s32)taps[i] * lineC[i];
    lineC[i - 1] = lineC[i];
  }
  lineC[N - 1] = sample;
  return acc;
}

/* __C166_MAC__ rather than __c166__: a part without the coprocessor compiles
   the C version on both sides, which is what lets the per-part sweep build
   this for a C167 as well. */
#ifdef __C166_MAC__
/* The same thing as one repeated instruction.  The accumulator is seeded with
   the first tap because CoMACM would otherwise write below the start of the
   line, and the whole sequence is one asm statement because the accumulator is
   a machine resource the compiler does not model. */
static s32 fir_asm(s16 sample) {
  u16 lo, hi;
  s32 first = (s32)taps[0] * lineA[0];
  u16 seed_lo = (u16)first, seed_hi = (u16)((u32)first >> 16);
  void *xi = (void *)(lineA + 1);
  void *hp = (void *)(taps + 1);
  __asm__ volatile("mov idx0, %2\n\t"
                   "coload %5, %6\n\t"
                   "repeat 15 times comacm [idx0+], [%3+]\n\t"
                   "costore %0, mal\n\tcostore %1, mah"
                   : "=&r"(lo), "=&r"(hi), "+r"(xi), "+r"(hp)
                   : "r"(seed_lo), "r"(seed_lo), "r"(seed_hi)
                   : "memory");
  lineA[N - 1] = sample;
  return (s32)(((u32)hi << 16) | lo);
}
#else
static s32 fir_asm(s16 sample) {
  s32 acc = (s32)taps[0] * lineA[0];
  for (int i = 1; i < N; i++) {
    acc += (s32)taps[i] * lineA[i];
    lineA[i - 1] = lineA[i];
  }
  lineA[N - 1] = sample;
  return acc;
}
#endif

int main(void) {
  /* A stream long enough to fill the line twice over and then some, with
     sign changes and a value that overflows a word product. */
  for (int k = 0; k < 40; k++) {
    s16 s = (s16)((k * 2731) ^ (k << 9));
    s32 a = fir_c(s), b = fir_asm(s);
    puthex((u32)a, 8);
    /* Printed rather than compared, so that a disagreement between the two is
       a difference in the output and not a branch that hides it. */
    puthex((u32)b, 8);
  }
  /* And the lines themselves, which is where a shift in the wrong direction or
     by the wrong amount shows up. */
  for (int i = 0; i < N; i++) {
    puthex((u16)lineC[i], 4);
    puthex((u16)lineA[i], 4);
  }
  return 0;
}
