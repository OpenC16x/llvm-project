/* Compiled twice: once for c166 and run in the simulator, once for the host
   and run natively.  The two outputs must match exactly.

   The C library, called through its public C names and linked out of libc.a
   in the sysroot.  That is the difference between this and the drivers in
   corpus/, which compile llvm-libc's sources into both sides and call them
   through their namespace: those check the code generator against the host on
   the same source, and this checks the library a program on this part actually
   gets - the archive, the aliases that give the public names, and which member
   of the archive won where two define the same function.

   Most of what is called here had never been executed on this target before
   this file existed.  The four directories of llvm-libc that the sysroot
   builds are a hundred and five sources, and the corpus reached about thirty
   of them; the rest compiled and were never run.

   Three groups are deliberately not here:

     rand and srand, because the sequence is the implementation's to choose and
     the host's is a different one.  There is nothing to compare.

     the multibyte conversions, because wchar_t is sixteen bits on this part
     and thirty two on the host, so mbstowcs writes a different number of bytes
     on each side and being right looks like being wrong.

     the floating point conversions, because they do not work on this part and
     the reason is not the compiler's.  strtof and strtod hand anything the
     first pass cannot bound to simple_decimal_conversion, which puts an
     800 byte HighPrecisionDecimal on the stack; with its callers that chain
     needs about 1.1 KByte, and the ABI stack in the linker scripts here is the
     1 KByte between F600H and FA00H.  It overruns, silently, and what comes
     back depends on the optimisation level: strtof("1e39") is 1.4e-45 instead
     of infinity at every level, with the end pointer left unwritten, and the
     same call inside a longer program does not return at all.  An input the
     first pass can bound - "1e300" for a float, where the base ten exponent is
     past anything that could be finite - never reaches that path and is
     correct.  llvm/lib/Target/C166/startup/README.txt has the measurement.

   memalignment, memset_explicit and strnlen_s have reference definitions below
   for the host, which does not have them - they are C23 and C11 Annex K, and
   this glibc has neither.  Each is a few lines out of the standard, so what is
   being compared is still llvm-libc against the definition rather than against
   nothing.  */

#ifndef __c166__
#define _GNU_SOURCE /* memmem, strcasestr, strchrnul, mempcpy, a64l */
#endif

#include <ctype.h>
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>

#ifdef __c166__
static void put(char c) { *(__far volatile unsigned char *)0xFF0000 = c; }
#else
#include <stdio.h>
static void put(char c) { putchar(c); }
#endif

typedef __UINT8_TYPE__ u8;
typedef __UINT16_TYPE__ u16;
typedef __UINT32_TYPE__ u32;
typedef __INT32_TYPE__ s32;
typedef __INT64_TYPE__ s64;
typedef __UINT64_TYPE__ u64;

/* Fixed width everywhere a number is printed.  "long" is thirty two bits on
   the part and sixty four on the machine it is compared against, and "size_t"
   is sixteen against sixty four; a value printed at its own width would differ
   on the two sides for a reason that is not a bug.  */
static void puthex(u32 v, int digits) {
  for (int i = digits - 1; i >= 0; i--)
    put("0123456789abcdef"[(v >> (i * 4)) & 0xF]);
}

static void puts_(const char *s) {
  while (*s)
    put(*s++);
}

static void line(const char *tag, u32 v) {
  puts_(tag);
  put(' ');
  puthex(v, 8);
  put('\n');
}

static void line64(const char *tag, u64 v) {
  puts_(tag);
  put(' ');
  puthex((u32)(v >> 32), 8);
  puthex((u32)v, 8);
  put('\n');
}

/* A checksum rather than the first byte of a buffer, so that a function which
   wrote one element too many is a wrong answer and not a silent success.  */
static u32 sum(const void *p, unsigned n) {
  const u8 *b = (const u8 *)p;
  u32 s = 0x9e3779b9u;
  for (unsigned i = 0; i < n; i++)
    s = s * 16777619u + b[i];
  return s;
}

