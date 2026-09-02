// The multiply-accumulate coprocessor is a feature rather than a core, because
// -mcpu=st10 covers parts that have it and parts that do not - ST's own
// programming manual says to consult the device data sheet.  So it has to be
// askable for on top of whichever core was named, and refusable on a core that
// implies it.

// RUN: %clang -### --target=c166 -mcpu=st10 -mmac -c %s 2>&1 \
// RUN:   | FileCheck %s --check-prefix=ON
// ON: "-target-cpu" "st10"
// ON-SAME: "-target-feature" "+mac"

// RUN: %clang -### --target=c166 -mcpu=xc16x -mno-mac -c %s 2>&1 \
// RUN:   | FileCheck %s --check-prefix=OFF
// OFF: "-target-cpu" "xc16x"
// OFF-SAME: "-target-feature" "-mac"

// The last one wins, as it does for every other feature flag.
// RUN: %clang -### --target=c166 -mcpu=st10 -mmac -mno-mac -c %s 2>&1 \
// RUN:   | FileCheck %s --check-prefix=OFF-LAST
// OFF-LAST: "-target-feature" "-mac"
// RUN: %clang -### --target=c166 -mcpu=st10 -mno-mac -mmac -c %s 2>&1 \
// RUN:   | FileCheck %s --check-prefix=ON-LAST
// ON-LAST: "-target-feature" "+mac"

// Neither flag leaves the decision where it was: with -mcpu= and nothing else,
// the core decides and no feature is forced either way.
// RUN: %clang -### --target=c166 -mcpu=st10 -c %s 2>&1 \
// RUN:   | FileCheck %s --check-prefix=NEITHER
// NEITHER-NOT: "mac"

// A part implies its core, and the flag still applies on top of it.
// RUN: %clang -### --target=c166 -mmcu=st10f269 -mmac -c %s 2>&1 \
// RUN:   | FileCheck %s --check-prefix=PART
// PART: "-target-cpu" "st10"
// PART-SAME: "-target-feature" "+mac"

// And it is a C166 flag, so it is not one anywhere else.
// RUN: not %clang -### --target=x86_64-linux-gnu -mmac -c %s 2>&1 \
// RUN:   | FileCheck %s --check-prefix=ELSEWHERE
// ELSEWHERE: error: unsupported option '-mmac' for target
