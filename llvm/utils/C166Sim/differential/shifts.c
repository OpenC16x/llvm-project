/* Compiled twice: once for c166 and run in the simulator, once for the host
   and run natively.  The two outputs must match exactly.

   Shifts of a value two words wide by an amount only known at run time, at
   every amount there is.  This is where a 16 bit machine has a specific trap:
   the shift instructions read their count register as its low four bits, so a
   shift by sixteen is a shift by nothing.  An expansion that brings the
   crossing bits over with "16 - amount" is therefore right for every amount
   except zero, where instead of contributing nothing it contributes the whole
   word - and zero is the amount a loop is most likely to start at.

   So the amounts are walked one at a time rather than sampled, and the value
   is one with bits in both halves and in the top bit, so that a sign fill and
   a zero fill cannot be confused.  The amount comes through a volatile so that
   the compiler cannot turn any of these back into a constant shift. */
#ifdef __c166__
static void put(char c) { *(__far volatile unsigned char *)0xFF0000 = c; }
#else
#include <stdio.h>
static void put(char c) { putchar(c); }
#endif

typedef __UINT16_TYPE__ u16;
typedef __UINT32_TYPE__ u32;
typedef __INT32_TYPE__ s32;

static void puthex(u32 v, int d) {
  for (int i = d - 1; i >= 0; i--) put("0123456789abcdef"[(v >> (i*4)) & 0xF]);
  put('\n');
}

static volatile int amount;

static void walk(u32 v) {
  for (int n = 0; n < 32; n++) {
    amount = n;
    puthex((u32)(v << amount), 8);
    puthex((u32)(v >> amount), 8);
    puthex((u32)((s32)v >> amount), 8);
  }
}

int main(void) {
  walk(0x00000001u);
  walk(0x80000000u);
  walk(0x12345678u);
  walk(0xFEDCBA98u);
  walk(0xFFFFFFFFu);
  walk(0x00010000u);
  walk(0x0000FFFFu);
  return 0;
}
