#!/bin/sh
# What the corpus costs, checked against a recorded baseline.
#
# Every size and speed claim this backend has made was measured by hand at the
# time it was made - 44 bytes on the EXTend merge, 750 to 430 on the block
# functions, 52 to 32 on the dot product - and nothing checked that any of them
# still held.  A change that quietly gave one back would not have been noticed.
# This is what notices.
#
# What it measures is the corpus rather than the differential programs: those
# were written to exercise the backend and are small and shaped by hand, while
# these are drivers over LLVM's own libc, which is real code with no idea this
# target exists.  Both numbers are recorded, because several of the decisions
# in this backend were size against speed rather than one or the other, and a
# table with only one column would have hidden the trade.
#
# Nothing here compiles for the host, which is what run.sh beside it does: the
# answers being right is that script's job and this one would only repeat it.
# Programs are run, though, because the state count comes from running them and
# because a program that stopped early would otherwise report a flattering
# number.
#
# Usage: sizes.sh <build-bin-dir> <sysroot> <linker-script> [--update]
#
# Without --update it compares against baseline.txt beside this script and
# fails on a regression; with it, it rewrites that file.  The convention is to
# update the baseline in the same commit as the change that moves it, so that
# the diff shows the trade rather than leaving it in a log nobody reads.
set -e
BIN=${1:?usage: sizes.sh <build-bin-dir> <sysroot> <linker-script> [--update]}
SYSROOT=${2:?}
SCRIPT=${3:?}
UPDATE=${4:-}

HERE=$(cd "$(dirname "$0")" && pwd)
LIBC=$(cd "$HERE/../../../../libc" && pwd)
BASELINE="$HERE/baseline.txt"
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT

# The growth a row may show before this fails, in per cent of the baseline.
#
# The measurements themselves have no noise at all - the same compiler over the
# same sources produces the same bytes every time - so nothing here needs a
# tolerance to absorb variation.  What it absorbs is the libc under the corpus
# moving underneath us when upstream is merged, which is a change in what is
# being measured rather than in the thing doing the measuring.  A backend
# change worth catching moves these by far more than one per cent; the two
# whose commits quote the largest numbers moved them by twenty and forty.
TOLERANCE=1

# A row that got smaller is not a failure, but it does mean the baseline is
# stale, and a baseline that is only ever updated when something regresses
# would let a later regression back to the old value pass unnoticed.  So an
# improvement is printed as loudly as a regression and the run still succeeds.
LEVELS="-O0 -O1 -O2 -Os"

# The same flags run.sh uses, less the ones that are only about the host side.
# LIBC_ERRNO_MODE=6 is LIBC_ERRNO_MODE_SYSTEM_INLINE, whose name is only
# defined inside the header that reads it.
COMMON="-std=c++17 -fno-exceptions -fno-rtti -nostdinc++ \
        -DLIBC_NAMESPACE=llvmlibc -DLIBC_ERRNO_MODE=6 -I $LIBC -I $LIBC/include"

