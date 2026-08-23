/* Compiled twice: once for c166 and run in the simulator, once for the host
   and run natively.  The two outputs must match exactly.

   The C166 has no floating point unit, so every operation here is a call into
   the compiler-rt builtins - and those are reached with 32 and 64 bit values
   in and out, which on a 16 bit machine means two and four registers.  So this
   is as much a test of the calling convention under load as it is of the
   arithmetic.

   Results are printed as the bits rather than as a number, since that is the
   only comparison with nothing to argue about: a formatted number would only
   say the two agreed to however many digits were printed. */
#ifdef __c166__
static void put(char c) { *(__far volatile unsigned char *)0xFF0000 = c; }
#else
#include <stdio.h>
static void put(char c) { putchar(c); }
#endif

typedef __UINT16_TYPE__ u16; typedef __INT16_TYPE__ s16;
typedef __UINT32_TYPE__ u32; typedef __INT32_TYPE__ s32;
typedef __UINT64_TYPE__ u64; typedef __INT64_TYPE__ s64;

static void puthex(u32 v, int d) {
  for (int i = d - 1; i >= 0; i--) put("0123456789abcdef"[(v >> (i*4)) & 0xF]);
  put('\n');
}
static void puthex64(u64 v) {
  puthex((u32)(v >> 32), 8); puthex((u32)v, 8);
}

/* The bits of a value, which is what gets compared.  memcpy rather than a
   union or a cast, so that nothing here depends on how either compiler feels
   about aliasing. */
static u32 fbits(float f)  { u32 b; __builtin_memcpy(&b, &f, 4); return b; }
static u64 dbits(double d) { u64 b; __builtin_memcpy(&b, &d, 8); return b; }
static float  mkf(u32 b) { float f;  __builtin_memcpy(&f, &b, 4); return f; }
static double mkd(u64 b) { double d; __builtin_memcpy(&d, &b, 8); return d; }

/* Zero, one, a half, two, a denormal, the largest and smallest normals, and
   both infinities.  Negatives are made by flipping the sign bit, so each of
   these is really two. */
static const u32 fpat[] = {
  0x00000000u,  /* 0 */
  0x3F800000u,  /* 1 */
  0x3F000000u,  /* 0.5 */
  0x40000000u,  /* 2 */
  0x40490FDBu,  /* pi */
  0x00000001u,  /* the smallest denormal */
  0x007FFFFFu,  /* the largest denormal */
  0x00800000u,  /* the smallest normal */
  0x7F7FFFFFu,  /* the largest normal */
  0x7F800000u,  /* infinity */
  0x3E4CCCCDu,  /* 0.2, which is not exact */
  0x4B000000u,  /* 2^23, where the spacing reaches one */
};
#define NFPAT ((int)(sizeof fpat / sizeof fpat[0]))

static const u64 dpat[] = {
  0x0000000000000000ull, /* 0 */
  0x3FF0000000000000ull, /* 1 */
  0x3FE0000000000000ull, /* 0.5 */
  0x4000000000000000ull, /* 2 */
  0x400921FB54442D18ull, /* pi */
  0x0000000000000001ull, /* the smallest denormal */
  0x000FFFFFFFFFFFFFull, /* the largest denormal */
  0x0010000000000000ull, /* the smallest normal */
  0x7FEFFFFFFFFFFFFFull, /* the largest normal */
  0x7FF0000000000000ull, /* infinity */
  0x3FC999999999999Aull, /* 0.2 */
  0x4330000000000000ull, /* 2^52 */
};
#define NDPAT ((int)(sizeof dpat / sizeof dpat[0]))

/* A NaN is only checked for being one.  Which NaN an operation produces is not
   something the two are obliged to agree about, so the payload is not
   compared - only that the exponent is all ones with a mantissa behind it. */
static int fisnan(float f) { u32 b = fbits(f); return (b & 0x7F800000u) == 0x7F800000u && (b & 0x007FFFFFu) != 0; }
static int disnan(double d) { u64 b = dbits(d); return (b & 0x7FF0000000000000ull) == 0x7FF0000000000000ull && (b & 0x000FFFFFFFFFFFFFull) != 0; }

