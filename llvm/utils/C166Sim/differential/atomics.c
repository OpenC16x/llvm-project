/* Compiled twice: once for c166 and run in the simulator, once for the host
   and run natively.  The two outputs must match exactly.

   The atomic operations.  Nothing here is concurrent - one core, one thread,
   no interrupts running - so what this checks is that the sequences compute
   what they are supposed to compute.  Whether they are indivisible is a
   separate question, and one the simulator answers a different way: it stops
   with an error if an ATOMIC sequence ever reaches an instruction that changes
   the flow or extends again, which is what the count could not survive.

   So this file is the arithmetic half and the simulator's own check is the
   structural half, and every program here runs under both.

   Widths are spelled exactly, for the reason the other programs give: an int
   is sixteen bits on this machine and thirty two on the host. */
#ifdef __c166__
static void put(char c) { *(__far volatile unsigned char *)0xFF0000 = c; }
#else
#include <stdio.h>
static void put(char c) { putchar(c); }
#endif

#include <stdatomic.h>

typedef __UINT8_TYPE__ u8;
typedef __UINT16_TYPE__ u16;
typedef __UINT32_TYPE__ u32;

static void puthex(u32 v, int d) {
  for (int i = d - 1; i >= 0; i--) put("0123456789abcdef"[(v >> (i*4)) & 0xF]);
  put('\n');
}

static _Atomic u16 w;
static _Atomic u8 b;

/* --- the operations that become one ATOMIC sequence each ------------- */

static void straight_line(void) {
  atomic_store(&w, 0x1234);
  puthex(atomic_load(&w), 4);

  puthex(atomic_fetch_add(&w, 0x1111), 4);   /* returns the old value */
  puthex(atomic_load(&w), 4);
  puthex(atomic_fetch_sub(&w, 0x0345), 4);
  puthex(atomic_fetch_and(&w, 0x0FF0), 4);
  puthex(atomic_fetch_or(&w, 0x8001), 4);
  puthex(atomic_fetch_xor(&w, 0xFFFF), 4);
  puthex(atomic_exchange(&w, 0x00AA), 4);
  puthex(atomic_load(&w), 4);

  /* Wrapping, which is where a sequence that returned the new value rather
     than the old one would still look right most of the time. */
  atomic_store(&w, 0xFFFF);
  puthex(atomic_fetch_add(&w, 3), 4);
  puthex(atomic_load(&w), 4);
}

static void bytes(void) {
  atomic_store(&b, 0x5A);
  puthex(atomic_load(&b), 2);
  puthex(atomic_fetch_add(&b, 0xF0), 2);
  puthex(atomic_load(&b), 2);
  puthex(atomic_fetch_and(&b, 0x3C), 2);
  puthex(atomic_exchange(&b, 0x11), 2);
  puthex(atomic_load(&b), 2);
}

/* --- the ones that go round a compare and exchange loop -------------- */

/* NAND is an AND and a complement, which is one instruction more than an
   ATOMIC sequence reaches, so it goes round a compare and exchange loop
   instead.  It is spelled as the builtin because C11 has no
   atomic_fetch_nand; both compilers have this one. */
static u16 plain;

static void through_cmpxchg(void) {
  __atomic_store_n(&plain, 0x00F0, __ATOMIC_SEQ_CST);
  puthex(__atomic_fetch_nand(&plain, 0xFF00, __ATOMIC_SEQ_CST), 4);
  puthex(__atomic_load_n(&plain, __ATOMIC_SEQ_CST), 4);
}

/* --- compare and exchange itself ------------------------------------- */

static void compare_exchange(void) {
  atomic_store(&w, 0x0100);

  u16 expected = 0x0100;
  puthex(atomic_compare_exchange_strong(&w, &expected, 0x0200), 1);
  puthex(expected, 4);
  puthex(atomic_load(&w), 4);

  /* Now one that fails: expected is stale, so the word keeps its value and
     expected is updated to what was actually there. */
  expected = 0x0100;
  puthex(atomic_compare_exchange_strong(&w, &expected, 0x0300), 1);
  puthex(expected, 4);
  puthex(atomic_load(&w), 4);
}

int main(void) {
  straight_line();
  bytes();
  through_cmpxchg();
  compare_exchange();
  return 0;
}
