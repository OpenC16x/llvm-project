// The ABI stack has nothing watching it on this part, so checking it is a
// choice about the code rather than a property of the part.  It is carried as
// a target feature all the same, which is what lets a single function ask for
// it or opt out with __attribute__((target(...))).

// RUN: %clang -### -target c166 -mstack-check -c %s 2>&1 | FileCheck %s
// CHECK: "-target-feature" "+stack-check"

// RUN: %clang -### -target c166 -mno-stack-check -c %s 2>&1 \
// RUN:   | FileCheck %s --check-prefix=NO
// NO: "-target-feature" "-stack-check"

// The last one wins, as with every other feature flag.
// RUN: %clang -### -target c166 -mstack-check -mno-stack-check -c %s 2>&1 \
// RUN:   | FileCheck %s --check-prefix=NO

// And it is off unless asked for: the check is eight bytes in every prologue
// that takes it, and a program that has measured its stack does not need them.
// RUN: %clang -### -target c166 -c %s 2>&1 | FileCheck %s --check-prefix=DEFAULT
// DEFAULT-NOT: "+stack-check"
// DEFAULT-NOT: "-stack-check"
