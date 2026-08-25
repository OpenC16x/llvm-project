#!/bin/sh
# Build the things a C166 program has to link against, which the LLVM build
# does not produce: they are code for the target rather than for the machine
# doing the building.
#
#   crt0.o   the reset vector and the startup sequence
#   libc.a   memcpy, memmove, memset and memcmp, and the unwinder and C++
#            ABI that a thrown exception runs on
#   compiler-rt builtins, for what the instruction set does not do itself -
#            32 bit shifts and division, 64 bit arithmetic, and all of
#            floating point, which this part has no unit for
#
# The builtins go in the resource directory, where the driver already looks;
# the other two go in <sysroot>/c166-elf/lib, which is where it looks for them.
#
# Usage: mksysroot.sh <build-dir> <sysroot>
set -e
BUILD=${1:?usage: mksysroot.sh <build-dir> <sysroot>}
SYSROOT=${2:?}
HERE=$(dirname "$0")
STARTUP=$(cd "$HERE/../../../lib/Target/C166/startup" && pwd)

# cmake rejects a relative CMAKE_C_COMPILER or CMAKE_ASM_COMPILER outright -
# it looks the name up in PATH and does not find it - and it configures the
# builtins in a directory of its own, where a relative sysroot would land
# somewhere else again.  Both are given to us as the caller typed them, so
# make them absolute here rather than depending on where we were run from.
mkdir -p "$SYSROOT/c166-elf/lib"
SYSROOT=$(cd "$SYSROOT" && pwd)
BIN=$(cd "$BUILD/bin" && pwd)

"$BIN/clang" -target c166 -c "$STARTUP/crt0.S" -o "$SYSROOT/c166-elf/lib/crt0.o"

TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
"$BIN/clang" -target c166 -O2 -c "$STARTUP/mem.c" -o "$TMP/mem.o"

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
    "$TMP/unwind.o" "$TMP/cxa.o" "$TMP/unwind-asm.o"

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
    -DLLVM_CONFIG_PATH="$BIN/llvm-config" > "$TMP/cmake.log" 2>&1 ||
  { echo "configuring compiler-rt failed:"; tail -20 "$TMP/cmake.log"; exit 1; }

ninja -C "$TMP/crt" install > "$TMP/build.log" 2>&1 ||
  { echo "building compiler-rt failed:"; tail -20 "$TMP/build.log"; exit 1; }

echo "sysroot ready: $SYSROOT"
echo "  crt0.o and libc.a in $SYSROOT/c166-elf/lib"
echo "  builtins in $VERSION"
