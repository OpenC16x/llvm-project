/* c166-flags: -mcpu=xc16x */
/* c166-sim-flags: --interrupt-every=2003:8:1 */
/* Compiled twice: once for c166 and run in the simulator, once for the host
   and run natively.  The two outputs must match exactly.

   The coprocessor's four offset registers are the strides its pointers walk
   by, and they are in the extended register space - which has no "push mem",
   and no way to name one through a "reg" field without an EXTR in front of it.
   So until there was one they could not be saved, and an interrupt handler
   that walked a stream by a stride quietly took the interrupted code's stride
   with it.

   That is what this checks, and it is checked the way it would go wrong rather
   than by reading the assembly: the program puts two values in QX0 and QR0 by
   hand, spends a while being interrupted by a handler that runs a strided dot
   product - which is what writes them - and then reads them back.  Without the
   save they come back holding the handler's strides.

   The host has no coprocessor and no interrupts, so its side is the two
   constants and the same arithmetic.  What is being compared is that the C166
   side still has them.

   That this discriminates was checked rather than assumed: with the four Q
   registers taken out of the save list the program fails at -O1, -O2 and -Os,
   printing 0008 and 0006 - the handler's own strides - instead of the two
   constants.  It passes at -O0 either way, because C166MACRepeat does not run
   there, so the handler is a loop of ordinary multiplies and writes no offset
   register at all.

   The period is longer than the handler at -O0, where the handler is slowest.
   A period shorter than that would have the handler re-entered on every RETI
   and the program would spend all its time in it, which is what the part does
   too. */
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

/* The strides the program keeps, which nothing but this file writes.  Neither
   is one of the handler's - it walks by eight bytes and by six - so either one
   coming back wrong is the save having gone missing. */
#define MY_QX0 0x1234
#define MY_QR0 0x5678

#ifdef __c166__

/* The two streams the handler walks.  One is in the dual-port RAM because an
   IDX pointer reaches nothing else; see llvm/lib/Target/C166/README.txt.
   They are filled from a volatile word so that the compiler cannot fold the
   handler's arithmetic away, which is what it does to a loop over two arrays
   it can see are zero. */
__attribute__((c166_dpram)) static short coeff[32];
static short sample[32];
static volatile u16 seed = 3;
static volatile u32 sink;

/* Eight bytes and six, so both offset registers are written: QX0 belongs to
   the IDX pointer and QR0 to the general purpose one.  This is the shape
   C166MACRepeat turns into one repeated CoMAC. */
__attribute__((interrupt(8))) void handler(void) {
  /* "long" and not "int", which is what makes this a repeated CoMAC rather
     than a run of ordinary multiplies: int is sixteen bits on this part, so an
     int accumulator is a sixteen bit accumulation and the unit accumulates in
     forty.  The optimiser narrows the arithmetic to match what the source
     asked for, and what is left does not look like a dot product any more. */
  long a = 0;
  for (u16 i = 0; i < 16; i++)
    a += (long)coeff[i * 4] * (long)sample[i * 3];
  sink = (u32)a;
}

static void fillStreams(void) {
  u16 v = seed;
  for (u16 i = 0; i < 32; i++) {
    coeff[i] = (short)(v + i);
    sample[i] = (short)(v * 3 + i);
  }
}

static void setOffsets(u16 x, u16 r) {
  __asm__ volatile("mov qx0, %0" : : "r"(x));
  __asm__ volatile("mov qr0, %0" : : "r"(r));
}

static void getOffsets(u16 *x, u16 *r) {
  __asm__ volatile("mov %0, qx0" : "=r"(*x));
  __asm__ volatile("mov %0, qr0" : "=r"(*r));
}

/* Nothing enables interrupts for a program; crt0 leaves PSW.IEN clear and a
   program that wants a handler to run says so, which is what this is. */
static void enableInterrupts(void) { __asm__ volatile("bset psw.11"); }

#else

/* The host has no coprocessor, no offset registers and no interrupts.  Its
   side of the comparison is that the two strides come back as they were set,
   which is exactly what the C166 side is being asked to show. */
static void fillStreams(void) {}
static void setOffsets(u16 x, u16 r) { (void)x; (void)r; }
static void getOffsets(u16 *x, u16 *r) { *x = MY_QX0; *r = MY_QR0; }
static void enableInterrupts(void) {}

#endif

/* Something to be doing while the handler fires, and something whose answer
   depends on nothing the handler touches. */
static u32 work(void) {
  u32 s = 1;
  for (u16 i = 0; i < 500; i++)
    s = s * 1103515245u + i;
  return s;
}

int main(void) {
  u16 x = 0, r = 0;
  fillStreams();
  setOffsets(MY_QX0, MY_QR0);
  enableInterrupts();
  u32 w = work();
  getOffsets(&x, &r);
  puthex(x, 4);
  puthex(r, 4);
  puthex(w, 8);
  return 0;
}