/* The distance from a buffer to a pointer into it, or a value that cannot be a
   distance when the pointer is null.  Printing the pointer itself would print
   two different addresses.  */
static u32 off(const void *p, const void *base) {
  return p ? (u32)((const char *)p - (const char *)base) : 0xFFFFu;
}

/* What the host is missing.  These are the definitions, not clever versions of
   them: the point is to have something correct to compare against.  */
#ifndef __c166__
static size_t memalignment(const void *p) {
  __UINTPTR_TYPE__ v = (__UINTPTR_TYPE__)p;
  return v ? (size_t)(v & -v) : 0;
}
static void *memset_explicit(void *dst, int c, size_t n) {
  volatile unsigned char *d = (volatile unsigned char *)dst;
  while (n--)
    *d++ = (unsigned char)c;
  return dst;
}
static size_t strnlen_s(const char *s, size_t n) {
  return s ? strnlen(s, n) : 0;
}
#endif

/* ------------------------------------------------------------------ ctype */

/* Every classification of every argument the standard allows, which is EOF and
   the values of an unsigned char, packed one bit per function so that a single
   number covers the lot and names which one disagreed if it does.  */
static void ctype_all(void) {
  u32 s = 0;
  for (int c = -1; c < 256; c++) {
    u16 bits = 0;
    bits |= (u16)(!!isalnum(c) << 0);
    bits |= (u16)(!!isalpha(c) << 1);
    bits |= (u16)(!!isblank(c) << 2);
    bits |= (u16)(!!iscntrl(c) << 3);
    bits |= (u16)(!!isdigit(c) << 4);
    bits |= (u16)(!!isgraph(c) << 5);
    bits |= (u16)(!!islower(c) << 6);
    bits |= (u16)(!!isprint(c) << 7);
    bits |= (u16)(!!ispunct(c) << 8);
    bits |= (u16)(!!isspace(c) << 9);
    bits |= (u16)(!!isupper(c) << 10);
    bits |= (u16)(!!isxdigit(c) << 11);
    s = s * 16777619u + bits;
    s = s * 16777619u + (u16)(tolower(c) & 0xFFFF);
    s = s * 16777619u + (u16)(toupper(c) & 0xFFFF);
  }
  line("ctype", s);

  /* isascii and toascii separately: they are defined for every int, not only
     for the range above, so the range above would not have exercised them. */
  s = 0;
  for (s32 c = -300; c < 400; c++) {
    s = s * 16777619u + (u32)!!isascii((int)c);
    s = s * 16777619u + (u32)(toascii((int)c) & 0xFF);
  }
  line("ascii", s);
}

/* ----------------------------------------------------------------- string */

static const char *const words[] = {"",       "a",     "ab",    "abc",
                                    "abcabc", "aaaa",  "Hello", "hello",
                                    "  x  ",  "zzzzz", "abcdefghij"};
#define NWORDS ((unsigned)(sizeof words / sizeof words[0]))

static void string_scans(void) {
  u32 s = 0;
  for (unsigned i = 0; i < NWORDS; i++) {
    const char *w = words[i];
    unsigned n = (unsigned)strlen(w);
    for (int c = 0; c < 128; c += 7) {
      s = s * 31u + off(strchrnul(w, c), w);
      s = s * 31u + off(memrchr(w, c, n), w);
      s = s * 31u + off(memchr(w, c, n), w);
    }
    for (unsigned j = 0; j < NWORDS; j++) {
      const char *v = words[j];
      s = s * 31u + off(strcasestr(w, v), w);
      s = s * 31u + off(memmem(w, n, v, (unsigned)strlen(v)), w);
      s = s * 31u + (u32)(strcoll(w, v) < 0 ? 1 : strcoll(w, v) > 0 ? 2 : 0);
    }
    s = s * 31u + (u32)strnlen(w, 3);
    s = s * 31u + (u32)strnlen_s(w, 3);
  }
  line("scans", s);
}

