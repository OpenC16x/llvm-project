/* c166-flags: -mcpu=xc16x */
/* Compiled twice: once for c166 and run in the simulator, once for the host
   and run natively.  The two outputs must match exactly.

   The coprocessor's repeatable forms, driven by the repeat prefix.  These are
   the ones a filter is written with: one instruction reads a pair of words
   through two pointers, does something to the accumulator, steps both
   pointers, and does it again.  Nothing in the compiler generates them, so
   the C166 side is inline assembly and the host side is the same arithmetic
   spelled in C - which is the point, since what is being checked is that the
   simulator's idea of these instructions matches what the arithmetic means.

   Every sequence is one asm statement, because the accumulator is a machine
   resource the compiler does not model: two statements that expect it to
   survive between them stay adjacent at -O0 and do not at -O2.  The pointers
   are read-write operands for the same reason - the instruction steps them,
   so a compiler told they were inputs would reuse a register holding a value
   that is no longer there.

   Only the low 32 bits of the 40 bit accumulator are read back, through MAH
   and MAL, so every expected value below is a 32 bit truncation and the host
   side says so with an explicit cast. */
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

#define N 8
static s16 a[N] = {1, -2, 3, -4, 5, -6, 7, -8};
static s16 b[N] = {100, 200, -300, 400, -500, 600, -700, 800};
static u16 ua[N] = {1, 2, 3, 40000, 5, 6, 7, 60000};
static u16 ub[N] = {9, 8, 7, 60000, 5, 4, 3, 40000};
static s16 dst[N];

#ifdef __c166__
/* Clear the accumulator, run one repeated instruction over the two pointers,
   and read the low 32 bits back.  R0 is the ABI stack pointer here, so the
   accumulator is cleared from a register holding zero rather than with the
   "coload r0, r0" that would otherwise be the obvious way to say it. */
#define REPEAT2(insn, idxp, ptrp, out)                                         \
  do {                                                                         \
    u16 lo_, hi_;                                                              \
    void *xi_ = (void *)(idxp), *xp_ = (void *)(ptrp);                         \
    __asm__ volatile("mov idx0, %2\n\tcoload %4, %4\n\t" insn                  \
                     "\n\tcostore %0, mal\n\tcostore %1, mah"                  \
                     : "=&r"(lo_), "=&r"(hi_), "+r"(xi_), "+r"(xp_)            \
                     : "r"((u16)0)                                             \
                     : "memory");                                              \
    (out) = ((u32)hi_ << 16) | lo_;                                            \
  } while (0)
#endif

