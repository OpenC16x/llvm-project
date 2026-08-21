#!/bin/sh
# Compile each program here twice - once for the C166 and once for the machine
# running this - and check that the two produce the same output.  What that
# tests is everything at once: the backend's code, the compiler-rt builtins,
# the linker, crt0, and the simulator itself.  A disagreement means one of them
# is wrong, and the program says which area it was in.
#
# It is not a lit test because it needs a C166 crt0 and a C166 compiler-rt,
# neither of which the LLVM build produces.  See
# llvm/lib/Target/C166/startup/README.txt for how to build them.
#
# Usage: run.sh <build-bin-dir> <sysroot> <linker-script>
set -e
BIN=${1:?usage: run.sh <build-bin-dir> <sysroot> <linker-script>}
SYSROOT=${2:?}
SCRIPT=${3:?}
HERE=$(dirname "$0")
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
STATUS=0

for SRC in "$HERE"/*.c; do
  NAME=$(basename "$SRC" .c)
  "$BIN/clang" -target c166 -O2 --sysroot="$SYSROOT" -T "$SCRIPT" \
      "$SRC" -o "$TMP/$NAME.elf"
  "$BIN/c166-sim" "$TMP/$NAME.elf" > "$TMP/$NAME.c166"
  ${CC:-cc} -O2 -w -o "$TMP/$NAME.host" "$SRC"
  "$TMP/$NAME.host" > "$TMP/$NAME.host.out"
  if diff -u "$TMP/$NAME.host.out" "$TMP/$NAME.c166" > "$TMP/$NAME.diff"; then
    echo "PASS $NAME"
  else
    echo "FAIL $NAME"
    cat "$TMP/$NAME.diff"
    STATUS=1
  fi
done
exit $STATUS
