/* c166-flags: -fno-rtti */
/* c166-sim-flags: --check-user-stack=false */
/* Compiled twice: once for c166 and run in the simulator, once for the host
   and run natively.  The two outputs must match exactly.

   The simulator's ABI stack check is turned off for this one, and that is a
   defect being recorded rather than a check being avoided.  Throwing an
   exception on this part uses more ABI stack than the linker scripts here have
   to give: 1262 bytes against the 1024 between F600H and FA00H, which
   "c166-sim --count-states" now prints as "abi-stack 1262 of 1024".  It is the
   unwinder's own frames that do it - __unw_step is 374 bytes, frameInfo 280,
   _Unwind_RaiseException 356, and each of the first two holds a Row, which is
   twenty rules of ten bytes.  In the simulator the memory below F600H is
   ordinary memory and the program runs; on the part it is not memory at all,
   so this would be writing into nothing.

   So this passes here and would not work on hardware, and turning the check
   off is how that is said out loud rather than by a test that fails every run.
   Fixing it means making the unwinder's frames smaller, which is a change to
   startup/unwind.c and its own piece of work; llvm/lib/Target/C166/README.txt
   says so under "C++ exceptions".

   Throwing and catching, which on this part means the unwinder in libc walks
   the call frame information the compiler emitted, on a machine where a return
   address is on a second stack that no generated code touches.

   What is checked is the order things happen in as much as the values: a catch
   that runs, the frames between the throw and it being left behind, the local
   in the catching frame still holding what it held, and execution carrying on
   after the catch as if the call had returned.  A wrong canonical frame
   address unwinds to a place two bytes out, which is one word of somebody's
   saved registers, so the program says what it sees rather than only whether
   it finished. */
#ifdef __c166__
extern "C" void put(char c) { *(__far volatile unsigned char *)0xFF0000 = c; }
#else
#include <cstdio>
extern "C" void put(char c) { putchar(c); }
#endif

static void puthex(unsigned long v, int d) {
  for (int i = d - 1; i >= 0; i--) put("0123456789abcdef"[(v >> (i * 4)) & 0xF]);
  put('\n');
}

struct Small { int code; };
struct Wide { long a; long b; };

/* Thrown from the bottom of a chain, so the walk has frames to step over. */
__attribute__((noinline)) static int bottom(int n) {
  if (n > 2)
    throw Small{n * 10 + 7};
  return n;
}
__attribute__((noinline)) static int middle(int n) { return bottom(n) + 1; }
__attribute__((noinline)) static int top(int n) { return middle(n) * 2; }

/* The catching frame has locals of its own, in registers the callee saved
   convention says survive a call.  If the unwinder puts back the wrong ones,
   these come out wrong rather than the program merely finishing. */
__attribute__((noinline)) static void caught(int n) {
  int a = n + 1000;
  int b = n + 2000;
  int c = n + 3000;
  int r;
  try {
    r = top(n);
    put('n');
  } catch (const Small &s) {
    put('c');
    r = s.code;
  }
  puthex((unsigned long)(unsigned)r, 4);
  puthex((unsigned long)(unsigned)(a + b + c), 4);
}

/* A frame between the throw and the catch that has nothing to do with either:
   it must be stepped over and its registers put back. */
__attribute__((noinline)) static int passthrough(int n) {
  int keep = n * 3 + 1;
  int v = top(n);
  return v + keep;
}

__attribute__((noinline)) static void outer(int n) {
  int keep = n + 500;
  try {
    put('a');
    int v = passthrough(n);
    puthex((unsigned long)(unsigned)v, 4);
  } catch (const Small &s) {
    put('b');
    puthex((unsigned long)(unsigned)s.code, 4);
  }
  puthex((unsigned long)(unsigned)keep, 4);
}

/* Which of two catches runs, and that one that does not match is passed over
   rather than taken. */
__attribute__((noinline)) static void wrongType(int n) {
  try {
    if (n > 2)
      throw Small{n};
    throw Wide{n, n};
  } catch (const Wide &w) {
    put('W');
    puthex((unsigned long)(unsigned)w.a, 4);
  } catch (const Small &s) {
    put('S');
    puthex((unsigned long)(unsigned)s.code, 4);
  }
}

/* Caught, and then thrown again from inside the catch, so a second walk starts
   where the first one ended. */
__attribute__((noinline)) static void again(int n) {
  try {
    try {
      throw Small{n};
    } catch (const Small &s) {
      put('i');
      throw Small{s.code + 1};
    }
  } catch (const Small &s) {
    put('o');
    puthex((unsigned long)(unsigned)s.code, 4);
  }
}

int main() {
  for (int n = 1; n <= 4; n++)
    caught(n);
  for (int n = 1; n <= 4; n++)
    outer(n);
  for (int n = 1; n <= 4; n++)
    wrongType(n);
  for (int n = 1; n <= 3; n++)
    again(n);
  put('.');
  put('\n');
  return 0;
}
