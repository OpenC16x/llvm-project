/* Compiled twice: once for c166 and run in the simulator, once for the host
   and run natively.  The two outputs must match exactly.

   Bit fields, unions, conversions between every pair of integer widths, and
   the shapes a switch can take.  These are where a 16 bit machine has the most
   room to be quietly wrong: a byte lives in half of a word register, an int is
   sixteen bits rather than thirty two, and a switch becomes either a jump
   table or a chain of compares depending on how its cases are spread.

   Everything whose width matters is spelled with an exact width type, because
   int is not the same size on the two machines.  Plain char is signed on both,
   which is why it is used where the signedness is the point.

   That has a consequence worth knowing before adding anything here: integer
   promotion stops at int, so "a * 3" on a u16 is computed in sixteen bits here
   and in thirty two on the host, and the two disagree as soon as the product
   does not fit.  Anything that can exceed sixteen bits has to say so.  For the
   same reason a comparison that mixes signedness can differ - a u16 against an
   s8 is unsigned here, because a sixteen bit int cannot hold every u16, and
   signed on the host, where it can - so those are written with the width they
   are meant to be compared at. */
#ifdef __c166__
static void put(char c) { *(__far volatile unsigned char *)0xFF0000 = c; }
#else
#include <stdio.h>
static void put(char c) { putchar(c); }
#endif

typedef __UINT8_TYPE__ u8;   typedef __INT8_TYPE__ s8;
typedef __UINT16_TYPE__ u16; typedef __INT16_TYPE__ s16;
typedef __UINT32_TYPE__ u32; typedef __INT32_TYPE__ s32;
typedef __UINT64_TYPE__ u64; typedef __INT64_TYPE__ s64;

static void puthex(u32 v, int d) {
  for (int i = d - 1; i >= 0; i--) put("0123456789abcdef"[(v >> (i*4)) & 0xF]);
  put('\n');
}

/* --- bit fields ------------------------------------------------------ */

/* Widths that do and do not divide a byte, signed and unsigned, and one that
   straddles a word boundary however it is packed. */
struct flags {
  unsigned a : 1;
  unsigned b : 3;
  signed   c : 4;
  unsigned d : 8;
  signed   e : 12;
  unsigned f : 1;
};
struct packedish {
  u16 head;
  unsigned x : 5;
  unsigned y : 11;
  signed   z : 16;
  u8 tail;
};

static u32 bitfields(void) {
  u32 h = 0;
  for (u16 v = 0; v < 0x1000; v += 37) {
    struct flags s;
    s.a = v & 1;
    s.b = (v >> 1) & 7;
    s.c = (signed)((v >> 4) & 15) - 8;
    s.d = (u8)(v >> 3);
    s.e = (s16)((v * 7) & 0xFFF) - 2048;
    s.f = (v >> 11) & 1;
    /* Read every field back, which is the half that can go wrong on its own:
       a signed field has to come out sign extended from its own width. */
    h = h * 31 + s.a;
    h = h * 31 + s.b;
    h = h * 31 + (u32)(s32)s.c;
    h = h * 31 + s.d;
    h = h * 31 + (u32)(s32)s.e;
    h = h * 31 + s.f;
    /* And arithmetic on them, where the promotion happens before the width is
       forgotten. */
    h = h * 31 + (u32)(s32)(s.c + s.e);
    h = h * 31 + (u32)(s.b * s.d);
    s.b += 3; s.c -= 5; s.d ^= 0xAA; s.e += 1000;
    h = h * 31 + s.b + s.d;
    h = h * 31 + (u32)(s32)s.c + (u32)(s32)s.e;
    /* A field written through a pointer, so it is a load, a mask and a store
       rather than anything the compiler can keep in a register. */
    struct flags *p = &s;
    p->d = (u8)(p->d + p->b);
    p->c = (signed)(p->c ^ 3);
    h = h * 31 + p->d + (u32)(s32)p->c;

    struct packedish q;
    q.head = v; q.x = v & 31; q.y = (v >> 2) & 0x7FF;
    q.z = (s16)(v * 3 - 30000); q.tail = (u8)v;
    h = h * 31 + q.head + q.x + q.y + q.tail;
    h = h * 31 + (u32)(s32)q.z;
    q.y = q.y + q.x;
    h = h * 31 + q.y;
  }
  return h;
}

/* --- unions and type punning ----------------------------------------- */

/* Both machines are little endian and none of this has padding in it, so the
   halves of a word land in the same places on each. */
