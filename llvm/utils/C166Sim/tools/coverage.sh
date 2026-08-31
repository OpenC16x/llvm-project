#!/bin/sh
# Which of the C166 instruction set the assembler covers.
#
# Every mnemonic in the C166 Family Instruction Set Manual is offered to the
# assembler with operands of the right shape.  One it rejects is one nobody can
# write, in inline assembly or anywhere else, and one the disassembler cannot
# name either - so a gap here is a gap in what this toolchain can be used for.
#
# This is not a pass/fail check and is deliberately not wired into CI: filling a
# gap means adding an encoding, and an encoding can only come from the manual.
# What it does is keep the list honest, so that what is missing is known rather
# than discovered by somebody whose code did not assemble.
#
# Usage: coverage.sh <build-bin-dir>
set -e
BIN=${1:?usage: coverage.sh <build-bin-dir>}
BIN=$(cd "$BIN" && pwd)
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT

MISSING=""
COUNT=0
OK=0

try() {
  COUNT=$((COUNT + 1))
  printf '\t%s\n' "$1" > "$TMP/one.s"
  if "$BIN/llvm-mc" -triple=c166 -mcpu=xc16x -filetype=obj "$TMP/one.s" \
       -o /dev/null 2>/dev/null
  then
    OK=$((OK + 1))
  else
    MISSING="$MISSING
  $1"
  fi
}

# Arithmetic and logic, in each addressing mode the manual gives them.
for OP in add addc sub subc cmp and or xor; do
  try "$OP r2, r3"
  try "$OP r2, #3"
  try "$OP r2, 0x1000"
  try "$OP 0x1000, r2"
  try "$OP r2, [r3]"
done
for OP in addb addcb subb subcb cmpb andb orb xorb; do
  try "$OP rl2, rl3"
  try "$OP rl2, #3"
done

# Moves.
try "mov r2, r3";        try "mov r2, #0x1234";  try "mov r2, 0x1000"
try "mov 0x1000, r2";    try "mov r2, [r3]";     try "mov [r2], r3"
try "mov r2, [r3+]";     try "mov [-r2], r3";    try "mov r2, [r3+#4]"
try "movb rl2, rl3";     try "movbs r2, rl3";    try "movbz r2, rl3"

# Multiply and divide.
try "mul r2, r3";  try "mulu r2, r3"
try "div r3";      try "divu r3";  try "divl r3";  try "divlu r3"

# Shifts and rotates.
for OP in shl shr ashr rol ror; do
  try "$OP r2, #3"
  try "$OP r2, r3"
done

# Bit instructions.
try "bclr psw.3";          try "bset psw.3";        try "bmov psw.1, psw.2"
try "bmovn psw.1, psw.2";  try "band psw.1, psw.2"; try "bor psw.1, psw.2"
try "bxor psw.1, psw.2";   try "bcmp psw.1, psw.2"
try "bfldl psw, #1, #2";   try "bfldh psw, #1, #2"
try "jb psw.1, 0x10";      try "jnb psw.1, 0x10"
try "jbc psw.1, 0x10";     try "jnbs psw.1, 0x10"

# Flow.
try "jmpa cc_UC, 0x100";  try "jmpi cc_UC, [r2]";  try "jmpr cc_UC, 0x10"
try "jmps #0, 0x100";     try "calla cc_UC, 0x100"; try "calli cc_UC, [r2]"
try "callr 0x100";        try "calls #0, 0x100";   try "pcall r2, 0x100"
try "ret";  try "reti";  try "rets";  try "retp r2";  try "trap #2"

# Loop and context.
try "cmpd1 r2, #3";  try "cmpd2 r2, #3"
try "cmpi1 r2, #3";  try "cmpi2 r2, #3"
try "scxt r2, #0x100";  try "prior r2, r3"

# Stack.
try "push r2";  try "pop r2"

# Unary.
try "cpl r2";  try "cplb rl2";  try "neg r2";  try "negb rl2"

# System and addressing overrides.
try "nop";    try "srst";   try "idle";   try "pwrdn"
try "diswdt"; try "srvwdt"; try "einit";  try "atomic #2"
try "extr #1";   try "exts r2, #1";  try "extsr r2, #1"
try "extp r2, #1"; try "extpr r2, #1"

echo "the assembler covers $OK of $COUNT instruction forms"
if [ -n "$MISSING" ]; then
  echo
  echo "not covered:$MISSING"
  echo
  echo "Each of those is in the manual and cannot be written today.  Adding one"
  echo "means adding its encoding, which has to come from the manual rather"
  echo "than from the shape of the opcode map."
fi