/* The copies, each into a buffer with a known pattern already in it and each
   checksummed over the whole buffer, so that a byte written past the end is
   caught.  The return value goes in too: half of these exist because of what
   they return rather than what they write.  */
static void string_copies(void) {
  u32 s = 0;
  char buf[24];
  for (unsigned i = 0; i < NWORDS; i++) {
    const char *w = words[i];
    unsigned n = (unsigned)strlen(w);

    memset(buf, '.', sizeof buf);
    s = s * 31u + off(stpcpy(buf, w), buf) + sum(buf, sizeof buf);

    for (unsigned k = 0; k <= 8; k += 4) {
      memset(buf, '.', sizeof buf);
      s = s * 31u + off(stpncpy(buf, w, k), buf) + sum(buf, sizeof buf);

      memset(buf, '.', sizeof buf);
      buf[0] = 0;
      s = s * 31u + (u32)strlcpy(buf, w, k) + sum(buf, sizeof buf);

      memset(buf, '.', sizeof buf);
      buf[0] = 'q';
      buf[1] = 0;
      s = s * 31u + (u32)strlcat(buf, w, k) + sum(buf, sizeof buf);
    }

    memset(buf, '.', sizeof buf);
    s = s * 31u + off(mempcpy(buf, w, n), buf) + sum(buf, sizeof buf);

    for (int c = 'a'; c <= 'c'; c++) {
      memset(buf, '.', sizeof buf);
      s = s * 31u + off(memccpy(buf, w, c, n), buf) + sum(buf, sizeof buf);
    }

    memset(buf, '.', sizeof buf);
    memcpy(buf, w, n + 1);
    memset_explicit(buf, 0, n);
    s = s * 31u + sum(buf, sizeof buf);
  }
  line("copies", s);
}

/* The functions that take a size, asked for sizes at the ends of what a size
   can be.  size_t is sixteen bits on this part and sixty four on the host, so
   (size_t)-1 is a different number on the two sides and the answers still have
   to match: every one of these is defined to stop at something else first - a
   terminator, the end of the source - and a length that says "no limit" is the
   case where an implementation that counts down from it, or compares against
   it, or adds to it, gets a different answer on a narrow one.  A zero size is
   the other end of the same question.

   The character arguments go outside unsigned char for the same reason: memchr
   and its neighbours are defined to convert theirs to unsigned char, so
   'a' + 256 has to find 'a' and -1 has to find 0xFF, and an implementation
   that compared an int against a char would pass every test that stayed in
   range.  */
