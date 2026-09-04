#!/bin/sh
# How a program's conditional branches divide between the two forms, and how
# often each is taken.
#
# The question it was written for is the prediction bit of JMPA and CALLA,
# which this backend leaves at 0 - the manual's "assumed taken".  Setting it
# from the branch probabilities is a small change, and whether it is worth
# making depends on how much of a program's branching goes through an
# instruction that has the bit at all.  The two byte relative JMPR has no room
# for one and is what a conditional branch is selected as; only the ones that
# will not reach are grown into JMPA.  So the answer is a count, and this is
# what counts it.
#
# It is not a pass/fail check and is not wired into CI.  Like coverage.sh next
# door, it exists so that a number in llvm/lib/Target/C166/README.txt can be
# re-derived rather than believed.
#
# Usage: branches.sh <build-bin-dir> <program.elf> [max-steps]
set -e
BIN=${1:?usage: branches.sh <build-bin-dir> <program.elf> [max-steps]}
PROG=${2:?}
STEPS=${3:-400000}

# The trace is one line per instruction, "address<tab>text".  Taken-ness is
# whether the next line's address is the one after this instruction, which is
# why the width matters: JMPA and CALLA are four bytes and JMPR is two.
"$BIN/c166-sim" --trace --max-steps="$STEPS" "$PROG" 2>&1 |
  awk '
    /^[0-9a-f]+[ \t]/ {
      if (prev != "") {
        # $2 of the previous line is the mnemonic and $3 its condition.
        if (pk != "" && pc != "cc_UC") {
          n[pk " " pc]++
          total++
          if (pk == "jmpr") rel++; else abs++
          # strtonum is not in every awk, so parse the hex by hand.
          if (int_of($1) != pa + pw) { t[pk " " pc]++; taken++ }
        }
      }
      pa = int_of($1); prev = $0; pk = ""; pc = ""
      if ($2 == "jmpa" || $2 == "calla" || $2 == "jmpr") {
        pk = $2; pc = $3; sub(/,$/, "", pc)
        pw = ($2 == "jmpr") ? 2 : 4
      }
      next
    }
    function int_of(h,   i, c, v) {
      v = 0
      for (i = 1; i <= length(h); i++) {
        c = index("0123456789abcdef", substr(h, i, 1))
        if (c == 0) return -1
        v = v * 16 + c - 1
      }
      return v
    }
    END {
      if (total == 0) { print "no conditional branches executed"; exit }
      printf "%d conditional branches, %d taken (%.0f%%)\n", total, taken,
             100 * taken / total
      printf "  %d through JMPA or CALLA (%.2f%%), which have the "\
             "prediction bit\n", abs, 100 * abs / total
      printf "  %d through JMPR (%.2f%%), which has no room for one\n", rel,
             100 * rel / total
      for (k in n)
        printf "    %-16s %7d executed, %5.1f%% taken\n", k, n[k],
               100 * (k in t ? t[k] : 0) / n[k]
    }'
