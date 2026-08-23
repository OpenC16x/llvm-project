#ifdef __c166__
static void put(char c) { *(__far volatile unsigned char *)0xFF0000 = c; }
#define FAR __far
/* Copied out of the Flash into the PSRAM at startup and called across
   segments.  noinline is what keeps it a function at all, and therefore what
   keeps it in the section. */
#define PSRAMTEXT __attribute__((far, noinline, section(".psramtext")))
#else
#include <stdio.h>
static void put(char c) { putchar(c); }
#define FAR
#define PSRAMTEXT __attribute__((noinline))
#endif
typedef __UINT16_TYPE__ u16; typedef __INT16_TYPE__ s16;
typedef __UINT32_TYPE__ u32; typedef __INT32_TYPE__ s32;
typedef __UINT64_TYPE__ u64; typedef __INT64_TYPE__ s64;
typedef __SIZE_TYPE__ size_t;

static void puthex(u32 v, int d) {
  for (int i = d - 1; i >= 0; i--) put("0123456789abcdef"[(v >> (i*4)) & 0xF]);
  put('\n');
}

/* --- recursion and the stack --------------------------------------- */
static u32 ack(u32 m, u32 n) {              /* deep-ish recursion */
  if (m == 0) return n + 1;
  if (n == 0) return ack(m - 1, 1);
  return ack(m - 1, ack(m, n - 1));
}
static u32 fib(u32 n) { return n < 2 ? n : fib(n-1) + fib(n-2); }

/* --- structs by value and by reference ----------------------------- */
struct big { u16 a; u32 b; char c[7]; s16 d; };
static u32 sum_big(struct big s) {
  u32 t = s.a + s.b + (u32)s.d;
  for (int i = 0; i < 7; i++) t = t * 7 + (unsigned char)s.c[i];
  return t;
}
static struct big make_big(u16 seed) {
  struct big s; s.a = seed; s.b = seed * 65537u; s.d = -(s16)seed;
  for (int i = 0; i < 7; i++) s.c[i] = (char)(seed + i);
  return s;
}

/* --- switch, which becomes a jump table ---------------------------- */
static u32 classify(int c) {
  switch (c) {
  case 0: return 11; case 1: return 22; case 2: return 33; case 3: return 44;
  case 4: return 55; case 5: return 66; case 6: return 77; case 7: return 88;
  case 20: return 99; default: return 0xdead;
  }
}

/* --- a function that runs from RAM ---------------------------------- */
/* On the C166 this one is executed out of the PSRAM rather than the Flash,
   which is a different segment, so getting the same answer means the startup
   copy, the inter-segment call and the return all worked.
   
   It is also written under the constraint that comes with being there: a near
   call reaches only the segment it is made from, and the compiler-rt builtins
   are in the Flash, so code here must not need one.  A 32 bit multiply and a
   shift by a constant are emitted inline and are fine; a shift by a variable
   amount and a 32 bit division are calls to __ashlsi3 and __udivsi3 and are
   not.  Getting that wrong used to link and then run away into the PSRAM; the
   linker now refuses it, which is how this came to be written this way. */
static PSRAMTEXT u32 ram_mix(u32 a, u32 b) {
  u32 t = a;
  for (int i = 0; i < 8; i++) {
    t = t * 33 + b + (t >> 7);
    b = (b << 3) ^ (b >> 5) ^ t;
  }
  return t;
}

/* --- varargs -------------------------------------------------------- */
#ifdef __c166__
#include <stdarg.h>
#else
#include <stdarg.h>
#endif
static u32 vsum(int n, ...) {
  va_list ap; u32 t = 0; va_start(ap, n);
  for (int i = 0; i < n; i++) t = t * 5 + (u32)va_arg(ap, int);
  va_end(ap);
  return t;
}

/* --- 64 bit --------------------------------------------------------- */
static u64 mix64(u64 a, u64 b) { return a * b + (a >> 17) - (b << 5); }

/* --- memory --------------------------------------------------------- */
static char buf1[64], buf2[64];
static u32 mem_work(void) {
  u32 t = 0;
  for (int i = 0; i < 64; i++) buf1[i] = (char)(i * 3 + 1);
  __builtin_memset(buf2, 0x5A, sizeof buf2);
  __builtin_memcpy(buf2 + 8, buf1 + 4, 40);
  __builtin_memmove(buf2 + 10, buf2 + 8, 30);   /* overlapping, forward */
  __builtin_memmove(buf2 + 4, buf2 + 12, 30);   /* overlapping, backward */
  for (int i = 0; i < 64; i++) t = t * 17 + (unsigned char)buf2[i];
  t = t * 17 + (u32)(__builtin_memcmp(buf1, buf2, 64) != 0);
  return t;
}

/* --- pointers, arrays, and a far object ----------------------------- */
FAR u32 fartable[16];
static u32 pointer_work(void) {
  u32 t = 0;
  for (int i = 0; i < 16; i++) fartable[i] = (u32)i * 0x01010101u;
  FAR u32 *p = fartable;
  for (int i = 0; i < 16; i++) t = t * 13 + *p++;
  u32 local[8];
  for (int i = 0; i < 8; i++) local[i] = t + i;
  u32 *q = local + 7;
  for (int i = 0; i < 8; i++) t = t * 11 + *q--;
  return t;
}

int main(void) {
  puthex(ack(2, 3), 8);
  puthex(fib(16), 8);

  u32 acc = 0;
  for (u16 s = 1; s < 40; s += 3) acc = acc * 3 + sum_big(make_big(s));
  puthex(acc, 8);

  acc = 0;
  for (int i = -2; i < 24; i++) acc = acc * 9 + classify(i);
  puthex(acc, 8);

  acc = vsum(6, 1, -2, 3, -4, 5, -6);
  acc = acc * 3 + vsum(3, 1000, 2000, 3000);
  puthex(acc, 8);

  {
    static const u64 v[] = {0, 1, 0xFFFFFFFFFFFFFFFFull, 0x123456789ABCDEFull,
                            0x8000000000000000ull, 12345678901234ull};
    u64 h = 0;
    for (int i = 0; i < 6; i++)
      for (int j = 0; j < 6; j++) h = h * 31 + mix64(v[i], v[j]);
    puthex((u32)h, 8); puthex((u32)(h >> 32), 8);
  }

  puthex(mem_work(), 8);
  puthex(pointer_work(), 8);

  acc = ram_mix(0x12345678u, 0xABCDu);
  acc = acc * 7 + ram_mix(acc, 0xFFFFu);
  puthex(acc, 8);
  return 0;
}
