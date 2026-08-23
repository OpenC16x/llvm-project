/* Compiled twice: once for c166 and run in the simulator, once for the host
   and run natively.  The two outputs must match exactly.

   This one is about how values get from one function to another.  Four words
   go in registers and the rest on the ABI stack, so anything wider than a word
   is already split across registers before the fifth argument makes it a stack
   one, and a struct is whatever shape it is.  None of that is visible in the
   result of any single call - it shows up as a wrong answer three calls later,
   which is what a differential test is for.

   What is deliberately never printed is a size or an offset.  Nothing on this
   part is aligned to more than a word, so a struct is laid out more tightly
   than the host lays out the same declaration, and the two are entitled to
   disagree about that.  Only the values that come out are compared. */
#ifdef __c166__
static void put(char c) { *(__far volatile unsigned char *)0xFF0000 = c; }
#define FAR __far
#else
#include <stdio.h>
static void put(char c) { putchar(c); }
#define FAR
#endif
#include <stdarg.h>

typedef __UINT16_TYPE__ u16; typedef __INT16_TYPE__ s16;
typedef __UINT32_TYPE__ u32; typedef __INT32_TYPE__ s32;
typedef __UINT64_TYPE__ u64; typedef __INT64_TYPE__ s64;

static void puthex(u32 v, int d) {
  for (int i = d - 1; i >= 0; i--) put("0123456789abcdef"[(v >> (i*4)) & 0xF]);
  put('\n');
}

/* --- arguments past the registers, in every width ------------------- */

static u32 w8(u16 a, u16 b, u16 c, u16 d, u16 e, u16 f, u16 g, u16 h) {
  return ((u32)a << 24) ^ ((u32)b << 20) ^ ((u32)c << 16) ^ ((u32)d << 12) ^
         ((u32)e << 8) ^ ((u32)f << 4) ^ (u32)g ^ ((u32)h * 0x10001u);
}

/* Mixed widths, so that a 32 bit value straddles the boundary between the
   registers and the stack rather than landing tidily on one side. */
static u32 mixed(u16 a, u32 b, u16 c, u64 d, u16 e, u32 f, u16 g) {
  return (u32)a * 2 + b * 3 + (u32)c * 5 + (u32)d * 7 + (u32)(d >> 32) * 11 +
         (u32)e * 13 + f * 17 + (u32)g * 19;
}

/* A byte argument is promoted to a word, and a signed one has to arrive sign
   extended rather than merely narrow. */
static s32 bytes(signed char a, unsigned char b, signed char c,
                 unsigned char d, signed char e, unsigned char f,
                 signed char g, unsigned char h) {
  return (s32)a + b * 3 + (s32)c * 5 + d * 7 + (s32)e * 11 + f * 13 +
         (s32)g * 17 + h * 19;
}

/* --- structs by value, at every awkward size ------------------------ */

struct s1 { char a; };
struct s2 { char a, b; };
struct s3 { char a, b, c; };
struct s4 { u16 a, b; };
struct s5 { u32 a; char b; };
struct s8 { u16 a, b, c, d; };
struct s9 { u32 a, b; char c; };
struct s16 { u32 a, b, c, d; };
struct nested { struct s4 inner; u32 x; struct s2 tail; };

static u32 take1(struct s1 s) { return (u32)(unsigned char)s.a; }
static u32 take2(struct s2 s) { return (u32)(unsigned char)s.a * 3 + (unsigned char)s.b; }
static u32 take3(struct s3 s) { return ((u32)(unsigned char)s.a * 3 + (unsigned char)s.b) * 5 + (unsigned char)s.c; }
static u32 take4(struct s4 s) { return (u32)s.a * 7 + s.b; }
static u32 take5(struct s5 s) { return s.a * 11 + (unsigned char)s.b; }
static u32 take8(struct s8 s) { return (((u32)s.a * 3 + s.b) * 5 + s.c) * 7 + s.d; }
static u32 take9(struct s9 s) { return s.a * 13 + s.b * 17 + (unsigned char)s.c; }
static u32 take16(struct s16 s) { return ((s.a * 3 + s.b) * 5 + s.c) * 7 + s.d; }
static u32 takenested(struct nested s) {
  return take4(s.inner) * 3 + s.x * 5 + take2(s.tail);
}
/* And a struct behind seven other arguments, so it lands on the stack. */
static u32 late_struct(u16 a, u16 b, u16 c, u16 d, u16 e, u16 f, u16 g,
                       struct s8 s) {
  return (u32)(a + b + c + d + e + f + g) * 31 + take8(s);
}