static void string_extremes(void) {
  u32 s = 0;
  char buf[32], src[32];
  const size_t nolimit = (size_t)-1;

  for (unsigned i = 0; i < NWORDS; i++) {
    const char *w = words[i];
    unsigned n = (unsigned)strlen(w);

    s = s * 31u + (u32)strnlen(w, nolimit);
    s = s * 31u + (u32)strnlen(w, 0);
    s = s * 31u + (u32)strnlen_s(w, nolimit);
    s = s * 31u + (u32)strnlen_s(w, 0);

    /* Only the functions that read are asked for a size of (size_t)-1.  For
       one that writes it is a claim about the destination rather than a limit
       on the source, and an untrue one: this host's fortified strlcpy aborts
       the program for making it, correctly. */
    memset(buf, '.', sizeof buf);
    s = s * 31u + (u32)strlcpy(buf, w, sizeof buf) + sum(buf, sizeof buf);
    memset(buf, '.', sizeof buf);
    s = s * 31u + (u32)strlcpy(buf, w, 0) + sum(buf, sizeof buf);

    memset(buf, '.', sizeof buf);
    buf[0] = 'q';
    buf[1] = 0;
    s = s * 31u + (u32)strlcat(buf, w, sizeof buf) + sum(buf, sizeof buf);
    memset(buf, '.', sizeof buf);
    buf[0] = 'q';
    buf[1] = 0;
    s = s * 31u + (u32)strlcat(buf, w, 0) + sum(buf, sizeof buf);

    /* strxfrm with a size of zero is how a caller asks how much room it would
       need, and it must not write through the pointer to say so. */
    memset(buf, '.', sizeof buf);
    s = s * 31u + (u32)strxfrm(buf, w, 0) + sum(buf, sizeof buf);
    memset(buf, '.', sizeof buf);
    s = s * 31u + (u32)strxfrm(buf, w, sizeof buf) + sum(buf, sizeof buf);

    /* A character argument that is not an unsigned char.  Both of these are
       defined: the value is converted, so the match is on the low eight
       bits. */
    memcpy(src, w, n + 1);
    src[n] = (char)0xFF;
    src[n + 1] = 0;
    s = s * 31u + off(memchr(src, -1, n + 1), src);
    s = s * 31u + off(memrchr(src, -1, n + 1), src);
    s = s * 31u + off(strchr(src, 'a' + 256), src);
    s = s * 31u + off(strchrnul(src, 'a' + 256), src);
    memset(buf, '.', sizeof buf);
    s = s * 31u + off(memccpy(buf, src, 'a' + 256, n + 1), buf) +
        sum(buf, sizeof buf);

    /* And the scans that corpus/strings.cpp reaches through the namespace,
       reached here through the archive instead. */
    for (unsigned j = 0; j < NWORDS; j++) {
      const char *v = words[j];
      s = s * 31u + off(strstr(w, v), w);
      s = s * 31u + off(strpbrk(w, v), w);
      s = s * 31u + (u32)strspn(w, v);
      s = s * 31u + (u32)strcspn(w, v);
    }
  }
  line("extremes", s);
}

/* strtok, strtok_r and strsep over the same input, which do not agree with
   each other and are each meant not to: strsep returns the empty field between
   two separators and the other two skip it.  */
static void string_split(void) {
  static const char *const inputs[] = {"a,b,c", ",,", "a,,b", "abc", "",
                                       ",a", "a,", "  a b  c "};
  static const char *const seps[] = {",", " ", ",;", " ,"};
  u32 s = 0;
  char buf[16];

  for (unsigned i = 0; i < sizeof inputs / sizeof inputs[0]; i++) {
    for (unsigned j = 0; j < sizeof seps / sizeof seps[0]; j++) {
      char *save, *tok;

      strcpy(buf, inputs[i]);
      for (tok = strtok(buf, seps[j]); tok; tok = strtok(0, seps[j]))
        s = s * 31u + sum(tok, (unsigned)strlen(tok)) + 1;

      strcpy(buf, inputs[i]);
      for (tok = strtok_r(buf, seps[j], &save); tok;
           tok = strtok_r(0, seps[j], &save))
        s = s * 31u + sum(tok, (unsigned)strlen(tok)) + 2;

      strcpy(buf, inputs[i]);
      { char *p = buf;
        while ((tok = strsep(&p, seps[j])) != 0)
          s = s * 31u + sum(tok, (unsigned)strlen(tok)) + 3;
      }
    }
  }
  line("split", s);
}

/* ----------------------------------------------------------------- stdlib */

