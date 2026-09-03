/* The parts of <stdlib.h> a freestanding part can mean.
 *
 * Most of these are defined now: libc.a carries LLVM's own libc, and what it
 * has of stdlib is the half that is about values rather than about a process -
 * the conversions, the integer arithmetic, the two sorts, the generator.
 *
 * What is declared and not defined is still deliberate, and it is now down to
 * two groups.  A program that calls malloc on a part with two kilobytes of RAM
 * and no allocator gets a link error naming it, which is a better answer than
 * a header that hides it or an allocator nobody chose the policy of.  And
 * getenv is left out on purpose: llvm-libc has it, but it reads an environment
 * block, and nothing starts a program on this part that could pass one.
 *
 * exit and abort are defined in the C library rather than by llvm-libc, and
 * come first in the archive so that they win: what exit means here is
 * stopping, and llvm-libc's would call an operating system to do it.
 *
 * Part of the LLVM Project, under the Apache License v2.0 with LLVM
 * Exceptions.  See https://llvm.org/LICENSE.txt for license information.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */

#ifndef _C166_STDLIB_H
#define _C166_STDLIB_H

#include <stddef.h>

#define EXIT_SUCCESS 0
#define EXIT_FAILURE 1

/* rand() returns at most this.  It is the minimum the standard allows, and the
 * right value for a part with no C library of its own to say otherwise: a
 * generator masks its output down to RAND_MAX, so whatever is linked in
 * produces a range this header decides. */
#define RAND_MAX 32767

/* One byte: nothing here has a multibyte encoding to be more than that.  It is
 * a size_t because the standard says so, and because a program comparing it
 * against one should not get a signed comparison. */
#define MB_CUR_MAX ((size_t)1)

typedef struct {
  int quot;
  int rem;
} div_t;

typedef struct {
  long quot;
  long rem;
} ldiv_t;

typedef struct {
  long long quot;
  long long rem;
} lldiv_t;

#ifdef __cplusplus
extern "C" {
#endif

/* Defined: the runtime reaches these itself. */
void exit(int status) __attribute__((noreturn));
void _Exit(int status) __attribute__((noreturn));
void abort(void) __attribute__((noreturn));

/* Declared: a program that wants one brings it. */
void *malloc(size_t size);
void *calloc(size_t count, size_t size);
void *realloc(void *ptr, size_t size);
void free(void *ptr);
void *aligned_alloc(size_t alignment, size_t size);

/* Defined.  "long" is thirty two bits on this part, so strtol and strtoul are
 * the thirty two bit conversions and atol is a thirty two bit atoi. */
int atoi(const char *s);
long atol(const char *s);
long long atoll(const char *s);
long strtol(const char *s, char **end, int base);
unsigned long strtoul(const char *s, char **end, int base);
long long strtoll(const char *s, char **end, int base);
unsigned long long strtoull(const char *s, char **end, int base);

/* The floating point conversions, which are declared, are in libc.a, and do
 * not work.  Read this before calling one.
 *
 * Anything the first pass cannot bound goes to llvm-libc's
 * simple_decimal_conversion, which puts an 800 byte object on the stack; the
 * chain needs about 1.1 KByte and the ABI stack in the scripts beside this
 * header is the 1 KByte between F600H and FA00H.  It overruns it without
 * saying so.  A program that wants these needs a larger ABI stack than the
 * memory map here has anywhere to put, which is a fact about the part and not
 * about the library: see "The C library" in ../README.txt.
 *
 * There is also no floating point unit, so every one of these runs on the
 * compiler-rt builtins and a program that calls one carries them - see the
 * note in llvm/utils/C166Sim/differential/run.sh about what that costs in near
 * ROM. */
double atof(const char *s);
float strtof(const char *s, char **end);
double strtod(const char *s, char **end);
long double strtold(const char *s, char **end);

int abs(int v);
long labs(long v);
long long llabs(long long v);
div_t div(int num, int den);
ldiv_t ldiv(long num, long den);
lldiv_t lldiv(long long num, long long den);

int rand(void);
void srand(unsigned seed);

void qsort(void *base, size_t count, size_t size,
           int (*compare)(const void *, const void *));
void *bsearch(const void *key, const void *base, size_t count, size_t size,
              int (*compare)(const void *, const void *));

/* POSIX 1003.1-2024's argument order, which is the one llvm-libc implements
 * and the one glibc has: the context is the comparison's last argument. */
void qsort_r(void *base, size_t count, size_t size,
             int (*compare)(const void *, const void *, void *), void *arg);

/* The multibyte conversions.  wchar_t is sixteen bits here - see <wchar.h> for
 * why - so these convert to and from a type that holds the basic character set
 * and not a code point. */
int mblen(const char *s, size_t n);
int mbtowc(wchar_t *wc, const char *s, size_t n);
size_t mbstowcs(wchar_t *dst, const char *src, size_t n);
size_t wcstombs(char *dst, const wchar_t *src, size_t n);

/* C23: the largest power of two the pointer is aligned to. */
size_t memalignment(const void *p);

/* POSIX, and as old as it: the base 64 conversions.  l64a returns a pointer
 * into a buffer of its own that the next call overwrites. */
long a64l(const char *s);
char *l64a(long v);

/* No atexit: exit here does not run handlers - there is nothing for one to
 * clean up on a part that stops rather than returning to anything - and a
 * declaration of it would promise that it did.
 *
 * getenv is declared and not defined, for the reason at the top of this file:
 * there is no environment on this part for it to read. */
char *getenv(const char *name);

#ifdef __cplusplus
}
#endif

#endif /* _C166_STDLIB_H */
