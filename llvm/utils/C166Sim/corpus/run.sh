#!/bin/sh
# Build each program here for the C166 and for the machine running this, run
# both, and check the two produce the same output.  Then report what it cost.
#
# What this adds to differential/run.sh is the code under test.  Those programs
# were written to exercise the backend, so they reach what somebody thought to
# reach; these are drivers over LLVM's own libc, which was written by other
# people to solve a different problem and reaches shapes nobody here chose -
# long functions, helpers calling helpers, tables built on the stack, and the
# word-at-a-time loops libc/src/__support/CPP/simd.h builds out of _BitInt.
#
# The libc sources are compiled for both sides from the same files, so a
# difference is the C166 backend's and not libc's.  They are called through
# their namespace rather than through an alias, so there is no question about
# which implementation ran on the host.
#
# Usage: run.sh <build-bin-dir> <sysroot> <linker-script> [levels]
set -e
BIN=${1:?usage: run.sh <build-bin-dir> <sysroot> <linker-script> [levels]}
SYSROOT=${2:?}
SCRIPT=${3:?}
shift 3
LEVELS=${*:-"-O0 -O1 -O2 -Os"}

HERE=$(cd "$(dirname "$0")" && pwd)
LIBC=$(cd "$HERE/../../../../libc" && pwd)
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
STATUS=0

# LIBC_ERRNO_MODE=6 is LIBC_ERRNO_MODE_SYSTEM_INLINE, which makes libc's
# errno the errno of whichever <errno.h> is in the include path - the sysroot's
# on the part, the host's on the machine being compared against - rather than a
# thread local of libc's own that neither side links.  The name of the constant
# is only defined inside the header that reads it, so the number has to be
# spelled out here.
COMMON="-std=c++17 -fno-exceptions -fno-rtti -nostdinc++ \
        -DLIBC_NAMESPACE=llvmlibc -DLIBC_ERRNO_MODE=6 -I $LIBC -I $LIBC/include"

for SRC in "$HERE"/*.cpp; do
  NAME=$(basename "$SRC" .cpp)

  # Which libc sources this program needs: whatever it includes out of src/
  # and has an implementation file.  Some of what it uses is header only -
  # the templates under src/__support are - and brings nothing to link.
  SOURCES=""
  for H in $(sed -n 's,^#include "\(src/[^"]*\)\.h",\1,p' "$SRC"); do
    [ -f "$LIBC/$H.cpp" ] && SOURCES="$SOURCES $LIBC/$H.cpp"
  done

  # The reference, built with the host's own compiler settings but the same
  # libc sources.
  # shellcheck disable=SC2086
  "$BIN/clang++" -O2 -w $COMMON "$SRC" $SOURCES -o "$TMP/$NAME.host"
  "$TMP/$NAME.host" > "$TMP/$NAME.expected"

  for OPT in $LEVELS; do
    # shellcheck disable=SC2086
    if ! "$BIN/clang++" -target c166 -mmcu=xc164cm-8f "$OPT" -w $COMMON \
        --sysroot="$SYSROOT" -T "$SCRIPT" "$SRC" $SOURCES \
        -o "$TMP/$NAME.elf" 2> "$TMP/$NAME.build"; then
      if grep -q "will not fit in region" "$TMP/$NAME.build"; then
        echo "SKIP $NAME $OPT (does not fit)"
        continue
      fi
      echo "FAIL $NAME $OPT (build)"
      cat "$TMP/$NAME.build"
      STATUS=1
      continue
    fi

    if ! "$BIN/c166-sim" --count-states --max-steps=500000000 \
        "$TMP/$NAME.elf" > "$TMP/$NAME.got" 2> "$TMP/$NAME.run"; then
      echo "FAIL $NAME $OPT (run)"
      tail -3 "$TMP/$NAME.run"
      STATUS=1
      continue
    fi

    if ! cmp -s "$TMP/$NAME.expected" "$TMP/$NAME.got"; then
      echo "FAIL $NAME $OPT (output)"
      diff "$TMP/$NAME.expected" "$TMP/$NAME.got" | head -10
      STATUS=1
      continue
    fi

    TEXT=$("$BIN/llvm-size" "$TMP/$NAME.elf" | awk 'NR==2{print $1}')
    STATES=$(grep -o 'states [0-9]*' "$TMP/$NAME.run" | cut -d' ' -f2)
    printf "PASS %-10s %-4s text %6s  states %s\n" "$NAME" "$OPT" "$TEXT" "$STATES"
  done
done

exit $STATUS
