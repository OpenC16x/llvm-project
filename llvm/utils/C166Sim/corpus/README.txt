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

What it costs
-------------

The numbers below are what run.sh prints, for an XC164CM-8F.  They are a more
honest baseline than the small programs next door, because these functions are
the size real ones are:

  numbers   -O0  text  17764  states 5718697
  numbers   -O1  text   7936  states 2148596
  numbers   -O2  text   8580  states 2025272
  numbers   -Os  text   6156  states 2044420
  sorting   -O0  text  25140  states 2761823
  sorting   -O1  text  20060  states  656549
  sorting   -O2  text  21060  states  640318
  sorting   -Os  text  18340  states  652640
  strings   -O0  text   7840  states 1591988
  strings   -O1  text   5416  states  523271
  strings   -O2  text   5296  states  436391
  strings   -Os  text   4220  states  466432

Two things are worth reading off that.  -O2 is bigger than -O1 for two of the
three, and for sorting it is bigger by a kilobyte while saving 2% of the time;
on a part with 48 KByte of near program memory that is a trade worth knowing
about.  And sorting at -Os is 18 KByte, which is more than a third of that
memory for one library function - real code is big here, and a corpus of small
programs will not tell you so.

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
