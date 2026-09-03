/* c166-flags: -mcpu=xc16x */
/* Compiled twice: once for c166 and run in the simulator, once for the host
   and run natively.  The two outputs must match exactly.

   Dot products written as ordinary C loops over a __dpram array, which is the
   shape C166MACRepeat turns into one repeated coprocessor instruction.  What
   is under test is that the instruction computes what the loop said: the
   answer comes back through the simulator's model of the repeat prefix, and
   the host's answer is the loop run as written.

   Every case below is here because it is a different path through the pass or
   through the instruction: which stream is the one in the dual-port RAM, the
   sign of the product, adding against taking away, a count the repeat field
   holds against one that has to go through MRW, an accumulator that does not
   start at zero, a stream that starts partway into its array, and the pointer
   update codes - a word forward, a word back, and a stride through one of the
   unit's offset registers, in either direction and on either pointer.

   The negative cases matter as much.  A loop over two ordinary arrays cannot
   use IDX0 at all, one whose trip count is not a constant has nothing to put
   in the repeat field, and one that also stores has more to do than the prefix
   can repeat; all three have to come out with the same answer as the host by
   the ordinary route.  The accumulator is 32 bit and wraps, on both sides, so
   a sum that overflows is a defined answer and one of these gets there. */
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
  for (int i = d - 1; i >= 0; i--)
    put("0123456789abcdef"[(v >> (i * 4)) & 0xF]);
  put('\n');
}

#ifdef __c166__
#define DPRAM __dpram
#else
#define DPRAM
#endif

/* Long enough for a count the five bit repeat field cannot hold, which is what
   makes the run go through MRW instead. */
#define N 40
static DPRAM s16 xs[N];
static s16 ys[N];
static DPRAM u16 uxs[N];
static u16 uys[N];
static s16 plain_a[N], plain_b[N];

/* A volatile the compiler cannot fold, for the loops that must not be turned
   into one instruction. */
static volatile u16 opaque = N;

static void fill(void) {
  for (int i = 0; i < N; i++) {
    xs[i] = (s16)(i * 37 - 300);
    ys[i] = (s16)(500 - i * 91);
    uxs[i] = (u16)(i * 1237 + 7);
    uys[i] = (u16)(60000 - i * 913);
    plain_a[i] = xs[i];
    plain_b[i] = ys[i];
  }
}

int main(void) {
  fill();

  /* The plain signed dot product, at a count the repeat field holds. */
  {
    s32 acc = 0;
    for (int i = 0; i < 8; i++)
      acc += (s32)xs[i] * ys[i];
    puthex((u32)acc, 8);
  }

  /* The same the other way round, so the stream in the dual-port RAM is the
     second operand of the multiply rather than the first.  Both kinds here are
     symmetric, so the instruction takes them in either order. */
  {
    s32 acc = 0;
    for (int i = 0; i < 8; i++)
      acc += (s32)ys[i] * xs[i];
    puthex((u32)acc, 8);
  }

  /* A count the field cannot hold, which has to go through MRW. */
  {
    s32 acc = 0;
    for (int i = 0; i < N; i++)
      acc += (s32)xs[i] * ys[i];
    puthex((u32)acc, 8);
  }

  /* Unsigned, where the product is zero extended and the sum wraps. */
  {
    u32 acc = 0;
    for (int i = 0; i < N; i++)
      acc += (u32)uxs[i] * uys[i];
    puthex(acc, 8);
  }

  /* Taking the products away rather than adding them. */
  {
    s32 acc = 0;
    for (int i = 0; i < 24; i++)
      acc -= (s32)xs[i] * ys[i];
    puthex((u32)acc, 8);
  }

  /* An accumulator that does not start at zero, which is what CoLOAD is for. */
  {
    s32 acc = (s32)0x12345678;
    for (int i = 0; i < 12; i++)
      acc += (s32)xs[i] * ys[i];
    puthex((u32)acc, 8);
  }

  /* Starting partway into both arrays, so the two pointers the instruction is
     given are not the arrays themselves. */
  {
    s32 acc = 0;
    for (int i = 5; i < 21; i++)
      acc += (s32)xs[i] * ys[i];
    puthex((u32)acc, 8);
  }

  /* Walking one array backwards, which is update code 3 - the pointer a word
     the other way - and is what a convolution rather than a correlation looks
     like. */
  {
    s32 acc = 0;
    for (int i = 0; i < 16; i++)
      acc += (s32)xs[15 - i] * ys[i];
    puthex((u32)acc, 8);
  }

  /* A stride, which goes through one of the unit's offset registers: every
     third sample of the ordinary array, which is a decimating filter.  QR0
     belongs to the general purpose pointer. */
  {
    s32 acc = 0;
    for (int i = 0; i < 12; i++)
      acc += (s32)xs[i] * ys[i * 3];
    puthex((u32)acc, 8);
  }

  /* The other pointer strided, so it is QX0 that is written; the dual-port RAM
     array is the one being decimated this time. */
  {
    s32 acc = 0;
    for (int i = 0; i < 12; i++)
      acc += (s32)xs[i * 3] * ys[i];
    puthex((u32)acc, 8);
  }

  /* Both at once, and neither by a word, so both offset registers are in
     play. */
  {
    s32 acc = 0;
    for (int i = 0; i < 9; i++)
      acc += (s32)xs[i * 4] * ys[i * 3];
    puthex((u32)acc, 8);
  }

  /* A stride backwards, which is update code 5 - the offset register taken
     away rather than added. */
  {
    s32 acc = 0;
    for (int i = 0; i < 12; i++)
      acc += (s32)xs[i] * ys[33 - i * 3];
    puthex((u32)acc, 8);
  }

  /* Unsigned and strided, which is a different multiply with the same
     addressing. */
  {
    u32 acc = 0;
    for (int i = 0; i < 12; i++)
      acc += (u32)uxs[i] * uys[i * 3];
    puthex(acc, 8);
  }

  /* Neither array in the dual-port RAM, so there is nothing for IDX0 to hold
     and the loop stays a loop. */
  {
    s32 acc = 0;
    for (int i = 0; i < 16; i++)
      acc += (s32)plain_a[i] * plain_b[i];
    puthex((u32)acc, 8);
  }

  /* A trip count only the running program knows. */
  {
    s32 acc = 0;
    for (u16 i = 0; i < opaque; i++)
      acc += (s32)xs[i] * ys[i];
    puthex((u32)acc, 8);
  }

  /* A loop with something else in it: the prefix repeats one instruction, so a
     loop that also writes somewhere is not one of these. */
  {
    s32 acc = 0;
    for (int i = 0; i < 16; i++) {
      acc += (s32)xs[i] * ys[i];
      plain_a[i] = (s16)acc;
    }
    puthex((u32)acc, 8);
    u32 h = 0;
    for (int i = 0; i < N; i++)
      h = h * 31u + (u16)plain_a[i];
    puthex(h, 8);
  }

  /* Two products in one loop, which is two instructions' worth of work and one
     accumulator too many for the shape. */
  {
    s32 acc = 0;
    for (int i = 0; i < 16; i++)
      acc += (s32)xs[i] * ys[i] + (s32)uxs[i] * uys[i];
    puthex((u32)acc, 8);
  }

  /* A sum that overflows thirty two bits, which wraps rather than saturating -
     the accumulator is forty bits wide and only the low thirty two come out,
     and 2^32 divides 2^40, so it is the same answer either way. */
  {
    s32 acc = 0;
    for (int i = 0; i < N; i++)
      acc += (s32)(s16)0x7000 * (s16)0x7000;
    puthex((u32)acc, 8);
  }

  return 0;
}
