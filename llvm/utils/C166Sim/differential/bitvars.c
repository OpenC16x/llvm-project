/* c166-flags: -mcpu=xc16x */
/* Compiled twice: once for c166 and run in the simulator, once for the host
   and run natively.  The two outputs must match exactly.

   Bit variables.  __bitaddr puts a variable in the bit-addressable RAM, which
   is the 128 words from FD00H to FDFEH and the only memory a bit instruction
   can name - it takes an 8 bit word number of that space rather than an
   address, so the linker works out which word each variable is and the
   compiler emits BSET, BCLR and the bit test branches against it.

   What is checked here is that the arithmetic is the same arithmetic.  Setting
   bit 3 of a word has an obvious meaning and BSET has to agree with it, and so
   does the bit that a byte at an odd offset in the word turns out to be: the
   compiler narrows a word one byte of which is looked at into a byte access,
   and the high byte of a word is the same word eight bits up.  Getting that
   backwards gives a wrong answer here rather than a wrong instruction that
   still happens to run. */
#ifdef __c166__
static void put(char c) { *(__far volatile unsigned char *)0xFF0000 = c; }
#define BITADDR __bitaddr
#else
#include <stdio.h>
static void put(char c) { putchar(c); }
#define BITADDR
#endif

typedef __UINT16_TYPE__ u16;
typedef __UINT8_TYPE__ u8;

static void puthex(u16 v, int d) {
  for (int i = d - 1; i >= 0; i--) put("0123456789abcdef"[(v >> (i * 4)) & 0xF]);
  put('\n');
}

/* Volatile, which is how a word an interrupt also touches is spelled, and what
   keeps the load in the branch rather than letting it be hoisted. */
static BITADDR volatile u16 flags;
static BITADDR volatile u8 byteflags;
/* Not volatile, to check the read-change-write forms are picked there too. */
static BITADDR u16 plain;
/* Initialised, which is the other section: it has to be copied out of the
   image by the startup code like any other initialised data. */
static BITADDR u16 seeded = 0xA5C3;

static int taken;
/* Not inlined, so that a test of one bit stays a branch: inlined, "if (bit)
   taken++" becomes "taken += bit" and there is no branch left to be a bit
   test branch. */
#ifdef __c166__
__attribute__((noinline))
#endif
static void act(void) { taken++; }

/* One statement per bit, because a bit position has to be a constant for the
   compiler to name it: "flags |= 1u << i" in a loop is a shift and an OR on
   this machine as on any other, and no BSET can come of it.  That is the rule
   worth knowing and the reason this is written out. */
#define SETBIT(n)                                                              \
  do {                                                                         \
    flags = 0;                                                                 \
    flags |= (u16)(1u << (n));                                                 \
    puthex(flags, 4);                                                          \
  } while (0)

#define TESTBIT(n)                                                             \
  do {                                                                         \
    if (flags & (u16)(1u << (n)))                                              \
      act();                                                                   \
  } while (0)

int main(void) {
  /* Set one bit at a time across the whole word, including the high byte. */
  SETBIT(0); SETBIT(1); SETBIT(2); SETBIT(3);
  SETBIT(4); SETBIT(5); SETBIT(6); SETBIT(7);
  SETBIT(8); SETBIT(9); SETBIT(10); SETBIT(11);
  SETBIT(12); SETBIT(13); SETBIT(14); SETBIT(15);

  /* Clearing one bit of a full word, the other half of the same instruction. */
  flags = 0xFFFF;
  flags &= (u16)~(1u << 3);
  puthex(flags, 4);
  flags &= (u16)~(1u << 11);
  puthex(flags, 4);

  /* The bit test branches.  Both bytes of the word are exercised because they
     take different paths through the compiler: a word only one byte of which
     is looked at is narrowed to a byte access first, and the high byte is the
     same word eight bits up. */
  taken = 0;
  flags = 0x0808;
  TESTBIT(3);
  TESTBIT(11);
  TESTBIT(4);
  TESTBIT(12);
  puthex((u16)taken, 4);

  /* And on a variable that is a byte to begin with. */
  taken = 0;
  byteflags = 0x22;
  if (byteflags & 0x02) act();
  if (byteflags & 0x20) act();
  if (byteflags & 0x04) act();
  puthex((u16)taken, 4);

  /* A word the compiler is free to keep in a register between accesses, which
     is where the read-change-write forms are picked rather than the branch. */
  plain = 0;
  plain |= 1u << 5;
  plain |= 1u << 12;
  plain &= (u16)~(1u << 5);
  puthex(plain, 4);

  /* The initialised half, which says the startup code copied this section. */
  puthex(seeded, 4);
  seeded ^= 0xFFFF;
  puthex(seeded, 4);
  return 0;
}
