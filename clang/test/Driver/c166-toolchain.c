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

// LLD is the only linker that knows the C166 relocations, and crt0.o goes
// first because the reset vector is in it.
// RUN: %clang -### -target c166 %s -o a.out 2>&1 | FileCheck --check-prefix=LINK %s
// LINK: "{{.*}}ld.lld"
// LINK-SAME: "--gc-sections"
// LINK-SAME: "{{.*}}crt0.o"
// LINK-SAME: "-lc"
// LINK-SAME: "-o" "a.out"

// RUN: %clang -### -target c166 %s -o a.out -nostartfiles -nolibc 2>&1 \
// RUN:     | FileCheck --check-prefix=BARE %s
// BARE: "{{.*}}ld.lld"
// BARE-NOT: crt0.o
// BARE-NOT: "-lc"
