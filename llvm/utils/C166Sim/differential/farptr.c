/* Compiled twice: once for c166 and run in the simulator, once for the host
   and run natively.  The two outputs must match exactly.

   Pointer arithmetic that crosses a segment boundary, which is the place a
   16 bit machine with a 24 bit bus is most likely to be quietly wrong.

   A far pointer here is a linear 24 bit address zero extended into 32 bits,
   so arithmetic on one is a plain 32 bit add and a carry out of the offset
   has to land in the segment.  Getting that wrong does not fail loudly: the
   access simply goes to the same offset in the wrong segment, which is a
   location that usually exists.  So the walk below steps a pointer one byte
   at a time across a boundary rather than sampling either side of it, and
   checks the addresses it produces as values as well as using them.

   The region walked is chosen for the crossing rather than for the part: it
   is plain memory in the simulator and nothing on a real XC164CM, which has
   no storage there.  What is under test is the compiler's address
   arithmetic, not the memory map.

   On the host the same span is an ordinary array and the same arithmetic is
   ordinary pointer arithmetic, which is what makes the two comparable. */
#ifdef __c166__
static void put(char c) { *(__far volatile unsigned char *)0xFF0000 = c; }
#else
#include <stdio.h>
static void put(char c) { putchar(c); }
#endif

typedef __UINT8_TYPE__ u8;
typedef __UINT16_TYPE__ u16;
typedef __UINT32_TYPE__ u32;

static void puthex(u32 v, int d) {
  for (int i = d - 1; i >= 0; i--) put("0123456789abcdef"[(v >> (i*4)) & 0xF]);
  put('\n');
}

/* 0x03FC00 to 0x040400, so the span straddles the 0x040000 boundary between
   segment 3 and segment 4 with a kilobyte on either side.  The room matters
   twice over.  A block move only tests the crossing if it is long enough to
   reach it - and whatever it writes past the crossing has to land inside the
   range the checksums cover, or a move that went to the wrong segment would
   corrupt only bytes nothing looks at and the test would pass anyway. */
#define BASE 0x03FC00ul
#define SPAN 2048

#ifdef __c166__
typedef __far u8 *bytep;
typedef __far u16 *wordp;
#define AT(a)  ((bytep)(u32)(a))
#define ATW(a) ((wordp)(u32)(a))
/* The address a pointer denotes, as a number.  On the target that is the
   pointer itself; on the host it is where the array stands in for it. */
#define ADDR(p) ((u32)(p))
#else
static u8 space[SPAN];
typedef u8 *bytep;
typedef u16 *wordp;
#define AT(a)  (space + ((u32)(a) - BASE))
#define ATW(a) ((wordp)(void *)AT(a))
#define ADDR(p) (BASE + (u32)((u8 *)(p) - space))
#endif

static u8 pattern(u16 i) { return (u8)(i * 7u + (i >> 3) + 1u); }

/* Step one byte at a time the whole way across the boundary, writing through
   the pointer as it goes, and check both what was written and the address the
   pointer holds at each step. */
static void walk(void) {
  bytep p = AT(BASE);
  for (u16 i = 0; i < SPAN; i++) {
    *p = pattern(i);
    p++;
  }
  /* The pointer has carried out of the offset SPAN times; where it ends up
     says whether the carry reached the segment. */
  puthex(ADDR(p), 8);

  u32 sum = 0;
  p = AT(BASE);
  for (u16 i = 0; i < SPAN; i++) {
    sum = sum * 31u + *p;
    if ((i & 63) == 0) puthex(ADDR(p), 8);
    p++;
  }
  puthex(sum, 8);
}

/* Indexing rather than stepping: the index is scaled and added in one go, so
   the carry comes out of a different place. */
static void indexed(void) {
  bytep base = AT(BASE);
  u32 sum = 0;
  for (u16 i = 0; i < SPAN; i++)
    sum = sum * 33u + base[i];
  puthex(sum, 8);

  /* Word sized, so the index is scaled by two and the bit shifted out of it
     has to reach the segment as well.  Both ends are even, so no access
     straddles the boundary itself. */
  wordp w = ATW(BASE);
  for (u16 i = 0; i < SPAN / 2; i++)
    w[i] = (u16)(i * 3u + 1u);
  sum = 0;
  for (u16 i = 0; i < SPAN / 2; i++)
    sum = sum * 37u + w[i];
  puthex(sum, 8);
  puthex(ADDR(&w[SPAN / 2 - 1]), 8);
}

