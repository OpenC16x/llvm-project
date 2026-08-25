/* The parts of <stdlib.h> a freestanding part can mean.
 *
 * What is declared and not defined is deliberate: a program that calls
 * malloc on a part with two kilobytes of RAM and no allocator gets a link
 * error naming it, which is a better answer than a header that hides it or an
 * allocator nobody chose the policy of.
 *
 * exit and abort are defined, in crt0 and the C library, because the runtime
 * calls them whether or not the program does.
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

int atoi(const char *s);
long atol(const char *s);
long long atoll(const char *s);
long strtol(const char *s, char **end, int base);
unsigned long strtoul(const char *s, char **end, int base);
long long strtoll(const char *s, char **end, int base);
unsigned long long strtoull(const char *s, char **end, int base);

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

/* No atexit: exit here does not run handlers - there is nothing for one to
 * clean up on a part that stops rather than returning to anything - and a
 * declaration of it would promise that it did. */
char *getenv(const char *name);

#ifdef __cplusplus
}
#endif

#endif /* _C166_STDLIB_H */
