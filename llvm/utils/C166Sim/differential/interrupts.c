/* Compiled twice: once for c166 and run in the simulator, once for the host
   and run natively.  The two outputs must match exactly.

   What this one is for is the interrupt attribute.  The host has no
   interrupts, so the reference is simply the computation; the C166 side runs
   the same computation with a handler firing into it about a thousand times.
   The two agreeing is the claim the attribute makes - that a handler saves
   and restores everything it touches - being checked rather than read off the
   assembly.

   The handler is reached the way the documentation says to reach one: a jump
   in the vector table, written by hand.  .vectors follows the four bytes of
   the reset vector, so what is placed there is vector 1.  */
/* c166-sim-flags: --interrupt-every=1009:1:8 */

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

#ifdef __c166__
/* Volatile so the handler has to load and store rather than keep anything,
   and so nothing here is optimised away for being unread.  */
static volatile u32 Ticks;
static volatile u32 A = 1, B = 2, C = 3, D = 4;

__attribute__((interrupt)) void handler(void) {
  /* Enough arithmetic to need most of a register bank, so that a register the
     interrupted code was using is very likely one of them.  All of it cheap:
     a handler that took longer than the period between requests would be
     re-entered as soon as it returned and the program would never finish,
     which is what the part does too.  */
  u32 a = A, b = B, c = C, d = D;
  a += b ^ 0x5A5A5A5Au;
  b -= c + 0x1234u;
  c ^= d << 3;
  d += a >> 5;
  A = a; B = b; C = c; D = d;
  Ticks = Ticks + 1;
}

asm(".section .vectors,\"ax\",@progbits\n\t"
    "jmps #seg(handler), sof(handler)\n\t"
    ".text");
#endif

int main(void) {
#ifdef __c166__
  /* Nothing enables interrupts for a program; crt0 leaves PSW.IEN clear and
     the part comes up that way.  */
  __asm__ volatile("bset psw.11");
#endif

  /* A computation whose every step depends on the last, ending in a
     conditional branch each time round: the interrupt lands between an
     instruction that sets the flags and the one that reads them about as
     often as anywhere else, and only the PSW that entry pushes and RETI pops
     makes that survivable.  */
  u32 s = 1;
  u16 t = 0xACE1u;
  for (u32 i = 1; i <= 400; ++i) {
    s = s * 2654435761u + i;
    s ^= s >> 13;
    if (s & 1u)
      s += i * 3u;
    else
      s -= i;
    t = (u16)((t >> 1) ^ (u16)(-(t & 1u) & 0xB400u));
  }
  puthex(s, 8);
  puthex(t, 4);

#ifdef __c166__
  /* The host has no handler, so the number of times this one ran cannot be
     part of the compared output.  That it ran at all can be: a zero here
     prints a line the host never prints, and the run fails.  */
  if (Ticks == 0)
    put('!'), put('\n');
#endif
  return 0;
}
