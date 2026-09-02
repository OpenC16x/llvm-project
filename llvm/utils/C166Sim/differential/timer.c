/* Compiled twice: once for c166 and run in the simulator, once for the host
   and run natively.  The two outputs must match exactly.

   A periodic interrupt from the part's own timer, which is the whole chain
   end to end and the one thing interrupts.c cannot do: there the request is
   injected by the simulator's command line, so the peripheral half - the
   control registers a program writes, the request flag it raises, the enable
   and the priority beside it - is not in the picture at all.  Here nothing is
   injected.  The program configures GPT1 exactly as it would on the part, and
   what arrives is what the timer sent.

   The host has no timer, so the reference is the computation on its own.  The
   C166 side runs the same computation while the handler fires into it, and
   waits for a fixed number of ticks before printing - so a chain that is
   broken anywhere does not print the wrong answer, it never prints at all,
   and the run times out.  That is the right failure for this: the interesting
   way to get it wrong is for the interrupt never to arrive.

   The registers are the C167CR Derivatives User's Manual V3.1 chapter 10's,
   and every one of them is in the set the three special function register
   maps in this tree agree about, so this is the same program on every part.  */

#ifdef __c166__
static void put(char c) { *(__far volatile unsigned char *)0xFF0000 = c; }
#else
#include <stdio.h>
static void put(char c) { putchar(c); }
#endif

typedef __UINT16_TYPE__ u16;
typedef __UINT32_TYPE__ u32;

static void puthex(u32 v, int digits) {
  for (int i = digits - 1; i >= 0; i--)
    put("0123456789abcdef"[(v >> (i * 4)) & 0xF]);
  put('\n');
}

/* How many ticks to wait for.  Enough that the handler has landed in every
   part of the loop below, few enough that the run is short.  */
#define TICKS 40

#ifdef __c166__

#define SFR(a) (*(volatile u16 *)(a))
#define T2 SFR(0xFE40)
#define T3 SFR(0xFE42)
#define T2CON SFR(0xFF40)
#define T3CON SFR(0xFF42)
#define T3IC SFR(0xFF62)

/* Volatile so the handler has to load and store rather than keep anything,
   and so the wait below re-reads it each time round.  */
static volatile u32 Ticks;

/* Trap 23H, which is GPT1 timer 3.  Naming the number is what makes the
   compiler write the vector table slot; the linker script lays it out.  */
__attribute__((interrupt(35))) void tick(void) { Ticks = Ticks + 1; }

static void startTimer(void) {
  /* T2 holds the reload value and watches T3's output toggle latch: mode 100B
     with T2I = 111B, any transition.  In reload mode the auxiliary timer stops
     itself, so its own run bit stays clear.  */
  T2 = 0xFFC0;
  T2CON = 0x0027;
  T3 = 0xFFC0;
  /* The source's own enable and priority, which is what a peripheral has and
     an injected request does not: xxIE at bit 6, ILVL 8 in the low nibble.  */
  T3IC = 0x0048;
  __asm__ volatile("bset psw.11" ::: "memory");
  /* Timer mode, running, prescaler 3: a count every 8 x 2^3 = 64 states, so
     64 counts to the reload is 4096 states a tick.  */
  T3CON = 0x0043;
}

static void waitForTicks(u32 N) {
  while (Ticks < N)
    ;
}

#else
static void startTimer(void) {}
static void waitForTicks(u32 N) { (void)N; }
#endif

/* The computation, which has to come out the same whether or not anything
   interrupted it.  It is the same shape as the one in interrupts.c and for the
   same reason: enough live values that a handler which failed to put a
   register back would be noticed.  */
static u32 mix(u32 n) {
  u32 a = 1, b = 2, c = 3, d = 4;
  for (u32 i = 0; i < n; i++) {
    a += b ^ 0x5A5A5A5Au;
    b -= c + 0x1234u;
    c ^= d << 3;
    d += a >> 5;
  }
  return a ^ b ^ c ^ d;
}

int main(void) {
  startTimer();

  /* Long enough that the ticks land inside it rather than after it. */
  puthex(mix(4000), 8);

  waitForTicks(TICKS);
  /* Not the tick count itself - that would be the simulator's arithmetic
     rather than the program's - but that the wait ended, which is what says
     the interrupts arrived.  */
  puthex(TICKS, 8);

  /* And again afterwards, with the timer still running, so the second answer
     is computed with the handler firing into it throughout.  */
  puthex(mix(4000), 8);
  return 0;
}
