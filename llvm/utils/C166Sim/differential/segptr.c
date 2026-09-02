/* Compiled twice: once for c166 and run in the simulator, once for the host
   and run natively.  The two outputs must match exactly.

   A segment-confined far pointer, which is __seg: the same 24 bit address in
   the same 32 bits as a __far pointer, with the promise that arithmetic on it
   stays inside its own segment.  The compiler takes the promise and steps only
   the low sixteen bits.

   farptr.c beside this walks a __far pointer across a segment boundary and
   checks that the carry lands in the segment.  This is the other half of that:
   the same shapes of arithmetic - stepping, indexing, differences,
   comparisons, block moves - inside one segment, where the promise holds and
   the answers must come out the same as the host's.  A compiler that took the
   promise and got the arithmetic wrong anyway would show up here, and only
   here: nothing about a confined pointer is visible in the values it produces,
   so the only way to check it is to use one.

   The region is inside segment 4 with room at both ends, so no step here goes
   near a boundary.  What happens to a pointer that leaves its segment is
   undefined and is not tested, because it has no host answer to be compared
   against; llvm/docs/C166Usage.md says what it does on the part. */
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

/* 0x040400 to 0x040C00, a kilobyte clear of the segment 3/4 boundary below it
   and a long way from the one above. */
#define BASE 0x040400ul
#define SPAN 2048

#ifdef __c166__
typedef __seg u8 *bytep;
typedef __seg u16 *wordp;
typedef __far u8 *farbytep;
#define AT(a)  ((bytep)(u32)(a))
#define ATW(a) ((wordp)(u32)(a))
/* The address a pointer denotes, as a number.  On the target that is the
   pointer itself; on the host it is where the array stands in for it. */
#define ADDR(p) ((u32)(p))
#else
static u8 space[SPAN];
typedef u8 *bytep;
typedef u16 *wordp;
typedef u8 *farbytep;
#define AT(a)  (space + ((u32)(a) - BASE))
#define ATW(a) ((wordp)(void *)AT(a))
#define ADDR(p) (BASE + (u32)((u8 *)(p) - space))
#endif

static u8 pattern(u16 i) { return (u8)(i * 7u + (i >> 3) + 1u); }

/* Step one byte at a time the whole way, writing through the pointer as it
   goes, and check both what was written and the address the pointer holds. */
