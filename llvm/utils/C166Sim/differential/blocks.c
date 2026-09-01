/* Compiled twice: once for c166 and run in the simulator, once for the host
   and run natively.  The two outputs must match exactly.

   Block copies and fills, at sizes either side of where the compiler stops
   writing them out and calls the runtime instead.  Both sides of both
   boundaries are here, at -O2 and at -Os, which move them: the point is that
   the answer does not depend on which the compiler picked.

   Each case checksums the whole destination rather than the part that should
   have changed, so an expansion that writes one word too many is a wrong
   answer and not a silent success.  Odd offsets are here too, because a copy
   that cannot be done in words falls back to bytes and is a different
   expansion.  */

#ifdef __c166__
static void put(char c) { *(__far volatile unsigned char *)0xFF0000 = c; }
#else
#include <stdio.h>
static void put(char c) { putchar(c); }
#endif

typedef __UINT8_TYPE__ u8;
typedef __UINT16_TYPE__ u16;
typedef __UINT32_TYPE__ u32;

static void puthex(u32 v, int digits) {
  for (int i = digits - 1; i >= 0; i--)
    put("0123456789abcdef"[(v >> (i * 4)) & 0xF]);
  put('\n');
}

/* Big enough for the largest case plus the odd offsets and a margin the
   checksum covers, so writing past the end shows up.  */
#define BUF 96
static u8 dst[BUF], src[BUF];

static u16 sum(const u8 *p, unsigned n) {
  u16 s = 0x1234;
  for (unsigned i = 0; i < n; i++)
    s = (u16)(s * 31u + p[i]);
  return s;
}

static void fill_src(void) {
  for (unsigned i = 0; i < BUF; i++)
    src[i] = (u8)(i * 7 + 1);
}

/* The sizes: 4 and 6 are either side of the copy boundary when optimising for
   size, 16 and 18 either side of it otherwise, 8 and 10 either side of the
   fill boundary for size, 32 and 34 either side of it otherwise.  The rest
   fill in around them, and 0 is here because a copy of nothing is a case of
   its own.  */
static const u16 sizes[] = {0,  1,  2,  3,  4,  5,  6,  7,  8,  10, 12,
                            14, 16, 17, 18, 20, 24, 31, 32, 33, 34, 40,
                            48, 63, 64};

int main(void) {
  fill_src();

  for (unsigned k = 0; k < sizeof sizes / sizeof sizes[0]; k++) {
    u16 n = sizes[k];
    /* Word aligned, then one byte off on each side and on both. */
    for (unsigned off = 0; off < 4; off++) {
      unsigned d = off & 1, s = (off >> 1) & 1;
      for (unsigned i = 0; i < BUF; i++)
        dst[i] = 0xA5;
      __builtin_memcpy(dst + d, src + s, n);
      puthex(sum(dst, BUF), 4);
    }

    for (unsigned off = 0; off < 2; off++) {
      for (unsigned i = 0; i < BUF; i++)
        dst[i] = 0x5A;
      __builtin_memset(dst + off, (int)(n & 0xFF), n);
      puthex(sum(dst, BUF), 4);
    }

    /* And a move, which the compiler cannot prove does not overlap: the two
       regions here really do, in both directions. */
    for (unsigned i = 0; i < BUF; i++)
      dst[i] = src[i];
    __builtin_memmove(dst + 2, dst, n);
    puthex(sum(dst, BUF), 4);
    for (unsigned i = 0; i < BUF; i++)
      dst[i] = src[i];
    __builtin_memmove(dst, dst + 2, n);
    puthex(sum(dst, BUF), 4);
  }
  return 0;
}