for SRC in "$HERE"/*.cpp; do
  NAME=$(basename "$SRC" .cpp)

  SOURCES=""
  for H in $(sed -n 's,^#include "\(src/[^"]*\)\.h",\1,p' "$SRC"); do
    [ -f "$LIBC/$H.cpp" ] && SOURCES="$SOURCES $LIBC/$H.cpp"
  done

  for OPT in $LEVELS; do
    # shellcheck disable=SC2086
    if ! "$BIN/clang++" -target c166 -mmcu=xc164cm-8f "$OPT" -w $COMMON \
        --sysroot="$SYSROOT" -T "$SCRIPT" "$SRC" $SOURCES \
        -o "$TMP/prog.elf" 2> "$TMP/build"; then
      # Running out of the 48 KByte a near address reaches is a fact about the
      # part rather than a measurement, and it is the same fact run.sh reports.
      if grep -q "will not fit in region" "$TMP/build"; then
        echo "skip $NAME $OPT (does not fit in the near ROM)"
        continue
      fi
      echo "FAIL $NAME $OPT (build)"
      cat "$TMP/build"
      exit 1
    fi

    if ! "$BIN/c166-sim" --count-states --max-steps=500000000 \
        "$TMP/prog.elf" > /dev/null 2> "$TMP/run"; then
      echo "FAIL $NAME $OPT (run)"
      tail -3 "$TMP/run"
      exit 1
    fi

    TEXT=$("$BIN/llvm-size" "$TMP/prog.elf" | awk 'NR==2{print $1}')
    STATES=$(grep -o 'states [0-9]*' "$TMP/run" | cut -d' ' -f2)
    echo "$NAME $OPT $TEXT $STATES" >> "$TMP/measured"
  done
done

if [ "$UPDATE" = "--update" ]; then
  {
    echo "# What the corpus costs: text bytes and states, per program and"
    echo "# optimisation level.  Written by sizes.sh --update; see that script"
    echo "# for what moves these and for how a change to one should be"
    echo "# recorded."
    cat "$TMP/measured"
  } > "$BASELINE"
  echo "baseline written: $(grep -c . "$TMP/measured") rows"
  exit 0
fi

if [ ! -f "$BASELINE" ]; then
  echo "no baseline at $BASELINE; write one with sizes.sh ... --update"
  exit 1
fi

STATUS=0
grep -v '^#' "$BASELINE" | grep . > "$TMP/base" || true

printf '%-10s %-4s %14s %16s\n' program level "text (was)" "states (was)"
while read -r NAME OPT TEXT STATES; do
  WAS=$(awk -v n="$NAME" -v o="$OPT" '$1==n && $2==o {print $3, $4}' "$TMP/base")
  if [ -z "$WAS" ]; then
    printf '%-10s %-4s %6s %7s   NEW - not in the baseline\n' \
        "$NAME" "$OPT" "$TEXT" "$STATES"
    STATUS=1
    continue
  fi
  WASTEXT=${WAS% *}
  WASSTATES=${WAS#* }

  NOTE=""
  for WHAT in text states; do
    if [ "$WHAT" = text ]; then NOW=$TEXT; OLD=$WASTEXT; else NOW=$STATES; OLD=$WASSTATES; fi
    [ "$NOW" = "$OLD" ] && continue
    # Integer arithmetic throughout: 100 * (now - old) against tolerance * old.
    DIFF=$((NOW - OLD))
    if [ "$DIFF" -gt 0 ] && [ $((100 * DIFF)) -gt $((TOLERANCE * OLD)) ]; then
      NOTE="$NOTE  REGRESSED $WHAT by $DIFF"
      STATUS=1
    elif [ "$DIFF" -lt 0 ]; then
      NOTE="$NOTE  improved $WHAT by $((-DIFF))"
    else
      NOTE="$NOTE  $WHAT +$DIFF, inside the tolerance"
    fi
  done

  printf '%-10s %-4s %6s (%6s) %7s (%7s)%s\n' \
      "$NAME" "$OPT" "$TEXT" "$WASTEXT" "$STATES" "$WASSTATES" "$NOTE"
done < "$TMP/measured"

# A row in the baseline that nothing measured is as much of a drift as a row
# that moved: a program removed from the corpus without its rows going too
# leaves a baseline that no longer describes anything.
while read -r NAME OPT REST; do
  [ -n "$REST" ] || continue
  if ! awk -v n="$NAME" -v o="$OPT" '$1==n && $2==o {found=1} END {exit !found}' \
      "$TMP/measured"; then
    echo "$NAME $OPT is in the baseline and was not measured"
    STATUS=1
  fi
done < "$TMP/base"

if [ "$STATUS" != 0 ]; then
  echo
  echo "The baseline and what was measured disagree.  Where that is the point"
  echo "of the change, record it: sizes.sh <build> <sysroot> <script> --update"
  echo "and commit baseline.txt alongside the change that moved it."
fi
exit $STATUS