union split32 { u32 w; u16 h[2]; u8 b[4]; };
union split64 { u64 q; u32 w[2]; u16 h[4]; };

static u32 unions(void) {
  u32 h = 0;
  for (u32 v = 1; v; v *= 3) {
    union split32 a; a.w = v;
    h = h * 31 + (u32)a.h[0] + (u32)a.h[1] * 3;
    h = h * 31 + (u32)a.b[0] + (u32)a.b[1] * 3 + (u32)a.b[2] * 5 +
        (u32)a.b[3] * 7;
    a.b[1] ^= 0xFF; a.h[1] += 0x1234;
    h = h * 31 + a.w;

    union split64 b; b.q = (u64)v * 0x100000001ull + 0x1234;
    h = h * 31 + b.w[0] + b.w[1] * 3;
    h = h * 31 + (u32)b.h[0] + (u32)b.h[1] * 3 + (u32)b.h[2] * 5 +
        (u32)b.h[3] * 7;
    b.h[2] = (u16)~b.h[2];
    h = h * 31 + (u32)b.q + (u32)(b.q >> 32);
    if (v > 0x40000000u) break;
  }
  return h;
}

/* --- conversions between every pair of widths ------------------------ */

static u32 conversions(void) {
  u32 h = 0;
  static const u64 vals[] = {
    0, 1, 0x7F, 0x80, 0xFF, 0x100, 0x7FFF, 0x8000, 0xFFFF, 0x10000,
    0x7FFFFFFFull, 0x80000000ull, 0xFFFFFFFFull, 0x100000000ull,
    0x7FFFFFFFFFFFFFFFull, 0x8000000000000000ull, 0xFFFFFFFFFFFFFFFFull,
    0x123456789ABCDEFull, 0xFEDCBA9876543210ull,
  };
  for (int i = 0; i < (int)(sizeof vals / sizeof vals[0]); i++) {
    u64 v = vals[i];
    /* Down through every width, unsigned and signed, and back up again.  The
       widening is where a byte register has to be zero or sign extended into a
       word one, which are different instructions. */
    u8  b  = (u8)v;   s8  sb = (s8)v;
    u16 w  = (u16)v;  s16 sw = (s16)v;
    u32 d  = (u32)v;  s32 sd = (s32)v;
    h = h * 31 + b + (u32)(s32)sb;
    h = h * 31 + w + (u32)(s32)sw;
    h = h * 31 + d + (u32)sd;
    h = h * 31 + (u32)(u16)b + (u32)(u16)(s16)sb;
    h = h * 31 + (u32)(u32)b + (u32)(u32)(s32)sb;
    h = h * 31 + (u32)(u32)w + (u32)(u32)(s32)sw;
    h = h * 31 + (u32)(u64)b + (u32)((u64)b >> 32);
    h = h * 31 + (u32)(u64)(s64)sb + (u32)((u64)(s64)sb >> 32);
    h = h * 31 + (u32)(u64)(s64)sw + (u32)((u64)(s64)sw >> 32);
    h = h * 31 + (u32)(u64)(s64)sd + (u32)((u64)(s64)sd >> 32);
    /* Comparisons that mix signedness, at the width each is meant to be
       compared at.  Written bare, "sb < w" would be unsigned here and signed
       on the host, which is a property of int being sixteen bits rather than
       anything either machine gets wrong. */
    h = h * 31 + (u32)(((s32)sb < (s32)w) | (((s32)b < (s32)sw) << 1) |
                       (((s32)sw < (s32)d) << 2) | (((u32)w < (u32)sd) << 3) |
                       (((s64)sd < (s64)v) << 4) | (((u64)d < (u64)v) << 5));
    /* And the same values compared at their own widths, which is where the
       unsigned form of each branch actually gets used. */
    h = h * 31 + (u32)((sb < sw) | ((b < w) << 1) | ((sw < sd) << 2) |
                       ((w < d) << 3) | ((sd < (s64)v) << 4) | ((d < v) << 5));
  }
  return h;
}

/* --- a byte at a time ------------------------------------------------ */

/* Byte work through pointers, which is where the low and high halves of a
   register are addressed separately. */
