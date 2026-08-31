/* c166-flags: -mcpu=xc16x */
/* Compiled twice: once for c166 and run in the simulator, once for the host
   and run natively.  The two outputs must match exactly.

   Reaching the multiply-accumulate coprocessor from C.  Nothing selects its
   instructions, so an asm statement is the only way to use it - and until the
   unit's registers were names the compiler knew, the only way to put a pointer
   in IDX0 was to write the move by hand inside the asm string, as
   macrepeat.c still does.  That works and says nothing: the compiler does not
   know the register was written, so it cannot know it was read back either.

   Here the pointer is pinned to IDX0 or IDX1 and the accumulator's two words
   are read out of MAL and MAH, all by name.  The moves around the asm
   statement are the compiler's, which is the whole point: what is checked is
   that the absolute addressed MOV it emits for a register that is really a
   memory location reaches the same register the instruction inside the asm
   statement is using.

   The array behind IDX is __dpram, which is what puts it where the unit can
   address it at all; see below.

   The pointers are read-write because the instruction steps them, so the value
   the compiler reads back out afterwards is one past the last element
   - which the host side computes too, since a wrong step count would otherwise
   only show up as a wrong sum. */
#ifdef __c166__
static void put(char c) { *(__far volatile unsigned char *)0xFF0000 = c; }
#else
#include <stdio.h>
static void put(char c) { putchar(c); }
#endif

typedef __UINT32_TYPE__ u32;
typedef __INT16_TYPE__ s16;
typedef __UINT16_TYPE__ u16;

static void puthex(u32 v, int d) {
  for (int i = d - 1; i >= 0; i--) put("0123456789abcdef"[(v >> (i * 4)) & 0xF]);
  put('\n');
}

/* The array an IDX pointer walks has to be in the dual-port RAM, which is the
   only memory IDX0 and IDX1 reach - PM0036 section 2.1.  The other operand is
   reached through a general purpose register, which gets to the whole memory
   space, so only one of these two needs placing.  That asymmetry is the point
   of putting both in one program: a run where the wrong one is marked stops
   the simulator rather than quietly reading somewhere else. */
#ifdef __c166__
#define DPRAM __dpram
#else
#define DPRAM
#endif

#define N 6
static DPRAM s16 a[N] = {1, -2, 3, -4, 5, -6};
static s16 b[N] = {100, 200, -300, 400, -500, 600};

int main(void) {
  u32 sum;
  unsigned advanced;

#ifdef __c166__
  {
    /* The first pointer is one of the coprocessor's own and the second an
       ordinary register, which is the only shape the two-pointer forms have:
       the instruction encodes which IDX it uses and takes a general purpose
       register for the other side.  That register has to be R0 to R3, which
       is what "q" is for.

       The accumulator is cleared with CoLOAD from a register holding zero
       rather than "coload r0, r0", which would load the ABI stack pointer.

       The plain form and not CoMACM, which also writes the word it read one
       step back down the buffer - a delay line, which macrepeat.c covers and
       which would leave the arrays here different for the sequences after
       this one. */
    register const s16 *p __asm__("idx0") = a;
    register u16 lo __asm__("mal");
    register u16 hi __asm__("mah");
    const s16 *q = b;
    u16 zero = 0;
    __asm__ volatile("coload %4, %4\n\t"
                     "repeat 6 times comac [idx0+], [%1+]"
                     : "+r"(p), "+q"(q), "=r"(lo), "=r"(hi)
                     : "r"(zero)
                     : "msw", "memory");
    sum = ((u32)hi << 16) | lo;
    advanced = (unsigned)(p - a);
  }
#else
  {
    long long acc = 0;
    for (int i = 0; i < N; i++) acc += (long long)a[i] * b[i];
    sum = (u32)acc;
    advanced = N;
  }
#endif
  puthex(sum, 8);
  puthex(advanced, 4);

  /* The other pointer.  Which one an instruction uses is in the encoding, so
     naming the register is the only way to say it - there is nothing here for
     a register class constraint to choose between. */
#ifdef __c166__
  {
    register const s16 *p __asm__("idx1") = a;
    register u16 lo __asm__("mal");
    register u16 hi __asm__("mah");
    const s16 *q = b;
    u16 zero = 0;
    __asm__ volatile("coload %4, %4\n\t"
                     "repeat 6 times comac [idx1+], [%1+]"
                     : "+r"(p), "+q"(q), "=r"(lo), "=r"(hi)
                     : "r"(zero)
                     : "msw", "memory");
    sum = ((u32)hi << 16) | lo;
    advanced = (unsigned)(q - b);
  }
#else
  {
    long long acc = 0;
    for (int i = 0; i < N; i++) acc += (long long)a[i] * b[i];
    sum = (u32)acc;
    advanced = N;
  }
#endif
  puthex(sum, 8);
  puthex(advanced, 4);

  /* An offset register set by name and used as the step.  QX0 is in the
     extended space at F000H rather than in the ordinary one, so the move that
     writes it comes from a different base - which is the other half of what
     is being checked here. */
#ifdef __c166__
  {
    register const s16 *p __asm__("idx0") = a;
    register u16 step __asm__("qx0") = 2;
    register u16 lo __asm__("mal");
    register u16 hi __asm__("mah");
    const s16 *q = b;
    u16 zero = 0;
    __asm__ volatile("coload %5, %5\n\t"
                     "repeat 6 times comac [idx0+qx0], [%1+]"
                     : "+r"(p), "+q"(q), "=r"(lo), "=r"(hi), "+r"(step)
                     : "r"(zero)
                     : "msw", "memory");
    sum = ((u32)hi << 16) | lo;
    advanced = (unsigned)(p - a);
  }
#else
  {
    long long acc = 0;
    for (int i = 0; i < N; i++) acc += (long long)a[i] * b[i];
    sum = (u32)acc;
    advanced = N;
  }
#endif
  puthex(sum, 8);
  puthex(advanced, 4);
  return 0;
}
