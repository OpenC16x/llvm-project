#!/bin/sh
# Check that what the compiler writes assembles back to what it emitted
# directly.  Those two paths go through different code - the printer and the
# parser on one side, the code emitter on the other - and they are only correct
# if they agree, which is what -S and -save-temps quietly depend on.
#
# It is worth having as a standing check rather than a technique someone
# remembers: a global named after a special function register used to compile
# to "mov r2, t2" and assemble back into a load from the T2 timer, with no
# relocation and no diagnostic.  Nothing but this notices that.
#
# Usage: roundtrip.sh <build-bin-dir> [more directories of .ll or .c]
set -e
BIN=${1:?usage: roundtrip.sh <build-bin-dir> [directories]}
shift
HERE=$(dirname "$0")
ROOT=$(cd "$HERE/../../../.." && pwd)
DIRS=${*:-"$ROOT/llvm/test/CodeGen/C166 $HERE"}
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
STATUS=0
TOTAL=0

check() {   # $1 direct object, $2 object by way of assembly, $3 what it was
  "$BIN/llvm-objdump" -d -r "$1" | tail -n +3 > "$TMP/a"
  "$BIN/llvm-objdump" -d -r "$2" | tail -n +3 > "$TMP/b"
  if diff -u "$TMP/a" "$TMP/b" > "$TMP/diff"; then
    return 0
  fi
  echo "FAIL $3: the assembly does not come back as the same bytes"
  head -20 "$TMP/diff"
  STATUS=1
}

for DIR in $DIRS; do
  for SRC in "$DIR"/*.ll; do
    [ -e "$SRC" ] || continue
    NAME=$(basename "$SRC")
    "$BIN/llc" -mtriple=c166 -filetype=obj "$SRC" -o "$TMP/direct.o" 2>/dev/null || continue
    "$BIN/llc" -mtriple=c166 "$SRC" -o "$TMP/asm.s" 2>/dev/null || continue
    TOTAL=$((TOTAL + 1))
    if ! "$BIN/llvm-mc" -triple=c166 -filetype=obj "$TMP/asm.s" -o "$TMP/via.o" 2> "$TMP/err"; then
      echo "FAIL $NAME: the compiler's own output does not assemble"
      head -5 "$TMP/err"
      STATUS=1
      continue
    fi
    check "$TMP/direct.o" "$TMP/via.o" "$NAME"
  done

  for SRC in "$DIR"/*.c; do
    [ -e "$SRC" ] || continue
    NAME=$(basename "$SRC")
    for OPT in -O0 -O1 -O2 -Os; do
      "$BIN/clang" -target c166 "$OPT" -c "$SRC" -o "$TMP/direct.o" 2>/dev/null || continue
      "$BIN/clang" -target c166 "$OPT" -S "$SRC" -o "$TMP/asm.s" 2>/dev/null || continue
      TOTAL=$((TOTAL + 1))
      if ! "$BIN/llvm-mc" -triple=c166 -filetype=obj "$TMP/asm.s" -o "$TMP/via.o" 2> "$TMP/err"; then
        echo "FAIL $NAME $OPT: the compiler's own output does not assemble"
        head -5 "$TMP/err"
        STATUS=1
        continue
      fi
      check "$TMP/direct.o" "$TMP/via.o" "$NAME $OPT"
    done
  done
done

echo "round tripped $TOTAL objects"
[ "$STATUS" -eq 0 ] && echo "all identical"
exit $STATUS