static u8 table[192];
static u32 bytewise(void) {
  u32 h = 0;
  for (int i = 0; i < 192; i++) table[i] = (u8)(i * 7 + 3);
  u8 *p = table;
  u8 *q = table + 191;
  for (int i = 0; i < 96; i++) {
    u8 a = *p++, b = *q--;
    h = h * 31 + (u8)(a + b);
    h = h * 31 + (u8)(a - b);
    h = h * 31 + (u8)(a ^ b);
    h = h * 31 + (u8)(a >> (b & 7));
    h = h * 31 + (u8)(a << (b & 7));
    h = h * 31 + (u32)(s32)(s8)(a + b);
  }
  /* Rewriting through a signed view, so the same bytes are read both ways. */
  s8 *sp = (s8 *)table;
  for (int i = 0; i < 192; i++) sp[i] = (s8)(-sp[i] - 1);
  for (int i = 0; i < 192; i++) h = h * 31 + (u32)(s32)sp[i] + table[i];
  return h;
}

/* --- switches of every shape ----------------------------------------- */

/* Dense from zero, which is what a jump table is for. */
static u32 dense(int c) {
  switch (c) {
  case 0: return 0x11; case 1: return 0x22; case 2: return 0x33;
  case 3: return 0x44; case 4: return 0x55; case 5: return 0x66;
  case 6: return 0x77; case 7: return 0x88; case 8: return 0x99;
  case 9: return 0xAA; case 10: return 0xBB; case 11: return 0xCC;
  default: return 0xDEAD;
  }
}
/* Dense but not from zero, and running through negative values. */
static u32 shifted(int c) {
  switch (c) {
  case -5: return 1; case -4: return 2; case -3: return 3; case -2: return 4;
  case -1: return 5; case 0: return 6; case 1: return 7; case 2: return 8;
  default: return 99;
  }
}
/* Spread far enough apart that a table would be mostly holes. */
static u32 sparse(s32 c) {
  switch (c) {
  case -30000: return 1; case 0: return 2; case 17: return 3;
  case 1000: return 4; case 30000: return 5; case 32767: return 6;
  default: return 7;
  }
}
/* Several cases sharing a body, and one that falls into the next. */
static u32 shared(int c) {
  u32 t = 0;
  switch (c) {
  case 1: case 3: case 5: case 7: t += 10; break;
  case 2: case 4: t += 20; /* falls through */
  case 6: t += 30; break;
  case 8: t += 40; break;
  default: t += 50; break;
  }
  return t;
}
/* On a byte, where the selector is narrower than a word. */
static u32 on_char(char c) {
  switch (c) {
  case 'a': return 1; case 'b': return 2; case 'z': return 3;
  case '\0': return 4; case (char)-1: return 5; case (char)127: return 6;
  default: return 7;
  }
}

static u32 switches(void) {
  u32 h = 0;
  for (int i = -8; i < 16; i++) {
    h = h * 31 + dense(i);
    h = h * 31 + shifted(i);
    h = h * 31 + shared(i);
  }
  {
    static const s32 sv[] = {-32768, -30001, -30000, -29999, -1, 0, 1, 16, 17,
                             18, 999, 1000, 1001, 29999, 30000, 32766, 32767};
    for (int i = 0; i < (int)(sizeof sv / sizeof sv[0]); i++)
      h = h * 31 + sparse(sv[i]);
  }
  for (int i = -128; i < 128; i += 7) h = h * 31 + on_char((char)i);
  return h;
}

/* --- control flow ---------------------------------------------------- */

static int side_effects;
static int bump(int v) { side_effects += v; return v; }

static u32 control(void) {
  u32 h = 0;
  side_effects = 0;

  /* Short circuit, where the right hand side must not run when it need not. */
  for (int a = 0; a < 3; a++)
    for (int b = 0; b < 3; b++) {
      int r = (bump(a) && bump(b)) || (bump(-a) && bump(-b));
      h = h * 31 + (u32)r + (u32)side_effects;
    }

  /* A goto out of two loops, which is a branch the structure does not give. */
  for (int i = 0; i < 20; i++) {
    for (int j = 0; j < 20; j++) {
      if (i * j > 60) { h = h * 31 + (u32)(i * 100 + j); goto done; }
      h = h * 7 + (u32)(i ^ j);
    }
  }
done:

  /* break and continue at two depths. */
  for (int i = 0; i < 12; i++) {
    if (i % 3 == 0) continue;
    for (int j = 0; j < 12; j++) {
      if (j == i) break;
      if (j % 2) continue;
      h = h * 11 + (u32)(i * j);
    }
  }

  /* do-while, which runs its body before it tests anything. */
  {
    int n = 0;
    do { h = h * 13 + (u32)n; n += 5; } while (n < 3);
    do { h = h * 13 + (u32)n; n += 5; } while (n < 40);
  }

  /* The comma operator and a chain of conditionals. */
  for (int i = -4; i < 5; i++) {
    int v = (bump(i), i < -2 ? 100 : i < 0 ? 200 : i == 0 ? 300 : i < 3 ? 400 : 500);
    h = h * 17 + (u32)v + (u32)side_effects;
  }
  return h;
}