static struct s1  make1(char v)  { struct s1 s = {v}; return s; }
static struct s2  make2(char v)  { struct s2 s = {v, (char)(v + 1)}; return s; }
static struct s3  make3(char v)  { struct s3 s = {v, (char)(v+1), (char)(v+2)}; return s; }
static struct s4  make4(u16 v)   { struct s4 s = {v, (u16)(v * 3)}; return s; }
static struct s5  make5(u32 v)   { struct s5 s = {v, (char)v}; return s; }
static struct s8  make8(u16 v)   { struct s8 s = {v, (u16)(v+1), (u16)(v*7), (u16)~v}; return s; }
static struct s9  make9(u32 v)   { struct s9 s = {v, ~v, (char)(v >> 8)}; return s; }
static struct s16 make16(u32 v)  { struct s16 s = {v, v*3, v*5, ~v}; return s; }
static struct nested makenested(u16 v) {
  struct nested s; s.inner = make4(v); s.x = (u32)v * 65537u; s.tail = make2((char)v);
  return s;
}

static u32 structs(void) {
  u32 h = 0;
  for (u16 v = 1; v < 300; v += 37) {
    h = h * 31 + take1(make1((char)v));
    h = h * 31 + take2(make2((char)v));
    h = h * 31 + take3(make3((char)v));
    h = h * 31 + take4(make4(v));
    h = h * 31 + take5(make5((u32)v * 70001u));
    h = h * 31 + take8(make8(v));
    h = h * 31 + take9(make9((u32)v * 70001u));
    h = h * 31 + take16(make16((u32)v * 70001u));
    h = h * 31 + takenested(makenested(v));
    h = h * 31 + late_struct(v, (u16)(v+1), (u16)(v+2), (u16)(v+3), (u16)(v+4),
                             (u16)(v+5), (u16)(v+6), make8(v));
    /* A struct assigned through a pointer, and one copied whole. */
    struct s16 a = make16(v), b;
    b = a;
    struct s16 *p = &b;
    p->c = p->a + p->b;
    h = h * 31 + take16(b);
  }
  return h;
}

/* --- calling through a pointer -------------------------------------- */

static u32 f_add(u32 a, u32 b) { return a + b; }
static u32 f_sub(u32 a, u32 b) { return a - b; }
static u32 f_mul(u32 a, u32 b) { return a * b; }
static u32 f_xor(u32 a, u32 b) { return a ^ b; }
typedef u32 (*binop)(u32, u32);
static const binop ops[4] = {f_add, f_sub, f_mul, f_xor};

/* A function pointer as an argument, and one returned from a function. */
static u32 apply(binop f, u32 a, u32 b) { return f(a, b); }
static binop pick(int i) { return ops[i & 3]; }

static u32 indirect(void) {
  u32 h = 0;
  for (int i = 0; i < 4; i++)
    for (u32 v = 1; v < 0x40000000u; v *= 7) {
      h = h * 31 + ops[i](v, v ^ 0x5A5A5A5Au);
      h = h * 31 + apply(ops[i], v, 3);
      h = h * 31 + apply(pick(i + 1), v, v >> 3);
    }
  return h;
}

/* --- varargs of every width ----------------------------------------- */

/* The widths are spelled out rather than left to int, which is sixteen bits
   here and thirty two on the host.  A double is what a float becomes on the
   way in, so that is what comes out. */
static u32 vmix(int n, ...) {
  va_list ap; u32 h = 0; va_start(ap, n);
  for (int i = 0; i < n; i++) {
    switch (i & 3) {
    case 0: h = h * 31 + (u32)va_arg(ap, s32); break;
    case 1: { u64 v = va_arg(ap, u64);
              h = h * 31 + (u32)v + (u32)(v >> 32); break; }
    case 2: h = h * 31 + (u32)(s32)va_arg(ap, double); break;
    /* What a pointer points at, never the pointer: an address is two bytes
       here and eight on the host, and neither is the other's. */
    case 3: { const u32 *p = va_arg(ap, const u32 *);
              h = h * 31 + (p ? *p : 0xFFFFFFFFu); break; }
    }
  }
  va_end(ap);
  return h;
}

/* A va_list passed on to something else, which is the case that has to keep
   working after the first function has already read part of the list. */
static u32 vtail(int n, va_list ap) {
  u32 h = 0;
  for (int i = 0; i < n; i++) h = h * 7 + (u32)va_arg(ap, s32);
  return h;
}
static u32 vhead(int n, ...) {
  va_list ap; va_start(ap, n);
  u32 first = (u32)va_arg(ap, s32);
  u32 rest = vtail(n - 1, ap);
  va_end(ap);
  return first * 3 + rest;
}

