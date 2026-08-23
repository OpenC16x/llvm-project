#!/bin/sh
# Compile each program here twice - once for the C166 and once for the machine
# running this - and check that the two produce the same output.  What that
# tests is everything at once: the backend's code, the compiler-rt builtins,
# the linker, crt0, and the simulator itself.  A disagreement means one of them
# is wrong, and the program says which area it was in.
#
# The C166 side is built at every optimisation level, because the answer must
# not depend on one: a program that agrees with the host at -O2 and not at -O0
# has found something just as surely as one that never agrees.  The host side
# is the reference and is built once.
#
# It is not a lit test because it needs a C166 crt0 and a C166 compiler-rt,
# neither of which the LLVM build produces.  See
# llvm/lib/Target/C166/startup/README.txt for how to build them.
#
# Usage: run.sh <build-bin-dir> <sysroot> <linker-script> [optimisation levels]
set -e
BIN=${1:?usage: run.sh <build-bin-dir> <sysroot> <linker-script> [levels]}
SYSROOT=${2:?}
SCRIPT=${3:?}
shift 3
LEVELS=${*:-"-O0 -O1 -O2 -Os"}
HERE=$(dirname "$0")
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
STATUS=0

for SRC in "$HERE"/*.c; do
  NAME=$(basename "$SRC" .c)

  ${CC:-cc} -O2 -w -o "$TMP/$NAME.host" "$SRC"
  "$TMP/$NAME.host" > "$TMP/$NAME.expected"

  for OPT in $LEVELS; do
    if ! "$BIN/clang" -target c166 "$OPT" --sysroot="$SYSROOT" -T "$SCRIPT" \
        "$SRC" -o "$TMP/$NAME.elf" 2> "$TMP/$NAME.build"; then
      # Running out of the 48 KByte a near address can reach is a fact about
      # the part and not a wrong answer, so it is reported and passed over
      # rather than counted as a failure.  A program that uses double
      # precision is most of the way through that region before it starts,
      # since the soft float builtins are about 44 KByte of it, and at the
      # lower optimisation levels there is not enough left.
      if grep -q "will not fit in region" "$TMP/$NAME.build"; then
        echo "SKIP $NAME $OPT (does not fit in the near ROM)"
        continue
      fi
      echo "FAIL $NAME $OPT (build)"
      cat "$TMP/$NAME.build"
      STATUS=1
      continue
    fi
    if ! "$BIN/c166-sim" "$TMP/$NAME.elf" > "$TMP/$NAME.got" 2> "$TMP/$NAME.run"; then
      echo "FAIL $NAME $OPT (run)"
      cat "$TMP/$NAME.run"
      STATUS=1
      continue
    fi
    if diff -u "$TMP/$NAME.expected" "$TMP/$NAME.got" > "$TMP/$NAME.diff"; then
      echo "PASS $NAME $OPT"
    else
      echo "FAIL $NAME $OPT"
      cat "$TMP/$NAME.diff"
      STATUS=1
    fi
  done
done
exit $STATUS
