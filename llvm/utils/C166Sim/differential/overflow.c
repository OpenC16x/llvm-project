/* Compiled twice: once for c166 and run in the simulator, once for the host
   and run natively.  The two outputs must match exactly.

   __builtin_mul_overflow, which on this core is answered out of a flag rather
   than worked out.  MUL and MULU set V when the product will not fit in a
   word, which is the same question the builtin asks, so the generated code is
   one multiply and a branch on cc_V - no second look at MDH and, for the
   signed case, no sign extension of MDL to compare against it.

   That equivalence is the whole of what is being tested, and it is not
   visible in the answer alone: a wrong flag shows up only where the product
   is near the edge of a word.  So the operands below are the edges - the
   largest and smallest a word holds, the square roots of those, zero, one and
   minus one - crossed with each other, and both the product and the flag are
   printed.  The product is printed even when the builtin says it overflowed,
   because the wrapped value is defined and a backend that got the flag right
   and the product wrong would otherwise pass.

   The types are fixed width rather than int, because int is a word here and
   two words on the machine this is compared against. */
#ifdef __c166__
static void put(char c) { *(__far volatile unsigned char *)0xFF0000 = c; }
#else
#include <stdio.h>
static void put(char c) { putchar(c); }
#endif

typedef __UINT16_TYPE__ u16;
typedef __INT16_TYPE__ s16;
typedef __UINT32_TYPE__ u32;

static void puthex(u32 v, int d) {
  for (int i = d - 1; i >= 0; i--) put("0123456789abcdef"[(v >> (i*4)) & 0xF]);
  put('\n');
}

/* Volatile so that nothing is folded into a constant at compile time, which
   would test the constant folder instead of the multiply. */
static volatile s16 sa, sb;
static volatile u16 ua, ub;

static const s16 signed_ops[] = {
  0, 1, -1, 2, -2, 3, -3, 181, -181, 182, -182,
  255, -255, 256, -256, 257, -257,
  0x7FFF, (s16)0x8000, 0x7FFE, (s16)0x8001, 100, -100, 327, -327,
};
#define NS ((int)(sizeof signed_ops / sizeof signed_ops[0]))

static const u16 unsigned_ops[] = {
  0u, 1u, 2u, 3u, 255u, 256u, 257u, 100u, 255u,
  0xFFFFu, 0xFFFEu, 0x8000u, 0x7FFFu, 0x0100u, 0x0101u,
};
#define NU ((int)(sizeof unsigned_ops / sizeof unsigned_ops[0]))

int main(void) {
  for (int i = 0; i < NS; i++)
    for (int j = 0; j < NS; j++) {
      sa = signed_ops[i];
      sb = signed_ops[j];
      s16 r = 0;
      int o = __builtin_mul_overflow(sa, sb, &r);
      puthex((u16)r, 4);
      put(o ? '1' : '0');
      put('\n');
    }

  for (int i = 0; i < NU; i++)
    for (int j = 0; j < NU; j++) {
      ua = unsigned_ops[i];
      ub = unsigned_ops[j];
      u16 r = 0;
      int o = __builtin_mul_overflow(ua, ub, &r);
      puthex(r, 4);
      put(o ? '1' : '0');
      put('\n');
    }

  /* The flag used where a branch reads it directly rather than a value being
     made from it, which is the shape the backend most wants to get right. */
  for (int i = 0; i < NS; i++) {
    sa = signed_ops[i];
    sb = signed_ops[NS - 1 - i];
    s16 r = 0;
    if (__builtin_mul_overflow(sa, sb, &r))
      put('V');
    else
      puthex((u16)r, 4);
  }
  put('\n');
  return 0;
}