static void stdlib_arith(void) {
  static const s32 v32[] = {0,      1,      -1,    2,     -2,      32767,
                            -32768, 65535,  -65535, 100000, -100000,
                            2147483647, -2147483647};
  u32 s = 0;
  for (unsigned i = 0; i < sizeof v32 / sizeof v32[0]; i++) {
    s = s * 31u + (u32)abs((int)(v32[i] & 0x7FFF));
    s = s * 31u + (u32)labs((long)v32[i]);
    s = s * 31u + (u32)llabs((long long)v32[i]);
    for (unsigned j = 1; j < sizeof v32 / sizeof v32[0]; j++) {
      s32 d = v32[j];
      if (!d)
        continue;
      ldiv_t l = ldiv((long)v32[i], (long)d);
      s = s * 31u + (u32)l.quot + (u32)l.rem;
      lldiv_t ll = lldiv((long long)v32[i], (long long)d);
      s = s * 31u + (u32)ll.quot + (u32)ll.rem;
      /* "int" is sixteen bits here and thirty two on the host, so div is
         only called where the quotient fits in the narrower of the two.
         div(-32768, -1) is 32768, which does not, and undefined behaviour on
         one side is not a disagreement worth reporting. */
      if (v32[i] >= -32768 && v32[i] <= 32767 && d >= -32768 && d <= 32767 &&
          !(v32[i] == -32768 && d == -1)) {
        div_t q = div((int)v32[i], (int)d);
        s = s * 31u + (u32)q.quot + (u32)q.rem;
      }
    }
  }
  line("arith", s);

  /* memalignment on addresses of every alignment a stack object can have.  It
     is a value the standard defines and not an address, so it compares. */
  s = 0;
  {
    static _Alignas(64) char block[128];
    for (unsigned i = 1; i < 64; i++)
      s = s * 31u + (u32)memalignment(block + i);
  }
  line("align", s);
}

/* atoi, atol and atoll on inputs at and either side of what each can hold,
   including the ones where the answer is that the conversion stopped.  Their
   widths differ between the two sides, so the printed value is masked to the
   narrower of the two - what is being checked is the parse.  */
static void stdlib_convert(void) {
  static const char *const nums[] = {
      "0",     "1",      "-1",       "+42",     "  12",    "\t\n 7",
      "0x10",  "010",    "123abc",   "abc",     "",        "-",
      "32767", "32768",  "-32768",   "-32769",  "65535",   "65536",
      "2147483647", "2147483648", "-2147483648", "-2147483649",
      "9223372036854775807", "9223372036854775808",
      "-9223372036854775808", "  -0009", "999999999999999999999"};
  u32 s = 0;
  for (unsigned i = 0; i < sizeof nums / sizeof nums[0]; i++) {
    /* atoll is sixty four bits on both sides and takes every input.  atoi and
       atol do not: "int" is sixteen bits here against thirty two, and "long"
       thirty two against sixty four, so each is asked only for values that fit
       in the narrower of its two widths.  Outside that the standard says the
       behaviour is undefined and the two sides are entitled to differ. */
    s64 v = atoll(nums[i]);
    s = s * 31u + (u32)(u64)v;
    s = s * 31u + (u32)((u64)v >> 32);
    if (v >= -32768 && v <= 32767)
      s = s * 31u + (u16)atoi(nums[i]) + 1u;
    if (v >= -2147483647 - 1 && v <= 2147483647)
      s = s * 31u + (u32)atol(nums[i]) + 2u;
  }
  line("atox", s);

  /* a64l and l64a as a pair: what goes through l64a has to come back through
     a64l.  The string itself is not compared, and that is not caution - it is
     that llvm-libc's l64a and this host's are both right and different.  The
     standard fixes the alphabet and the order but not the length, and
     llvm-libc writes all six digits where glibc stops at the last non-zero
     one, so l64a(1) is "/" there and "/....." here.  '.' is the digit zero, so
     the two strings mean the same number and a64l reads either.  Nothing about
     that is this target's: the same source says the same thing on any
     machine. */
  s = 0;
  for (s32 i = 0; i < 0x40000; i += 1237) {
    char *e = l64a((long)i);
    s = s * 31u + (u32)a64l(e);
  }
  line("a64l", s);
}

/* qsort, qsort_r and bsearch over element sizes that are and are not a
   multiple of the word, because a sort that moves elements it only knows the
   size of moves them a byte at a time and an odd size is the case that finds
   an alignment assumption.  */
typedef struct { u16 key; u8 pad[3]; } odd_t;

static int cmp_u16(const void *a, const void *b) {
  u16 x = *(const u16 *)a, y = *(const u16 *)b;
  return x < y ? -1 : x > y ? 1 : 0;
}
static int cmp_odd(const void *a, const void *b) {
  return cmp_u16(&((const odd_t *)a)->key, &((const odd_t *)b)->key);
}
static int cmp_r(const void *a, const void *b, void *arg) {
  int sign = *(const int *)arg;
  return sign * cmp_u16(a, b);
}