/* --- arrays, and what a pointer into one can do ---------------------- */

static u16 grid[9][7];
static const u16 lookup[] = {3, 1, 4, 1, 5, 9, 2, 6, 5, 3, 5, 8, 9, 7, 9};

static u32 arrays(void) {
  u32 h = 0;
  for (int i = 0; i < 9; i++)
    for (int j = 0; j < 7; j++)
      grid[i][j] = (u16)(i * 100 + j * 7 + lookup[(i + j) % 15]);
  /* Read it back both ways round, and flat, which is three different address
     computations over the same memory. */
  for (int i = 0; i < 9; i++)
    for (int j = 0; j < 7; j++) h = h * 31 + grid[i][j];
  for (int j = 0; j < 7; j++)
    for (int i = 0; i < 9; i++) h = h * 31 + grid[i][j];
  {
    u16 *flat = &grid[0][0];
    for (int i = 0; i < 63; i++) h = h * 31 + flat[i];
    u16 *end = flat + 63;
    for (u16 *p = end - 1; p >= flat; p--) h = h * 31 + *p;
    /* The difference between two pointers, which is a count and not an
       address, so the two machines agree about it. */
    h = h * 31 + (u32)(end - flat);
  }
  /* A static local, which is initialised once however often this runs. */
  {
    static u16 seen = 0;
    seen += 3;
    h = h * 31 + seen;
  }
  return h;
}

/* --- read-modify-write on a global, and counting leading zeroes ------ */
/* Both are single instructions on this machine and were neither of them
   selected until recently: "g |= x" is a load, an operation and a store back
   in one, and the leftmost set bit is what PRIOR reports.  Neither appeared
   anywhere in this suite, so a change to either would have gone unnoticed by
   everything that runs here. */

/* Volatile, and not static, for two separate reasons.  A file scope static one
   is promoted out of memory entirely and then there is no store to fold
   anything into; and without volatile the compiler forwards each store to the
   next load and folds the whole run into one operation, which is correct and
   leaves nothing here to look at.  Volatile makes each of these the load, the
   operation and the store back that the instruction does in one. */
volatile u16 rmw_acc;
volatile u8 rmw_accb;

static void rmw(u16 x, u8 b) {
  rmw_acc = 0x0F0F;
  rmw_acc += x;
  rmw_acc -= 3;
  rmw_acc |= x;
  rmw_acc &= 0x7FFF;
  rmw_acc ^= 0x5A5A;
  puthex(rmw_acc, 4);

  rmw_accb = 0x33;
  rmw_accb += b;
  rmw_accb |= b;
  rmw_accb &= 0x7F;
  rmw_accb ^= 0x11;
  puthex(rmw_accb, 2);
}

/* An unsigned is sixteen bits here and thirty two on the host, so the count
   has to be taken at the width it is meant to be taken at on each. */
#ifdef __c166__
#define CLZ16(v) ((unsigned)__builtin_clz((unsigned)(v)))
#else
#define CLZ16(v) ((unsigned)__builtin_clz((unsigned)(v)) - 16u)
#endif

static void leading(void) {
  static const u16 vals[] = {1, 2, 3, 0x8000, 0x4000, 0x0100, 0x00FF,
                             0x0001, 0xFFFF, 0x1234};
  for (unsigned i = 0; i < sizeof vals / sizeof vals[0]; i++)
    puthex(CLZ16(vals[i]), 2);
  /* Zero is the one the instruction does not answer, so it is asked for
     separately and the compiler has to put the test back. */
  volatile u16 z = 0;
  puthex(z ? CLZ16(z) : 16u, 2);
}

int main(void) {
  rmw(0x1357, 0x5A);
  leading();
  puthex(bitfields(), 8);
  puthex(unions(), 8);
  puthex(conversions(), 8);
  puthex(bytewise(), 8);
  puthex(switches(), 8);
  puthex(control(), 8);
  puthex(arrays(), 8);
  puthex(arrays(), 8);   /* again, for the static local */
  return 0;
}
