// A C166 part is bare metal: the build machine's headers describe the build
// machine, so they have to stay out of the compilation.
// RUN: %clang -### -target c166 -c %s 2>&1 | FileCheck --check-prefix=CC1 %s
// CC1: "-cc1"
// CC1-SAME: "-triple" "c166"
// CC1-SAME: "-nostdsysteminc"

// A 16 bit part cannot spare a register for a frame pointer, and there is no
// unwinder to walk one.
// RUN: %clang -### -target c166 -O2 -c %s 2>&1 | FileCheck --check-prefix=NOFP %s
// NOFP: "-mframe-pointer=none"

// Nothing links C166 objects yet.
// RUN: not %clang -### -target c166 %s 2>&1 | FileCheck --check-prefix=LINK %s
// LINK: error: no linker is available for target 'c166'