static u32 float_arithmetic(void) {
  u32 h = 0;
  for (int i = 0; i < NFPAT; i++)
    for (int s = 0; s < 2; s++) {
      float a = mkf(fpat[i] ^ (s ? 0x80000000u : 0));
      for (int j = 0; j < NFPAT; j++)
        for (int t = 0; t < 2; t++) {
          float b = mkf(fpat[j] ^ (t ? 0x80000000u : 0));
          float r;
          r = a + b; h = h * 31 + (fisnan(r) ? 1 : fbits(r));
          r = a - b; h = h * 31 + (fisnan(r) ? 2 : fbits(r));
          r = a * b; h = h * 31 + (fisnan(r) ? 3 : fbits(r));
          r = a / b; h = h * 31 + (fisnan(r) ? 4 : fbits(r));
          /* Every comparison, which on an unordered pair is its own answer. */
          h = h * 31 + (u32)((a == b) | ((a != b) << 1) | ((a < b) << 2) |
                             ((a <= b) << 3) | ((a > b) << 4) | ((a >= b) << 5));
        }
      h = h * 31 + fbits(-a);
      h = h * 31 + (fisnan(a * a) ? 5 : fbits(a * a));
    }
  return h;
}

static u32 double_arithmetic(void) {
  u32 h = 0;
  for (int i = 0; i < NDPAT; i++)
    for (int s = 0; s < 2; s++) {
      double a = mkd(dpat[i] ^ (s ? 0x8000000000000000ull : 0));
      for (int j = 0; j < NDPAT; j++)
        for (int t = 0; t < 2; t++) {
          double b = mkd(dpat[j] ^ (t ? 0x8000000000000000ull : 0));
          double r;
          u64 rb;
          r = a + b; rb = disnan(r) ? 1 : dbits(r); h = h * 31 + (u32)rb + (u32)(rb >> 32);
          r = a - b; rb = disnan(r) ? 2 : dbits(r); h = h * 31 + (u32)rb + (u32)(rb >> 32);
          r = a * b; rb = disnan(r) ? 3 : dbits(r); h = h * 31 + (u32)rb + (u32)(rb >> 32);
          r = a / b; rb = disnan(r) ? 4 : dbits(r); h = h * 31 + (u32)rb + (u32)(rb >> 32);
          h = h * 31 + (u32)((a == b) | ((a != b) << 1) | ((a < b) << 2) |
                             ((a <= b) << 3) | ((a > b) << 4) | ((a >= b) << 5));
        }
    }
  return h;
}

/* A NaN really does come out of the operations that should make one, and
   compares false against everything including itself. */
static u32 nan_behaviour(void) {
  u32 h = 0;
  float zf = mkf(0), inff = mkf(0x7F800000u);
  double zd = mkd(0), infd = mkd(0x7FF0000000000000ull);
  float fn = zf / zf;
  double dn = zd / zd;
  h = h * 31 + (u32)fisnan(fn);
  h = h * 31 + (u32)disnan(dn);
  h = h * 31 + (u32)fisnan(inff - inff);
  h = h * 31 + (u32)disnan(infd - infd);
  h = h * 31 + (u32)fisnan(inff * zf);
  h = h * 31 + (u32)disnan(infd * zd);
  h = h * 31 + (u32)((fn == fn) | ((fn != fn) << 1) | ((fn < fn) << 2) |
                     ((fn >= fn) << 3));
  h = h * 31 + (u32)((dn == dn) | ((dn != dn) << 1) | ((dn < dn) << 2) |
                     ((dn >= dn) << 3));
  /* And the two infinities are ordered the way they should be. */
  h = h * 31 + (u32)((-inff < inff) | ((inff > 1.0f) << 1) |
                     ((-infd < infd) << 2) | ((infd > 1.0) << 3));
  return h;
}

/* Between the two widths, and between each width and every integer type.  Only
   values each destination can hold are converted; anything else is undefined
   and would be comparing two guesses. */
