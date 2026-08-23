#!/bin/sh
# Check that every tool the C166 tests ask for was actually built.
#
# A missing one is otherwise found the slow way: the build succeeds, the tests
# run, and one of them stops with "command not found" after an hour of
# compiling.  This reads the tool names out of the tests themselves, so adding
# a test that needs something new is reported here rather than in CI.
#
# Usage: checktools.sh <build-bin-dir>
set -e
BIN=${1:?usage: checktools.sh <build-bin-dir>}
HERE=$(dirname "$0")
ROOT=$(cd "$HERE/../../../.." && pwd)
STATUS=0

TESTS="llvm/test/CodeGen/C166 llvm/test/MC/C166 llvm/test/DebugInfo/C166 llvm/test/tools/c166-sim"

# Every test file, which is what gets read for the tool names.  The directories
# are listed one at a time on purpose: "$TESTS/*" would attach the glob to the
# last word only and quietly read a quarter of them, which is how the first
# version of this managed to report success while missing what it was written
# to find.
cd "$ROOT"
FILES=""
for DIR in $TESTS; do
  for F in "$DIR"/*; do
    [ -f "$F" ] && FILES="$FILES $F"
  done
done
FILES="$FILES $(git ls-files 'lld/test/ELF/*c166*')"

# The RUN lines name their tools; ld.lld comes from the lld target and lit
# substitutes %c166_sim itself, so those two are spelled the way they are built.
NEEDED=$(
  cat $FILES |
  grep -oE '^[;#] RUN:.*' |
  grep -oE '\b(llvm-[a-z0-9-]+|clang|ld\.lld|llc|opt|not|count|split-file|yaml2obj|obj2yaml|FileCheck|c166-sim)\b' |
  sort -u
)

# ld.lld is what the lld target installs, so it is checked under that name.
[ -x "$BIN/ld.lld" ] || [ ! -x "$BIN/lld" ] || NEEDED=$(echo "$NEEDED" | grep -v '^ld\.lld$')

# What the scripts beside this one run, which the tests do not mention.
NEEDED="$NEEDED clang llvm-ar llvm-ranlib llvm-config llvm-objdump llvm-mc c166-sim"

for TOOL in $NEEDED; do
  if [ ! -x "$BIN/$TOOL" ]; then
    echo "missing: $BIN/$TOOL"
    STATUS=1
  fi
done

if [ "$STATUS" -ne 0 ]; then
  echo
  echo "Those are named by the C166 tests or used by the scripts here, so the"
  echo "build has to produce them.  Add them to the ninja target list in"
  echo ".github/workflows/c166.yml."
  exit 1
fi
echo "every tool the C166 tests need is present"
