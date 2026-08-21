// RUN: %clang_cc1 -triple c166 -fsyntax-only -verify %s
// expected-no-diagnostics

// A 16 bit machine: int is a register, and nothing needs more than word
// alignment because the bus is a word wide.
_Static_assert(sizeof(char) == 1, "");
_Static_assert(sizeof(short) == 2, "");
_Static_assert(sizeof(int) == 2, "");
_Static_assert(sizeof(long) == 4, "");
_Static_assert(sizeof(long long) == 8, "");
_Static_assert(sizeof(float) == 4, "");
_Static_assert(sizeof(double) == 8, "");
_Static_assert(sizeof(long double) == 8, "");

_Static_assert(_Alignof(long) == 2, "");
_Static_assert(_Alignof(long long) == 2, "");
_Static_assert(_Alignof(double) == 2, "");

_Static_assert(sizeof(void *) == 2, "a near pointer is a word");
_Static_assert(_Alignof(void *) == 2, "");
_Static_assert(sizeof(__far void *) == 4, "a far pointer is two words");
_Static_assert(_Alignof(__far void *) == 2, "");

_Static_assert(sizeof(__SIZE_TYPE__) == 2, "");
_Static_assert(sizeof(__PTRDIFF_TYPE__) == 2, "");
_Static_assert((__SIZE_TYPE__)-1 > 0, "size_t is unsigned");

// A far pointer needs a cast on the way back down: the top eight bits have
// nowhere to go.
int *narrow(__far int *p) { return (int *)p; }