static u32 variadic(void) {
  u32 h = 0;
  static const u32 anchor = 0xC0FFEE01u;
  h = h * 31 + vmix(4, (s32)-12345, (u64)0x123456789ABCull, 2.5, &anchor);
  h = h * 31 + vmix(8, (s32)1, (u64)2, 3.0, (const u32 *)0,
                    (s32)-1, (u64)0xFFFFFFFFFFFFFFFFull, -4.5, &anchor);
  for (s32 i = -3; i < 4; i++)
    h = h * 31 + vhead(5, i, i * 100, -i, i * 10000, i + 7);
  return h;
}

/* --- callee saved registers across a call --------------------------- */

/* Enough live values across the call that some have to be in registers the
   callee is obliged to put back, and some have to be spilled. */
static u32 opaque(u32 x) { return x * 2654435761u + 1; }
static u32 preserved(u32 seed) {
  u32 a = seed + 1, b = seed + 2, c = seed + 3, d = seed + 4;
  u32 e = seed + 5, f = seed + 6, g = seed + 7, i = seed + 8;
  u32 j = seed + 9, k = seed + 10, l = seed + 11, m = seed + 12;
  u32 t = opaque(seed);
  t += opaque(a) ^ opaque(b);
  t += opaque(c) ^ opaque(d);
  t += opaque(e) ^ opaque(f);
  t += opaque(g) ^ opaque(i);
  return t + a * 2 + b * 3 + c * 5 + d * 7 + e * 11 + f * 13 + g * 17 +
         i * 19 + j * 23 + k * 29 + l * 31 + m * 37;
}

/* --- recursion, with something live across the recursive call ------- */

static u32 tree(u32 n, u32 acc) {
  if (n == 0) return acc + 1;
  u32 left = tree(n - 1, acc * 3 + 1);
  u32 right = tree(n - 1, acc * 5 + 2);
  return left ^ (right * 7) ^ (acc + n);
}

/* Mutual recursion, so that neither can be turned into a loop. */
static u32 odd_(u32 n);
static u32 even_(u32 n) { return n == 0 ? 1 : odd_(n - 1) * 3 + 1; }
static u32 odd_(u32 n) { return n == 0 ? 0 : even_(n - 1) * 5 + 2; }

/* --- a variable sized local ----------------------------------------- */

static u32 vla(int n) {
  u32 h = 0;
  {
    u16 a[n];
    for (int i = 0; i < n; i++) a[i] = (u16)(i * i + n);
    for (int i = 0; i < n; i++) h = h * 13 + a[i];
    {
      char b[n * 2];
      for (int i = 0; i < n * 2; i++) b[i] = (char)(h + i);
      for (int i = 0; i < n * 2; i++) h = h * 7 + (unsigned char)b[i];
    }
    /* The first array has to still be there after the second one is gone. */
    for (int i = 0; i < n; i++) h = h * 11 + a[i];
  }
  return h;
}

int main(void) {
  u32 h = 0;
  for (u16 v = 0; v < 500; v += 61) {
    h = h * 3 + w8(v, (u16)(v+1), (u16)(v+2), (u16)(v+3), (u16)(v+4),
                   (u16)(v+5), (u16)(v+6), (u16)(v+7));
    h = h * 3 + mixed(v, (u32)v * 70001u, (u16)(v+1),
                      (u64)v * 0x100000001ull, (u16)(v+2),
                      (u32)v * 12345u, (u16)(v+3));
    h = h * 3 + (u32)bytes((signed char)v, (unsigned char)(v+1),
                           (signed char)(v+2), (unsigned char)(v+3),
                           (signed char)(v+4), (unsigned char)(v+5),
                           (signed char)(v+6), (unsigned char)(v+7));
  }
  puthex(h, 8);

  puthex(structs(), 8);
  puthex(indirect(), 8);
  puthex(variadic(), 8);

  h = 0;
  for (u32 s = 0; s < 8; s++) h = h * 31 + preserved(s * 0x1234567u);
  puthex(h, 8);

  h = tree(9, 1);
  for (u32 n = 0; n < 12; n++) h = h * 31 + even_(n) + odd_(n);
  puthex(h, 8);

  h = 0;
  for (int n = 1; n <= 20; n++) h = h * 31 + vla(n);
  puthex(h, 8);
  return 0;
}
