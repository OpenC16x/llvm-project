/* Compiled twice: once for c166 and run in the simulator, once for the host
   and run natively.  The two outputs must match exactly. */
#ifdef __c166__
static void put(char c) { *(__far volatile unsigned char *)0xFF0000 = c; }
#else
#include <stdio.h>
static void put(char c) { putchar(c); }
#endif

/* exact widths, since long is 32 bits on the C166 and 64 on the host */
typedef __UINT16_TYPE__ u16; typedef __INT16_TYPE__ s16;
typedef __UINT32_TYPE__ u32; typedef __INT32_TYPE__ s32;

static void puthex(u32 v, int digits) {
  for (int i = digits - 1; i >= 0; i--)
    put("0123456789abcdef"[(v >> (i * 4)) & 0xF]);
  put('\n');
}

static const s16 vals16[] = {0, 1, -1, 2, -2, 32767, -32768, 255, -256,
                             0x1234, -0x1234, 100, -100, 7, -7, 0x5555};

int main(void) {
  u32 acc = 0;

  /* every signed and unsigned comparison, over every pair */
  for (unsigned i = 0; i < 16; i++)
    for (unsigned j = 0; j < 16; j++) {
      s16 a = vals16[i], b = vals16[j];
      u16 ua = (u16)a, ub = (u16)b;
      unsigned bits = 0;
      bits |= (a == b) << 0;  bits |= (a != b) << 1;
      bits |= (a <  b) << 2;  bits |= (a <= b) << 3;
      bits |= (a >  b) << 4;  bits |= (a >= b) << 5;
      bits |= (ua <  ub) << 6; bits |= (ua <= ub) << 7;
      bits |= (ua >  ub) << 8; bits |= (ua >= ub) << 9;
      acc = acc * 31 + bits;
    }
  puthex(acc, 8);

  /* word arithmetic, including the overflow edges */
  acc = 0;
  for (unsigned i = 0; i < 16; i++)
    for (unsigned j = 0; j < 16; j++) {
      s16 a = vals16[i], b = vals16[j];
      acc = acc * 33 + (u16)(a + b);
      acc = acc * 33 + (u16)(a - b);
      acc = acc * 33 + (u16)(a * b);
      acc = acc * 33 + (u16)(a & b) + (u16)(a | b) + (u16)(a ^ b);
      acc = acc * 33 + (u16)(~a);
      acc = acc * 33 + (u16)(-a);
      if (b != 0 && !(a == -32768 && b == -1)) {
        acc = acc * 33 + (u16)(a / b);
        acc = acc * 33 + (u16)(a % b);
        acc = acc * 33 + (u16)((u16)a / (u16)b);
        acc = acc * 33 + (u16)((u16)a % (u16)b);
      }
    }
  puthex(acc, 8);

  /* shifts of every count */
  acc = 0;
  for (unsigned i = 0; i < 16; i++)
    for (unsigned n = 0; n < 16; n++) {
      u16 ua = (u16)vals16[i];
      s16 sa = vals16[i];
      acc = acc * 37 + (u16)(ua << n);
      acc = acc * 37 + (u16)(ua >> n);
      acc = acc * 37 + (u16)(sa >> n);
    }
  puthex(acc, 8);

  /* byte arithmetic */
  acc = 0;
  for (unsigned i = 0; i < 256; i += 7)
    for (unsigned j = 1; j < 256; j += 11) {
      unsigned char a = (unsigned char)i, b = (unsigned char)j;
      signed char sa = (signed char)i, sb = (signed char)j;
      acc = acc * 41 + (unsigned char)(a + b);
      acc = acc * 41 + (unsigned char)(a - b);
      acc = acc * 41 + (unsigned char)(a * b);
      acc = acc * 41 + (unsigned char)(a / b);
      acc = acc * 41 + (unsigned char)(a % b);
      acc = acc * 41 + (unsigned char)(sa / (sb ? sb : 1));
      acc = acc * 41 + (unsigned char)(a ^ b) + (unsigned char)(a & b);
    }
  puthex(acc, 8);

  /* 32 bit arithmetic, which is where the carry chains and the libcalls are */
  acc = 0;
  {
    static const u32 v32[] = {0, 1, 0xFFFFFFFFu, 0x80000000u, 0x7FFFFFFFu,
                              0x12345678u, 0xDEADBEEFu, 0x0000FFFFu,
                              0xFFFF0000u, 3, 100000u, 0xABCDu};
    for (unsigned i = 0; i < 12; i++)
      for (unsigned j = 0; j < 12; j++) {
        u32 a = v32[i], b = v32[j];
        s32 sa = (s32)a, sb = (s32)b;
        acc = acc * 43 + (a + b);
        acc = acc * 43 + (a - b);
        acc = acc * 43 + (a * b);
        acc = acc * 43 + (a ^ b) + (a & b) + (a | b);
        for (unsigned n = 0; n < 32; n += 5) {
          acc = acc * 43 + (a << n);
          acc = acc * 43 + (a >> n);
          acc = acc * 43 + (u32)(sa >> n);
        }
        if (b) {
          acc = acc * 43 + a / b;
          acc = acc * 43 + a % b;
          if (!(sa == (s32)0x80000000u && sb == -1)) {
            acc = acc * 43 + (u32)(sa / sb);
            acc = acc * 43 + (u32)(sa % sb);
          }
        }
      }
  }
  puthex(acc, 8);

  return 0;
}
