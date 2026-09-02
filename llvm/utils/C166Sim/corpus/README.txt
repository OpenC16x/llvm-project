A corpus of code nobody here wrote
==================================

The programs in differential/ were written to exercise the backend, so they
reach what somebody thought to reach.  These are drivers over LLVM's own libc -
code written by other people, to solve a different problem, with no idea this
target exists.  What it reaches was not chosen.

Each driver is compiled twice, once for the C166 and once for the machine
running it, and the two have to produce the same output.  The libc sources are
compiled from the same files for both sides, so a disagreement is the C166
backend's rather than libc's, and they are called through their namespace
rather than through an alias, so there is no question about which
implementation ran on the host.

  run.sh <build-bin-dir> <sysroot> <linker-script> [levels]

The sysroot is the one differential/mksysroot.sh builds.

Beside it is sizes.sh, which builds the same programs for the target alone and
checks what they cost against baseline.txt:

  sizes.sh <build-bin-dir> <sysroot> <linker-script> [--update]

Every size and speed claim this backend has made was measured by hand at the
time it was made, and until this existed nothing checked that any of them still
held.  It records text bytes and states for each program at each level, fails
on a growth of more than one per cent, and prints an improvement as loudly as a
regression - a baseline that is only updated when something gets worse would
let a later regression back to the old value pass unnoticed.  --update
rewrites the file, and the convention is to do that in the same commit as the
change that moves it, so that the trade is in the diff rather than in a log.

It does not compile for the host and does not compare any output; that is
run.sh's job above and this would only repeat it.  It does run each program,
because the state count comes from running it and because one that stopped
early would report a flattering number.

  strings.cpp   libc/src/string - two-pointer scans, tables built on the
                stack, and the word-at-a-time loops that
                libc/src/__support/CPP/simd.h builds out of _BitInt
  sorting.cpp   libc/src/stdlib - an introsort with a heapsort fallback,
                calling back through a function pointer for every comparison
                and moving elements it only knows the size of a byte at a time
  numbers.cpp   libc/src/__support/integer_to_string.h - a template that
                formats into a buffer it sizes at compile time, in whatever
                base it is asked for, dividing its way down; on a sixteen bit
                ALU a 64 bit value in base ten is a long chain of calls into
                the compiler-rt builtins
  parsing.cpp   libc/src/stdlib and libc/src/inttypes - the strtol family,
                checked on all three of what they return, where they stopped
                and what they left in errno, at six bases and across the
                sixteen, thirty two and sixty four bit boundaries

What it found
-------------

Twenty two of libc's sources would not compile for this target at all: its
word-at-a-time loops are built out of _BitInt, and clang's C166 target did not
say it had _BitInt, so hasBitIntType() returned the default of false.  Turning
it on is all that was needed - the backend legalises an odd width the way it
legalises anything else that is not sixteen bits - and it took the sources that
compile from 48 to 70.  That is the shape of thing this corpus is for: nothing
written here would have used _BitInt, so nothing written here would have found
it.

Widening the sysroot to a set of headers took that from 70 to 104 of the 129
sources in string, stdlib, ctype and inttypes.  Three of the things standing in
the way were compiler bugs rather than missing headers:

  new char[n] crashed clang outright.  CodeGenTypeCache unions SizeTy with
  IntPtrTy, which is built from the target's widest pointer; this part's far
  pointers are twice the width of its near ones, so the allocation size came
  out an i32 and operator new takes an i16.  The fix is a second field,
  LangSizeTy, that is the language's size_t, used by the new, delete and array
  cookie machinery - the places where the value is a size_t to the language
  rather than merely an integer wide enough for a pointer.  It also shrinks the
  array cookie from four bytes to the two a size_t actually is, and stops it
  claiming four byte alignment on a part whose new returns two.

  The first attempt at that was to redefine SizeTy itself, on the theory that
  every one of its two hundred uses means size_t and that no other target has
  the two widths differ.  Forty seven HLSL tests said otherwise: DXIL's
  pointers are thirty two bits and its size_t is sixty four, so SizeTy is
  doing the opposite job there and every memcpy length changed width.  That is
  what a full check-clang is for; the narrow fix above is what survived it.

  A __atomic_compare_exchange whose failure order is not a constant built a
  switch whose cases were i32 and whose value was the language's int.  That is
  broken IR on any target with a sixteen bit int: three lines of C were enough
  to break MSP430 and AVR the same way, and the same one line fix covers all
  three.

  char32_t was unsigned int, which is sixteen bits here, so it could not hold a
  code point.  The standard says char32_t is uint_least32_t; AVR, the other
  target with a sixteen bit int, sets it to unsigned long, and so does this one
  now.  MSP430 still has this one - the same libc source is rejected there for
  the same reason - and nothing here changes that target.