/* Differences and comparisons of two pointers on opposite sides of the
   boundary.  A difference that wrapped inside the segment would come out
   0xFF00 short, and a comparison that only looked at the offset would put the
   later pointer first. */
static void relations(void) {
  bytep lo = AT(BASE);
  bytep mid = AT(0x040000ul);
  bytep hi = AT(BASE + SPAN - 1);

  puthex((u32)(mid - lo), 8);
  puthex((u32)(hi - lo), 8);
  puthex((u32)(hi - mid), 8);

  put(lo < mid ? 'y' : 'n');
  put(mid < hi ? 'y' : 'n');
  put(hi < lo ? 'y' : 'n');
  put(lo == AT(BASE) ? 'y' : 'n');
  put(mid == lo ? 'y' : 'n');
  put('\n');

  /* Stepping backwards over the boundary, which borrows rather than carries. */
  bytep p = AT(0x040004ul);
  for (int i = 0; i < 9; i++) {
    puthex(ADDR(p), 8);
    p--;
  }
}

/* The same crossing reached through arithmetic the compiler cannot fold,
   because the amount only shows up at run time. */
static volatile u16 step;

static void dynamic(void) {
  bytep p = AT(BASE);
  u32 sum = 0;
  for (int i = 0; i < 8; i++) {
    step = (u16)(37 + i * 11);
    p += step;
    sum = sum * 41u + *p;
    puthex(ADDR(p), 8);
  }
  puthex(sum, 8);
}

/* The block moves, which are a library call rather than inline code once the
   size is not a constant, and which for far pointers are entry points of
   their own.  Each one below spans the boundary, so a routine that stepped
   only the offset would stop at 0x040000 and write the rest somewhere else.

   The size goes through a volatile to keep it out of reach of the inline
   expansion, which is what makes these reach the library at all. */
static volatile u16 count;

static u32 checksum(u32 at, u16 n) {
  bytep p = AT(at);
  u32 h = 0;
  for (u16 i = 0; i < n; i++)
    h = h * 31u + p[i];
  return h;
}

static void blocks(void) {
  /* Fill across the boundary: 0x03FF00 + 400 reaches 0x040090. */
  count = 400;
  __builtin_memset(AT(0x03FF00ul), 0x5A, count);
  puthex(checksum(BASE, SPAN), 8);

  for (u16 i = 0; i < SPAN; i++)
    AT(BASE)[i] = pattern(i);

  /* Copy whose source crosses the boundary, to a destination that does not.
     Every one of these is long enough that the offset carries; a move that
     stopped at 0x040000 and continued at the foot of the same segment would
     leave both ranges wrong. */
  count = 400;
  __builtin_memcpy(AT(0x040100ul), AT(0x03FF00ul), count);
  puthex(checksum(BASE, SPAN), 8);

  /* And one whose destination crosses it instead. */
  count = 400;
  __builtin_memcpy(AT(0x03FF00ul), AT(0x040100ul), count);
  puthex(checksum(BASE, SPAN), 8);

  /* Overlapping moves that cross, in both directions, which is where memmove
     has to choose which way to walk. */
  for (u16 i = 0; i < SPAN; i++)
    AT(BASE)[i] = pattern((u16)(i + 3));
  count = 400;
  __builtin_memmove(AT(0x03FF80ul), AT(0x03FF00ul), count);
  puthex(checksum(BASE, SPAN), 8);

  count = 400;
  __builtin_memmove(AT(0x03FF00ul), AT(0x03FF80ul), count);
  puthex(checksum(BASE, SPAN), 8);

  /* Exactly up to and away from the boundary. */
  count = 1;
  __builtin_memset(AT(0x03FFFFul), 0x11, count);
  count = 1;
  __builtin_memset(AT(0x040000ul), 0x22, count);
  puthex(checksum(0x03FFFCul, 8), 8);
}

int main(void) {
  walk();
  indexed();
  relations();
  dynamic();
  blocks();
  return 0;
}
