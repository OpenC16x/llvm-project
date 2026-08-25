// An array bound is stored at the width of the target's widest pointer, and
// deduced as a size_t.  Those are not the same width on a target whose widest
// pointer is wider than its default one - C166's far pointers are twice the
// width of its near ones - and building the deduced argument at the bound's
// width with size_t's type asserted in IntegerLiteral.
//
// RUN: %clang_cc1 -triple c166 -std=c++17 -fsyntax-only -verify %s
// RUN: %clang_cc1 -triple x86_64-linux-gnu -std=c++17 -fsyntax-only -verify %s
// RUN: %clang_cc1 -triple msp430 -std=c++17 -fsyntax-only -verify %s
// expected-no-diagnostics

typedef __SIZE_TYPE__ size_t;

template <typename T, size_t N> constexpr size_t count(T (&)[N]) { return N; }

void f() {
  char small[64];
  int wide[7];
  static_assert(count(small) == 64, "");
  static_assert(count(wide) == 7, "");
}

// The deduced value is usable as a bound of its own, which is what the
// substituted expression is for.
template <typename T, size_t N> struct Copy { T storage[N]; };

void g() {
  short in[5];
  Copy<short, count(in)> c;
  static_assert(sizeof(c.storage) == 5 * sizeof(short), "");
}