static void walk(void) {
  bytep p = AT(BASE);
  for (u16 i = 0; i < SPAN; i++) {
    *p = pattern(i);
    p++;
  }
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

/* Indexing rather than stepping: the index is scaled and added in one go. */
static void indexed(void) {
  bytep base = AT(BASE);
  u32 sum = 0;
  for (u16 i = 0; i < SPAN; i++)
    sum = sum * 33u + base[i];
  puthex(sum, 8);

  wordp w = ATW(BASE);
  for (u16 i = 0; i < SPAN / 2; i++)
    w[i] = (u16)(i * 3u + 1u);
  sum = 0;
  for (u16 i = 0; i < SPAN / 2; i++)
    sum = sum * 37u + w[i];
  puthex(sum, 8);
  puthex(ADDR(&w[SPAN / 2 - 1]), 8);
}

/* A difference across a call, which is the shape that has to be right rather
   than merely close: the result is a ptrdiff_t, and ptrdiff_t is sixteen bits
   wide here whichever space its operands came from, so a compiler that worked
   the subtraction out in the width of the pointer has a value two bytes wider
   than the slot it is returning it in.  Nothing catches that inside one
   function, where the value flows on as it is; it takes a return.

   Negative differences are here for the same reason.  A difference computed in
   thirty two bits and one computed in sixteen agree on every positive answer
   that fits, so a test with only those in it passes either way. */
__attribute__((noinline)) static int segdiff(bytep a, bytep b) { return a - b; }
__attribute__((noinline)) static int fardiff(farbytep a, farbytep b) {
  return a - b;
}
__attribute__((noinline)) static int neardiff(const u8 *a, const u8 *b) {
  return a - b;
}

static void differences(void) {
  bytep lo = AT(BASE);
  bytep hi = AT(BASE + 300);
  puthex((u32)segdiff(hi, lo), 8);
  puthex((u32)segdiff(lo, hi), 8);
  puthex((u32)fardiff(hi, lo), 8);
  puthex((u32)fardiff(lo, hi), 8);

  static const u8 buf[64];
  puthex((u32)neardiff(buf + 40, buf), 8);
  puthex((u32)neardiff(buf, buf + 40), 8);
}

/* Differences and comparisons, which are the operations that read the pointer
   as a number rather than stepping it.  A difference is a ptrdiff_t, which is
   sixteen bits wide here whichever space its operands are in. */
static void relations(void) {
  bytep lo = AT(BASE);
  bytep mid = AT(BASE + SPAN / 2);
  bytep hi = AT(BASE + SPAN - 1);

  puthex((u32)(u16)(mid - lo), 4);
  puthex((u32)(u16)(hi - lo), 4);
  puthex((u32)(u16)(hi - mid), 4);

  put(lo < mid ? 'y' : 'n');
  put(mid < hi ? 'y' : 'n');
  put(hi < lo ? 'y' : 'n');
  put(lo == AT(BASE) ? 'y' : 'n');
  put(mid == lo ? 'y' : 'n');
  put('\n');

  /* Backwards, which borrows out of the offset rather than carrying into it. */
  bytep p = AT(BASE + 8);
  for (int i = 0; i < 9; i++) {
    puthex(ADDR(p), 8);
    p--;
  }
}

/* Arithmetic the compiler cannot fold, because the amount only shows up at run
   time and so the add is a real add rather than a bigger constant offset. */
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

/* Dropping the promise, which needs no cast, and making it, which does.  Both
   are the same 32 bits, so what is checked here is that they still denote the
   same place after the round trip. */
static void conversions(void) {
  bytep p = AT(BASE + 100);
  *p = 0x5A;
  farbytep f = p;
  puthex(ADDR(f), 8);
  puthex(*f, 2);
#ifdef __c166__
  bytep back = (__seg u8 *)f;
#else
  bytep back = f;
#endif
  puthex(ADDR(back), 8);
  puthex(*back, 2);
  put(back == p ? 'y' : 'n');
  put('\n');
}

/* A structure reached through one, so the offsets come from field layout
   rather than from an index. */
struct Rec {
  u16 a;
  u8 b;
  u16 c[4];
};

static void fields(void) {
#ifdef __c166__
  __seg struct Rec *r = (__seg struct Rec *)(u32)(BASE + 64);
#else
  struct Rec *r = (struct Rec *)(void *)AT(BASE + 64);
#endif
  r->a = 0x1234;
  r->b = 0x56;
  for (int i = 0; i < 4; i++)
    r->c[i] = (u16)(0x1000 + i);
  puthex(r->a, 4);
  puthex(r->b, 2);
  for (int i = 0; i < 4; i++)
    puthex(r->c[i], 4);
  puthex(ADDR(&r->c[3]), 8);
}

/* The block moves, which go through the far entry points because a confined
   pointer is a far pointer to everything below instruction selection.  The
   size goes through a volatile to keep the copy out of reach of the inline
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
  for (u16 i = 0; i < SPAN; i++)
    AT(BASE)[i] = pattern(i);

  count = 400;
  __builtin_memset(AT(BASE + 0x100), 0x5A, count);
  puthex(checksum(BASE, SPAN), 8);

  count = 400;
  __builtin_memcpy(AT(BASE + 0x400), AT(BASE + 0x100), count);
  puthex(checksum(BASE, SPAN), 8);

  count = 400;
  __builtin_memmove(AT(BASE + 0x180), AT(BASE + 0x100), count);
  puthex(checksum(BASE, SPAN), 8);

  count = 400;
  __builtin_memmove(AT(BASE + 0x100), AT(BASE + 0x180), count);
  puthex(checksum(BASE, SPAN), 8);
}

int main(void) {
  walk();
  indexed();
  differences();
  relations();
  dynamic();
  conversions();
  fields();
  blocks();
  return 0;
}