static u32 conversions(void) {
  u32 h = 0;
  for (int i = 0; i < NFPAT; i++)
    for (int s = 0; s < 2; s++) {
      float f = mkf(fpat[i] ^ (s ? 0x80000000u : 0));
      double d = (double)f;                 /* always exact */
      h = h * 31 + (u32)dbits(d) + (u32)(dbits(d) >> 32);
      h = h * 31 + fbits((float)d);         /* and back */
    }
  for (int i = 0; i < NDPAT; i++)
    for (int s = 0; s < 2; s++) {
      double d = mkd(dpat[i] ^ (s ? 0x8000000000000000ull : 0));
      float f = (float)d;                   /* rounds, and may go to infinity */
      h = h * 31 + (fisnan(f) ? 7 : fbits(f));
    }

  /* Integers to floating point and back, over values that survive the round
     trip and some that do not. */
  {
    static const s32 iv[] = {0, 1, -1, 2, -2, 100, -100, 32767, -32768,
                             65535, -65536, 1000000, -1000000,
                             (s32)0x7FFFFFFF, (s32)0x80000000, 16777216,
                             16777217};
    for (int i = 0; i < (int)(sizeof iv / sizeof iv[0]); i++) {
      s32 v = iv[i];
      u32 uv = (u32)v;
      h = h * 31 + fbits((float)v);
      h = h * 31 + fbits((float)uv);
      h = h * 31 + (u32)dbits((double)v) + (u32)(dbits((double)v) >> 32);
      h = h * 31 + (u32)dbits((double)uv) + (u32)(dbits((double)uv) >> 32);
      /* A double holds every s32 exactly, so this way round always returns. */
      h = h * 31 + (u32)(s32)(double)v;
      h = h * 31 + (u32)(u32)(double)uv;
      /* And the narrow types, from a value each of them can hold. */
      h = h * 31 + (u32)(s16)(double)(s16)v;
      h = h * 31 + (u32)(u16)(double)(u16)v;
    }
  }

  /* 64 bit integers, which are four registers on this machine. */
  {
    static const u64 lv[] = {0, 1, 0xFFFFFFFFull, 0x100000000ull,
                             0x123456789ABCull, 0x7FFFFFFFFFFFFFFFull,
                             0x8000000000000000ull, 1000000000000ull};
    for (int i = 0; i < (int)(sizeof lv / sizeof lv[0]); i++) {
      u64 u = lv[i];
      s64 v = (s64)u;
      h = h * 31 + fbits((float)u);
      h = h * 31 + fbits((float)v);
      u64 db = dbits((double)u); h = h * 31 + (u32)db + (u32)(db >> 32);
      db = dbits((double)v);     h = h * 31 + (u32)db + (u32)(db >> 32);
      /* Back again from a value that fits, which every one of these does after
         the round trip through double loses its low bits. */
      double back = (double)v;
      if (back >= -9.2e18 && back <= 9.2e18)
        h = h * 31 + (u32)(u64)(s64)back + (u32)((u64)(s64)back >> 32);
    }
  }
  return h;
}

/* Floating point through the places a 32 or 64 bit value has to travel: past
   the registers arguments arrive in, through a pointer, into a struct, and
   back out of a function as a return value. */
struct fpair { float a; double b; };
static struct fpair make_fpair(float a, double b) {
  struct fpair p; p.a = a; p.b = b; return p;
}
static double eight(double a, float b, double c, float d,
                    double e, float f, double g, float h) {
  return ((a * 2.0 + (double)b) * 3.0 + c) * 5.0 + (double)d +
         ((e * 7.0 + (double)f) * 11.0 + g) * 13.0 + (double)h;
}
static void by_pointer(float *f, double *d) { *f = *f * 2.0f + 1.0f; *d = *d / 4.0 - 0.25; }

static u32 passing(void) {
  u32 h = 0;
  for (int i = 0; i < NFPAT; i++) {
    float f = mkf(fpat[i]);
    double d = mkd(dpat[i]);
    struct fpair p = make_fpair(f, d);
    h = h * 31 + (fisnan(p.a) ? 1 : fbits(p.a));
    u64 b = dbits(p.b); h = h * 31 + (u32)b + (u32)(b >> 32);

    double r = eight(d, f, d * 2.0, f * 2.0f, d + 1.0, f + 1.0f, -d, -f);
    b = disnan(r) ? 9 : dbits(r); h = h * 31 + (u32)b + (u32)(b >> 32);

    by_pointer(&f, &d);
    h = h * 31 + (fisnan(f) ? 2 : fbits(f));
    b = disnan(d) ? 3 : dbits(d); h = h * 31 + (u32)b + (u32)(b >> 32);
  }
  return h;
}

int main(void) {
  puthex(float_arithmetic(), 8);
  puthex(double_arithmetic(), 8);
  puthex(nan_behaviour(), 8);
  puthex(conversions(), 8);
  puthex(passing(), 8);
  return 0;
}