static void stdlib_sort(void) {
  u16 keys[64];
  odd_t odds[64];
  u32 s = 0;
  u32 seed = 12345;

  for (unsigned n = 0; n <= 64; n += 8) {
    for (unsigned i = 0; i < n; i++) {
      seed = seed * 1103515245u + 12345u;
      keys[i] = (u16)(seed >> 16);
      odds[i].key = keys[i];
      odds[i].pad[0] = (u8)i;
      odds[i].pad[1] = (u8)(i * 3);
      odds[i].pad[2] = (u8)(i * 5);
    }

    qsort(keys, n, sizeof keys[0], cmp_u16);
    s = s * 31u + sum(keys, n * (unsigned)sizeof keys[0]);

    qsort(odds, n, sizeof odds[0], cmp_odd);
    for (unsigned i = 0; i < n; i++)
      s = s * 31u + odds[i].key + odds[i].pad[0];

    { int sign = -1;
      qsort_r(keys, n, sizeof keys[0], cmp_r, &sign);
      s = s * 31u + sum(keys, n * (unsigned)sizeof keys[0]);
      sign = 1;
      qsort_r(keys, n, sizeof keys[0], cmp_r, &sign);
      s = s * 31u + sum(keys, n * (unsigned)sizeof keys[0]);
    }

    for (unsigned i = 0; i < n; i++) {
      u16 k = keys[i];
      s = s * 31u + off(bsearch(&k, keys, n, sizeof keys[0], cmp_u16), keys);
    }
    { u16 missing = 0;
      s = s * 31u + off(bsearch(&missing, keys, n, sizeof keys[0], cmp_u16),
                        keys);
    }
  }
  line("sort", s);
}

/* --------------------------------------------------------------- inttypes */

static void inttypes_all(void) {
  static const s64 v[] = {0, 1, -1, 32768, -32768, 2147483647LL, -2147483648LL,
                          9223372036854775807LL, 1000000007LL};
  u64 s = 0;
  for (unsigned i = 0; i < sizeof v / sizeof v[0]; i++) {
    s = s * 1099511628211ULL + (u64)imaxabs((intmax_t)v[i]);
    for (unsigned j = 1; j < sizeof v / sizeof v[0]; j++) {
      if (!v[j])
        continue;
      imaxdiv_t d = imaxdiv((intmax_t)v[i], (intmax_t)v[j]);
      s = s * 1099511628211ULL + (u64)d.quot;
      s = s * 1099511628211ULL + (u64)d.rem;
    }
  }
  line64("imax", s);

  /* strtoimax and strtoumax on all three of what they return, where they
     stopped, and the sign they made of a leading minus on the unsigned one. */
  static const char *const texts[] = {
      "0", "-0", "+7", "  -42xyz", "0x7fffffffffffffff", "0xffffffffffffffff",
      "9223372036854775808", "-9223372036854775809", "z", "0b11", "777",
      "18446744073709551615", "18446744073709551616", "-1"};
  static const int bases[] = {0, 2, 8, 10, 16, 36};
  s = 0;
  for (unsigned i = 0; i < sizeof texts / sizeof texts[0]; i++)
    for (unsigned b = 0; b < sizeof bases / sizeof bases[0]; b++) {
      char *end;
      s = s * 1099511628211ULL + (u64)strtoimax(texts[i], &end, bases[b]);
      s = s * 1099511628211ULL + off(end, texts[i]);
      s = s * 1099511628211ULL + (u64)strtoumax(texts[i], &end, bases[b]);
      s = s * 1099511628211ULL + off(end, texts[i]);
    }
  line64("strtoimax", s);
}

int main(void) {
  ctype_all();
  string_scans();
  string_copies();
  string_split();
  string_extremes();
  stdlib_arith();
  stdlib_convert();
  stdlib_sort();
  inttypes_all();
  return 0;
}