Running the same sweep over all of libc/src rather than those four directories
found a fourth, which nothing in the four had reached:

  template <typename T, size_t N> f(T (&)[N]) asserted in the front end for
  any array, so every libc source that passes a buffer as a cpp::span failed -
  thirteen of them: err/report.cpp and all twelve of stdio's sprintf family.
  An array bound is stored at
  the width of the widest pointer and deduced as a size_t, and building the
  deduced argument at the bound's width with size_t's type is a malformed
  IntegerLiteral.  That one is not libc's at all: three lines of C++ reach it,
  and it is the reason to sweep everything rather than the part being measured.

What is left is mostly not about this target.  Seventeen of the twenty five
sources still failing in the four directories fail on an #error inside libc
itself - locale_t, __atexithandler_t and mbstate_t are declared unavailable in
overlay mode - and four more assume a width: memcmp bit_casts an int32_t to an
int, and wcrtomb static_asserts that wchar_t is four bytes.  Those four fail on
MSP430 too.  Two want internals that only exist in a full build, the startup
object's app and a Mutex.  That leaves two that are genuinely this target's:
mkstemp wants an <fcntl.h> for a part with no filesystem, and l64a wants
thread_local on a target whose clang sets TLSSupported to false.

What it costs
-------------

The numbers below are what run.sh prints, for an XC164CM-8F.  They are a more
honest baseline than the small programs next door, because these functions are
the size real ones are:

  numbers   -O0  text  17764  states 5718697
  numbers   -O1  text   7936  states 2148596
  numbers   -O2  text   8580  states 2025272
  numbers   -Os  text   6156  states 2044420
  parsing   -O0  text  18280  states 27560289
  parsing   -O1  text   9492  states 14776301
  parsing   -O2  text  10632  states 13302784
  parsing   -Os  text  11620  states 14314386
  sorting   -O0  text  25140  states 2761823
  sorting   -O1  text  20060  states  656549
  sorting   -O2  text  21060  states  640318
  sorting   -Os  text  18340  states  652640
  strings   -O0  text   7840  states 1591988
  strings   -O1  text   5416  states  523271
  strings   -O2  text   5296  states  436391
  strings   -Os  text   4220  states  466432

Three things are worth reading off that.  -O2 is bigger than -O1 for three of
the four, and for sorting it is bigger by a kilobyte while saving 2% of the
time; on a part with 48 KByte of near program memory that is a trade worth
knowing about.  sorting at -Os is 18 KByte, which is more than a third of that
memory for one library function - real code is big here, and a corpus of small
programs will not tell you so.  And parsing is the one program where -Os is
larger than -O2, by a kilobyte, while also being 8% slower: -Os is not a
smaller -O2, and on this target it is worth measuring rather than assuming.

Adding to it
------------

A driver includes what it wants out of libc/src, calls it through the
namespace, and prints enough that a wrong answer cannot hide - a checksum over
a buffer rather than the first element of it.

Use fixed width types.  "long" is thirty two bits on the part and sixty four on
the machine being compared against, and "size_t" is sixteen against sixty four;
a program that overflows one and not the other looks exactly like two compilers
disagreeing.  That is not a hypothetical - it is what the first version of
sorting.cpp did.
