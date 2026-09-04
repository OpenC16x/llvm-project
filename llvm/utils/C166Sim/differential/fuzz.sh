#!/bin/sh
# Generate random programs and check that the C166 and the host agree about
# what each one computes.  This is the differential test with the programs
# written by generate.py instead of by hand, which is what makes it able to
# find something nobody thought to look for.
#
# A seed that disagrees is a reproducer: the program is kept, and regenerating
# it is one command.  Narrow it down with generate.py's --depth, --statements
# and --functions, which all make the program smaller.
#
# Usage: fuzz.sh <build-bin-dir> <sysroot> <linker-script> [count] [first-seed]
set -e
BIN=${1:?usage: fuzz.sh <build-bin-dir> <sysroot> <linker-script> [count] [first]}
SYSROOT=${2:?}
SCRIPT=${3:?}
COUNT=${4:-100}
FIRST=${5:-1}
LEVELS=${LEVELS:-"-O0 -O2"}
HERE=$(dirname "$0")
OUT=${OUT:-$(mktemp -d)}
mkdir -p "$OUT"
STATUS=0
RAN=0
SKIPPED=0

SEED=$FIRST
END=$((FIRST + COUNT))
while [ "$SEED" -lt "$END" ]; do
  SRC="$OUT/seed$SEED.c"
  python3 "$HERE/generate.py" --seed "$SEED" > "$SRC"

  # The host is the reference.  If it will not build, the generator emitted
  # something it should not have, which is worth stopping for.
  if ! ${CC:-cc} -O1 -w -o "$OUT/host" "$SRC" 2> "$OUT/host.err"; then
    echo "FAIL seed $SEED: the generated program does not compile for the host"
    head -5 "$OUT/host.err"
    STATUS=1
    SEED=$((SEED + 1))
    continue
  fi
  "$OUT/host" > "$OUT/expected"

  for OPT in $LEVELS; do
    if ! "$BIN/clang" -target c166 "$OPT" --sysroot="$SYSROOT" -T "$SCRIPT" \
        "$SRC" -o "$OUT/prog.elf" 2> "$OUT/build.err"; then
      if grep -q "will not fit in region" "$OUT/build.err"; then
        SKIPPED=$((SKIPPED + 1))
        continue
      fi
      echo "FAIL seed $SEED $OPT (build)"
      head -5 "$OUT/build.err"
      STATUS=1
      continue
    fi
    RAN=$((RAN + 1))
    if ! "$BIN/c166-sim" "$OUT/prog.elf" > "$OUT/got" 2> "$OUT/run.err"; then
      # Running out of the kilobyte of ABI stack the part has is a fact about
      # the part and not a wrong answer, in the same way as running out of the
      # near ROM above, and it is what a generated program with a 512 byte
      # frame calling one with a 280 byte frame does at -O0.  There is no
      # answer to compare, so the seed is passed over at that level rather
      # than counted as a disagreement.  That seed fits at -O2 and is checked
      # there, which is what a per-level skip keeps.
      if grep -q "ABI stack overflow" "$OUT/run.err"; then
        RAN=$((RAN - 1))
        SKIPPED=$((SKIPPED + 1))
        continue
      fi
      echo "FAIL seed $SEED $OPT (run): $(head -1 "$OUT/run.err")"
      STATUS=1
      continue
    fi
    if ! diff -u "$OUT/expected" "$OUT/got" > "$OUT/diff"; then
      echo "FAIL seed $SEED $OPT: the two disagree"
      head -12 "$OUT/diff"
      echo "  reproduce with: generate.py --seed $SEED"
      STATUS=1
    fi
  done
  SEED=$((SEED + 1))
done

echo "ran $RAN builds over $COUNT seeds, $SKIPPED too big for the part"
[ "$STATUS" -eq 0 ] && echo "no disagreements"
exit $STATUS
