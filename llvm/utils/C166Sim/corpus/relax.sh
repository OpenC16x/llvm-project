#!/bin/sh
# What linker relaxation would be worth over the corpus, and what it would cost.
#
# lld/ELF/Arch/C166.cpp says there is no linker relaxation here and why not.
# The reason is a measurement, and this is the measurement, so that the claim
# can be checked rather than believed.  It builds the same programs sizes.sh
# beside it builds, then reads each linked image: see relax.py for what the two
# numbers are and how they are counted.
#
# Usage: relax.sh <build-bin-dir> <sysroot> <linker-script> [levels]
set -e
BIN=${1:?usage: relax.sh <build-bin-dir> <sysroot> <linker-script> [levels]}
SYSROOT=${2:?}
SCRIPT=${3:?}
LEVELS=${4:--O2 -Os}

HERE=$(cd "$(dirname "$0")" && pwd)
LIBC=$(cd "$HERE/../../../../libc" && pwd)
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT

# The same flags sizes.sh uses; see the comment there for what they are.
COMMON="-std=c++17 -fno-exceptions -fno-rtti -nostdinc++ \
        -DLIBC_NAMESPACE=llvmlibc -DLIBC_ERRNO_MODE=6 -I $LIBC -I $LIBC/include"

IMAGES=""
OBJECTS=""
for SRC in "$HERE"/*.cpp; do
  NAME=$(basename "$SRC" .cpp)

  SOURCES=""
  for H in $(sed -n 's,^#include "\(src/[^"]*\)\.h",\1,p' "$SRC"); do
    [ -f "$LIBC/$H.cpp" ] && SOURCES="$SOURCES $LIBC/$H.cpp"
  done

  for OPT in $LEVELS; do
    # shellcheck disable=SC2086
    if "$BIN/clang++" -target c166 -mmcu=xc164cm-8f "$OPT" -w $COMMON \
        --sysroot="$SYSROOT" -T "$SCRIPT" "$SRC" $SOURCES \
        -o "$TMP/$NAME$OPT.elf" 2> "$TMP/build"; then
      IMAGES="$IMAGES $TMP/$NAME$OPT.elf"
    else
      # The same fact run.sh and sizes.sh report: a near address reaches 48
      # KByte and some of these do not fit in it.
      if grep -q "will not fit in region" "$TMP/build"; then
        echo "skip $NAME $OPT (does not fit in the near ROM)"
        continue
      fi
      echo "FAIL $NAME $OPT (build)"
      cat "$TMP/build"
      exit 1
    fi
  done

  # And the objects on their own, for the question the assembler could answer
  # without any of this: a call whose target is in the same section.  One
  # optimisation level is enough for that one - it is about where the callee
  # ended up, not about how well it was compiled.
  for F in "$SRC" $SOURCES; do
    O="$TMP/o-$NAME-$(basename "$F" .cpp).o"
    # shellcheck disable=SC2086
    "$BIN/clang++" -target c166 -mmcu=xc164cm-8f -Os -w $COMMON \
        --sysroot="$SYSROOT" -c "$F" -o "$O"
    OBJECTS="$OBJECTS $O"
  done
done

echo "What a linker could shrink, and what letting it would put at risk:"
# shellcheck disable=SC2086
python3 "$HERE/relax.py" --linked "$BIN/llvm-objdump" $IMAGES

echo
echo "What the assembler could shrink on its own, with no linker involved:"
# shellcheck disable=SC2086
python3 "$HERE/relax.py" --objects "$BIN/llvm-objdump" $OBJECTS