int main(void) {
  u32 got;

  /* A signed dot product: the plain repeated CoMAC, both pointers stepping
     forward by a word. */
#ifdef __c166__
  REPEAT2("repeat 8 times comac [idx0+], [%3+]", a, b, got);
#else
  {
    long long acc = 0;
    for (int i = 0; i < N; i++) acc += (long long)a[i] * b[i];
    got = (u32)acc;
  }
#endif
  puthex(got, 8);

  /* The same with unsigned words, where the product is zero extended. */
#ifdef __c166__
  REPEAT2("repeat 8 times comacu [idx0+], [%3+]", ua, ub, got);
#else
  {
    long long acc = 0;
    for (int i = 0; i < N; i++) acc += (long long)ua[i] * ub[i];
    got = (u32)acc;
  }
#endif
  puthex(got, 8);

  /* Mixed signedness, both ways round: "su" is op1 signed and op2 unsigned,
     "us" the mirror, and op1 is the one behind IDX0. */
#ifdef __c166__
  REPEAT2("repeat 8 times comacsu [idx0+], [%3+]", a, ub, got);
#else
  {
    long long acc = 0;
    for (int i = 0; i < N; i++) acc += (long long)a[i] * (long long)ub[i];
    got = (u32)acc;
  }
#endif
  puthex(got, 8);

#ifdef __c166__
  REPEAT2("repeat 8 times comacus [idx0+], [%3+]", ua, b, got);
#else
  {
    long long acc = 0;
    for (int i = 0; i < N; i++) acc += (long long)ua[i] * (long long)b[i];
    got = (u32)acc;
  }
#endif
  puthex(got, 8);

  /* Subtracting the products instead, which is the trailing minus. */
#ifdef __c166__
  REPEAT2("repeat 8 times comac- [idx0+], [%3+]", a, b, got);
#else
  {
    long long acc = 0;
    for (int i = 0; i < N; i++) acc -= (long long)a[i] * b[i];
    got = (u32)acc;
  }
#endif
  puthex(got, 8);

  /* Stepping backwards, which walks the same arrays from the other end. */
#ifdef __c166__
  REPEAT2("repeat 8 times comac [idx0-], [%3-]", &a[N - 1], &b[N - 1], got);
#else
  {
    long long acc = 0;
    for (int i = N - 1; i >= 0; i--) acc += (long long)a[i] * b[i];
    got = (u32)acc;
  }
#endif
  puthex(got, 8);

  /* Adding pairs of words as one 32 bit value: op2 is the high half. */
#ifdef __c166__
  REPEAT2("repeat 8 times coadd [idx0+], [%3+]", a, b, got);
#else
  {
    long long acc = 0;
    for (int i = 0; i < N; i++)
      acc += (s32)(((u32)(u16)b[i] << 16) | (u16)a[i]);
    got = (u32)acc;
  }
#endif
  puthex(got, 8);

  /* And subtracting them, doubled, which is the "2" form. */
#ifdef __c166__
  REPEAT2("repeat 8 times cosub2 [idx0+], [%3+]", a, b, got);
#else
  {
    long long acc = 0;
    for (int i = 0; i < N; i++)
      acc -= 2 * (long long)(s32)(((u32)(u16)b[i] << 16) | (u16)a[i]);
    got = (u32)acc;
  }
#endif
  puthex(got, 8);

  /* Running maximum over the same pairs, starting from zero. */
#ifdef __c166__
  REPEAT2("repeat 8 times comax [idx0+], [%3+]", a, b, got);
#else
  {
    long long acc = 0;
    for (int i = 0; i < N; i++) {
      long long t = (s32)(((u32)(u16)b[i] << 16) | (u16)a[i]);
      if (t > acc) acc = t;
    }
    got = (u32)acc;
  }
#endif
  puthex(got, 8);

  /* The count taken from MRW rather than from the instruction, which is what
     reaches past 31 repetitions.  MRW holds one less than the count. */
#ifdef __c166__
  {
    u16 lo_, hi_;
    void *xi_ = (void *)a, *xp_ = (void *)b;
    __asm__ volatile("mov mrw, %5\n\tmov idx0, %2\n\tcoload %4, %4\n\t"
                     "repeat mrw times comac [idx0+], [%3+]\n\t"
                     "costore %0, mal\n\tcostore %1, mah"
                     : "=&r"(lo_), "=&r"(hi_), "+r"(xi_), "+r"(xp_)
                     : "r"((u16)0), "r"((u16)(N - 1))
                     : "memory");
    got = ((u32)hi_ << 16) | lo_;
  }
#else
  {
    long long acc = 0;
    for (int i = 0; i < N; i++) acc += (long long)a[i] * b[i];
    got = (u32)acc;
  }
#endif
  puthex(got, 8);

  /* A pointer stepped by one of the offset registers rather than by a word,
     which is how a filter walks a buffer with a stride.  QR0 is the
     coprocessor's, in the extended space at F004H. */
#ifdef __c166__
  {
    u16 lo_, hi_;
    void *xi_ = (void *)a, *xp_ = (void *)b;
    *(volatile u16 *)0xF004 = 4; /* QR0: two words */
    __asm__ volatile("mov idx0, %2\n\tcoload %4, %4\n\t"
                     "repeat 4 times comac [idx0+], [%3+qr0]\n\t"
                     "costore %0, mal\n\tcostore %1, mah"
                     : "=&r"(lo_), "=&r"(hi_), "+r"(xi_), "+r"(xp_)
                     : "r"((u16)0)
                     : "memory");
    got = ((u32)hi_ << 16) | lo_;
  }
#else
  {
    long long acc = 0;
    for (int i = 0; i < 4; i++) acc += (long long)a[i] * b[i * 2];
    got = (u32)acc;
  }
#endif
  puthex(got, 8);

  /* Shifting the accumulator, repeated: eight shifts of one bit each. */
#ifdef __c166__
  {
    u16 lo_, hi_;
    __asm__ volatile("coload %2, %3\n\t"
                     "repeat 8 times coshl %4\n\t"
                     "costore %0, mal\n\tcostore %1, mah"
                     : "=&r"(lo_), "=&r"(hi_)
                     : "r"((u16)0x3456), "r"((u16)0x0012), "r"((u16)1)
                     : "memory");
    got = ((u32)hi_ << 16) | lo_;
  }
#else
  got = (u32)(((long long)0x00123456 << 8) & 0xFFFFFFFF);
#endif
  puthex(got, 8);

  /* A memory to memory move, which is the one form that writes through IDX0
     rather than reading through it. */
#ifdef __c166__
  {
    void *xi_ = (void *)dst, *xp_ = (void *)b;
    __asm__ volatile("mov idx0, %0\n\t"
                     "repeat 8 times comov [idx0+], [%1+]"
                     : "+r"(xi_), "+r"(xp_)
                     :
                     : "memory");
  }
#else
  for (int i = 0; i < N; i++) dst[i] = b[i];
#endif
  for (int i = 0; i < N; i++) puthex((u16)dst[i], 4);

  /* Storing the accumulator through a stepping pointer, which is the one form
     that writes a MAC register out rather than reading operands in. */
#ifdef __c166__
  {
    void *xp_ = (void *)dst;
    __asm__ volatile("coload %1, %2\n\t"
                     "repeat 8 times costore [%0+], mal"
                     : "+r"(xp_)
                     : "r"((u16)0xBEEF), "r"((u16)0)
                     : "memory");
  }
#else
  for (int i = 0; i < N; i++) dst[i] = (s16)0xBEEF;
#endif
  for (int i = 0; i < N; i++) puthex((u16)dst[i], 4);

  /* CoMACM, which multiplies and accumulates like CoMAC and additionally
     writes the word it read through IDX0 back one step behind it - moving a
     delay line along while the taps are summed.  Stepping forward by a word
     means each sample lands where the previous one was, so afterwards the
     buffer holds itself shifted down by one. */
  {
    static s16 line[N] = {10, 20, 30, 40, 50, 60, 70, 80};
#ifdef __c166__
    REPEAT2("repeat 7 times comacm [idx0+], [%3+]", &line[1], b, got);
#else
    {
      long long acc = 0;
      for (int i = 0; i < 7; i++) acc += (long long)line[1 + i] * b[i];
      got = (u32)acc;
      for (int i = 0; i < 7; i++) line[i] = line[1 + i];
    }
#endif
    puthex(got, 8);
    for (int i = 0; i < N; i++) puthex((u16)line[i], 4);
  }

  return 0;
}
