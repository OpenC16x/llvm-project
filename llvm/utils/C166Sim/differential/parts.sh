#!/bin/sh
# Build and run one program for every part -mmcu= knows, and check each one
# agrees with the host.
#
# What this checks that run.sh does not is the part database and the memory map
# it drives: run.sh links everything for the default part, so a derivative
# whose map is wrong - or whose map the linker script cannot express - would
# not show up there.  A part with no data SRAM has to put its static data
# somewhere else, and a part with two segments of program memory has to keep
# them in separate regions; both are derivatives in the table.
#
# The linker script is the core's rather than the part's, because the shape of
# the memory map is: the one named on the command line is used for an XC16x
# part and the one beside it for a C167.  Passing a board's own script means
# passing one that suits every part in the table, which is a thing a board
# script is not.
#
# Usage: parts.sh <build-bin-dir> <sysroot> <linker-script> [program.c]
set -e
BIN=${1:?usage: parts.sh <build-bin-dir> <sysroot> <linker-script> [program]}
SYSROOT=${2:?}
SCRIPT=${3:?}
HERE=$(dirname "$0")
SRC=${4:-$HERE/bits.c}

TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
STATUS=0
PASSED=0
SKIPPED=0

${CC:-cc} -O2 -w -o "$TMP/host" "$SRC"
"$TMP/host" > "$TMP/expected"

# The parts come from the table itself rather than from the driver's error
# message, so that this does not quietly test nothing if that wording changes.
DEF=$(cd "$HERE/../../../include/llvm/TargetParser" && pwd)/C166TargetParser.def
PARTS=$(sed -n 's/^C166_PART("\([^"]*\)".*/\1/p' "$DEF")
CORES=$(sed -n 's/^C166_PART("[^"]*", *"\([^"]*\)".*/\1/p' "$DEF")
C167_SCRIPT=$(dirname "$SCRIPT")/c167.ld
if [ -z "$PARTS" ]; then
  echo "FAIL no parts found in $DEF"
  exit 1
fi

INDEX=0
for PART in $PARTS; do
  INDEX=$((INDEX + 1))
  CORE=$(echo "$CORES" | sed -n "${INDEX}p")
  PART_SCRIPT=$SCRIPT
  [ "$CORE" = c167 ] && PART_SCRIPT=$C167_SCRIPT

  if ! "$BIN/clang" -target c166 -mmcu="$PART" -Os --sysroot="$SYSROOT" \
      -T "$PART_SCRIPT" "$SRC" -o "$TMP/$PART.elf" 2> "$TMP/$PART.build"; then
    # A program too big for a small derivative is a fact about the part rather
    # than a wrong answer, and is reported and passed over.
    if grep -q "will not fit in region" "$TMP/$PART.build"; then
      echo "SKIP $PART (the program does not fit)"
      SKIPPED=$((SKIPPED + 1))
      continue
    fi
    echo "FAIL $PART (build)"
    cat "$TMP/$PART.build"
    STATUS=1
    continue
  fi

  if ! "$BIN/c166-sim" --max-steps=200000000 "$TMP/$PART.elf" \
      > "$TMP/$PART.got" 2> "$TMP/$PART.run"; then
    echo "FAIL $PART (run)"
    cat "$TMP/$PART.run"
    STATUS=1
    continue
  fi

  if cmp -s "$TMP/expected" "$TMP/$PART.got"; then
    echo "PASS $PART"
    PASSED=$((PASSED + 1))
  else
    echo "FAIL $PART (output)"
    diff "$TMP/expected" "$TMP/$PART.got" | head -20
    STATUS=1
  fi
done

# A skip is a fact about a small part rather than a failure, but it is also
# what a part whose program memory is written down too small looks like, and a
# line of it among sixteen PASSes is easy to miss.  So it is counted.
echo "$PASSED passed, $SKIPPED skipped"
if [ "$PASSED" -eq 0 ]; then
  echo "FAIL nothing was built and run"
  STATUS=1
fi

exit $STATUS
