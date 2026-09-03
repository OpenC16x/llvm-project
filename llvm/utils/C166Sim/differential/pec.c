/* Compiled twice: once for c166 and run in the simulator, once for the host
   and run natively.  The two outputs must match exactly.

   The Peripheral Event Controller, which is this part's answer to DMA: a
   channel moves one byte or word per interrupt request instead of entering a
   handler, counts down, and only lets the request through when it runs out.
   The requests come from GPT1 timer 3 in reload mode, so nothing in the
   program touches the timer after it is started - which is the point, since a
   channel that services the request leaves nobody to put the timer back.

   The host has no controller, so the reference is the same movement written as
   a loop.  What the C166 side prints is what the controller left behind, and a
   channel that stepped by the wrong amount, moved the wrong number of times or
   ran past its count changes it: the checksum below covers a guard area after
   the buffer as well, so an over-run shows up as a difference rather than as
   bytes nothing looks at.

   The program waits for the block to finish rather than for a fixed time, so
   a chain that is broken anywhere never prints at all and the run times out.
   That is the right failure here: the interesting way to get this wrong is for
   the transfers never to happen.

   The registers are written out here rather than taken from startup/xc164cm.h
   for the reason timer.c beside this gives: every one of them is in the set
   the three special function register maps in this tree agree about, so this
   is the same program on every part, and including one part's header would
   make it one part's program.  The header has the same values with names on
   them, and clang/test/CodeGen/C166/c166-pec-header.c checks that the two
   agree.

   The source and destination pointers are not registers at all - "these
   pointers do not reside in specific SFRs, but are mapped into the internal
   RAM ... just below the bit-addressable area" - which is why they are written
   as addresses.  */

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

/* How many words the first channel moves, and how much room is left after
   them so that a transfer too many has somewhere to be seen. */
#define WORDS 12
#define GUARD 4

static u16 Buffer[WORDS + GUARD];
static u16 Source = 0xA55A;

/* The byte channel's, which walks a string into one place.  Not const, and
   that is load bearing: a channel moves "between two locations in segment 0",
   and on this part read-only data is in the program memory at C0'0000H, which
   a sixteen bit pointer in segment 0 cannot name.  Const would put this where
   the controller cannot read it, and the pointer would quietly address
   whatever is at the same offset in segment 0 instead.  Anything the PEC
   touches has to be in RAM. */
static u8 Text[] = "PEC transfers";
#define BYTES 8
static u16 Landing;

static u32 checksum(void) {
  u32 h = 0;
  for (unsigned i = 0; i < WORDS + GUARD; i++)
    h = h * 31u + Buffer[i];
  return h;
}

#ifdef __c166__

#define SFR(a) (*(volatile u16 *)(a))
#define T2 SFR(0xFE40)
#define T3 SFR(0xFE42)
#define T2CON SFR(0xFF40)
#define T3CON SFR(0xFF42)
#define T3IC SFR(0xFF62)
#define PECC(ch) SFR(0xFEC0 + 2 * (ch))
#define SRCP(ch) SFR(0xFCE0 + 4 * (ch))
#define DSTP(ch) SFR(0xFCE2 + 4 * (ch))

/* Set by the handler, which runs once per block: the request that finds COUNT
   already at zero is the one that takes the vector.  Volatile because the wait
   below has to re-read it. */
static volatile u16 Done;

/* Trap 23H, GPT1 timer 3.  Naming the number is what makes the compiler write
   the vector table slot. */
__attribute__((interrupt(35))) void finished(void) {
  Done = Done + 1;
  /* Stop the timer, so the next block starts from a known place rather than
     from whatever the requests in between did. */
  T3CON = 0;
}

/* Channel 0, which level 14 group 0 selects. */
#define CHANNEL 0

/* What goes in a PECCx: COUNT in the low byte, BWT at bit 8, INC at 10-9. */
#define PECC_WORD 0x000
#define PECC_BYTE 0x100
#define PECC_INC_DST 0x200
#define PECC_INC_SRC 0x400
#define PECC_COUNT(n) ((n) & 0xFF)

static void startTimer(void) {
  /* Reload mode: T2 holds the reload value and watches T3's output toggle
     latch, so T3 puts itself back and no handler has to.  A count every
     8 x 2^0 = 8 states, sixteen counts to the reload. */
  T2 = 0xFFF0;
  T2CON = 0x0027;
  T3 = 0xFFF0;
  /* Level 14 group 0, which is channel 0, with the source's own enable bit -
     xxIE at bit 6, ILVL in the low nibble, GLVL above it. */
  T3IC = 0x40 | 0x0E | ((CHANNEL & 3) << 4);
  __asm__ volatile("bset psw.11" ::: "memory");
  T3CON = 0x0040;
}

static void runChannel(u16 control, unsigned src, unsigned dst) {
  Done = 0;
  SRCP(CHANNEL) = src;
  DSTP(CHANNEL) = dst;
  PECC(CHANNEL) = control;
  startTimer();
  while (!Done)
    ;
}
#endif

int main(void) {
#ifdef __c166__
  /* A word channel with an incrementing destination: the same word into
     WORDS successive places, and then the handler. */
  runChannel(PECC_COUNT(WORDS) | PECC_WORD | PECC_INC_DST,
             (unsigned)&Source, (unsigned)&Buffer[0]);
#else
  for (unsigned i = 0; i < WORDS; i++)
    Buffer[i] = Source;
#endif
  puthex(checksum(), 8);

#ifdef __c166__
  /* And a byte channel with an incrementing source, which is the shape a
     serial transmitter uses: BYTES bytes out of the string into one word. */
  runChannel(PECC_COUNT(BYTES) | PECC_BYTE | PECC_INC_SRC,
             (unsigned)&Text[0], (unsigned)&Landing);
  puthex(Done, 4);
#else
  Landing = Text[BYTES - 1];
  puthex(1, 4);
#endif
  puthex(Landing, 4);
  return 0;
}
