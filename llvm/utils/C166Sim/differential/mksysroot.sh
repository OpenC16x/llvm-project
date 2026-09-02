#!/bin/sh
# Build the things a C166 program has to link against, which the LLVM build
# does not produce: they are code for the target rather than for the machine
# doing the building.
#
#   crt0.o   the reset vector and the startup sequence, and c167-crt0.o
#            which is the same for a part with that core
#   libc.a   memcpy, memmove, memset and memcmp, what the headers promise and
#            nothing else defines (errno, exit, abort and the assert handler),
#            and the unwinder and C++ ABI that a thrown exception runs on
#   headers  what a freestanding part can honestly mean of <errno.h>,
#            <string.h>, <stdlib.h>, <assert.h>, <inttypes.h>, <wchar.h>,
#            <uchar.h>, <locale.h> and <fenv.h>, copied into the include
#            directory the driver searches
#   compiler-rt builtins, for what the instruction set does not do itself -
#            32 bit shifts and division, 64 bit arithmetic, and all of
#            floating point, which this part has no unit for
#
# The builtins go in the resource directory, where the driver already looks;
# the other two go in <sysroot>/c166-elf/lib and the headers in
# <sysroot>/c166-elf/include, which is where it looks for them.
#
# Usage: mksysroot.sh <build-dir> <sysroot>
set -e
BUILD=${1:?usage: mksysroot.sh <build-dir> <sysroot>}
SYSROOT=${2:?}
HERE=$(dirname "$0")
STARTUP=$(cd "$HERE/../../../lib/Target/C166/startup" && pwd)

# The builtins are C and assembly, but compiler-rt's project() names C++ too,
# so cmake probes for a working host C++ compiler.  In a tree configured for
# this target and nothing else there is none - the just-built clang++ has no
# back end for the machine doing the building - and the probe fails on a
# language nothing here compiles.  Hence CMAKE_CXX_COMPILER_WORKS below,
# alongside the two the same reasoning already covered.
#
# cmake rejects a relative CMAKE_C_COMPILER or CMAKE_ASM_COMPILER outright -
# it looks the name up in PATH and does not find it - and it configures the
# builtins in a directory of its own, where a relative sysroot would land
# somewhere else again.  Both are given to us as the caller typed them, so
# make them absolute here rather than depending on where we were run from.
mkdir -p "$SYSROOT/c166-elf/lib" "$SYSROOT/c166-elf/include"
SYSROOT=$(cd "$SYSROOT" && pwd)
BIN=$(cd "$BUILD/bin" && pwd)

cp "$STARTUP"/include/*.h "$SYSROOT/c166-elf/include/"

# crt0.S is the XC164CM's - it programs that part's PLL through that part's
# extended special function registers - so it is assembled for that part.  A
# name from one derivative's map is refused for another, which is the point;
# saying which part this file is for is how it gets past that.
"$BIN/clang" -target c166 -mcpu=xc16x -c "$STARTUP/crt0.S" \
    -o "$SYSROOT/c166-elf/lib/crt0.o"

# The C167's, which the driver picks for a part with that core.  It is the
# same startup less the PLL and less the far copies, because that part has
# neither; see c167-crt0.S.
"$BIN/clang" -target c166 -mcpu=c167 -c "$STARTUP/c167-crt0.S" \
    -o "$SYSROOT/c166-elf/lib/c167-crt0.o"

TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
# -ffunction-sections on these two, because the driver already passes
# --gc-sections and a member of an archive is pulled whole.  Without it a
# program that calls memcpy also carries memmove, memset, memcmp, strlen and
# the three far entry points, which is most of a kilobyte of a part that has
# 48; with it each program carries the block functions it actually calls.
"$BIN/clang" -target c166 -O2 -ffunction-sections -c "$STARTUP/mem.c" \
    -o "$TMP/mem.o"
"$BIN/clang" -target c166 -O2 -ffunction-sections -c "$STARTUP/runtime.c" \
    -o "$TMP/runtime.o"

# The unwinder and the C++ ABI, which are what a thrown exception runs on.
# They are built with unwind tables of their own: an exception is thrown from
# inside __cxa_throw, so the walk has to be able to step out of these before it
# reaches any of the program's frames.
"$BIN/clang" -target c166 -O2 -fasynchronous-unwind-tables \
    -I "$STARTUP" -c "$STARTUP/unwind.c" -o "$TMP/unwind.o"
"$BIN/clang" -target c166 -O2 -fasynchronous-unwind-tables \
    -I "$STARTUP" -c "$STARTUP/cxa.c" -o "$TMP/cxa.o"
"$BIN/clang" -target c166 -c "$STARTUP/unwind-asm.S" -o "$TMP/unwind-asm.o"

"$BIN/llvm-ar" rcs "$SYSROOT/c166-elf/lib/libc.a" "$TMP/mem.o" \
    "$TMP/runtime.o" "$TMP/unwind.o" "$TMP/cxa.o" "$TMP/unwind-asm.o"

# Where the driver expects the builtins, which is version specific.
VERSION=$("$BIN/clang" -print-resource-dir)

cmake -S "$HERE/../../../../compiler-rt/lib/builtins" -B "$TMP/crt" -GNinja \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_C_COMPILER="$BIN/clang" \
    -DCMAKE_ASM_COMPILER="$BIN/clang" \
    -DCMAKE_AR="$BIN/llvm-ar" \
    -DCMAKE_RANLIB="$BIN/llvm-ranlib" \
    -DCMAKE_NM="$BIN/llvm-nm" \
    -DCMAKE_C_COMPILER_TARGET=c166 \
    -DCMAKE_ASM_COMPILER_TARGET=c166 \
    -DCOMPILER_RT_BAREMETAL_BUILD=ON \
    -DCOMPILER_RT_DEFAULT_TARGET_ONLY=ON \
    -DLLVM_ENABLE_PER_TARGET_RUNTIME_DIR=ON \
    -DCOMPILER_RT_INSTALL_PATH="$VERSION" \
    -DCMAKE_C_COMPILER_WORKS=1 \
    -DCMAKE_ASM_COMPILER_WORKS=1 \
    -DCMAKE_CXX_COMPILER_WORKS=1 \
    -DLLVM_CONFIG_PATH="$BIN/llvm-config" > "$TMP/cmake.log" 2>&1 ||
  { echo "configuring compiler-rt failed:"; tail -20 "$TMP/cmake.log"; exit 1; }

ninja -C "$TMP/crt" install > "$TMP/build.log" 2>&1 ||
  { echo "building compiler-rt failed:"; tail -20 "$TMP/build.log"; exit 1; }

echo "sysroot ready: $SYSROOT"
echo "  crt0.o and libc.a in $SYSROOT/c166-elf/lib"
echo "  headers in $SYSROOT/c166-elf/include"
echo "  builtins in $VERSION"
