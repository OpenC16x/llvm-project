"""What relaxing a branch or a call would be worth, and what it would cost.

lld/ELF/Arch/C166.cpp says there is no linker relaxation here and why not; this
is where the numbers in that note come from, so that the claim can be checked
rather than believed.  Two questions are asked, at the two ends where the
answer could come from.

--linked asks the linker's question of a linked image.  A JMPA or CALLA is four
bytes and names a 16 bit offset within the segment; a JMPR or CALLR is two
bytes and names a signed 8 bit displacement in words from the instruction after
it.  So one whose target turns out to be within that reach could have been two
bytes shorter, and only the linker knows.  Shrinking one moves everything after
it and brings others into reach, so this iterates to a fixed point rather than
counting a single pass.  CALLR is unconditional, so a conditional CALLA is not
a candidate; JMPR takes every condition JMPA does.

The stake column is what such a scheme would put at risk.  Today the assembler
picks the short form itself for a branch it can measure, which is every branch
inside a section.  A linker that shrinks has to be given a relocation for each
of those instead, and an assembler that leaves a relocation no longer knows the
distance - so it has to emit the long form everywhere and rely on the linker to
take it back.  The short branches already in the image are what that would put
at risk, and the linker's first job under such a scheme would be to win them
back before it won anything at all.

--objects asks the assembler's question of an object file, which needs no
linker and no new relocation: a CALLA whose target is in the same section is a
distance the assembler already knows, so it could be a CALLR the same way a
JMPR that cannot reach is already grown into a JMPA.  Only a target in the same
section counts, because that is all the assembler can measure.

Usage: relax.py --linked|--objects <llvm-objdump> <file>...
"""
import re
import subprocess
import sys

# JMPR and CALLR reach a signed 8 bit count of words from the instruction
# after them, which is 254 bytes forward and 256 back.
REACH_LO, REACH_HI = -128, 127

INSN = re.compile(r"\s*([0-9a-f]+):\s+((?:[0-9a-f]{2} )+)\s*\t(\S+)\s*(.*)")


def run(objdump, path, extra=()):
    return subprocess.run([objdump, "-d", *extra, path], capture_output=True,
                          text=True, check=True).stdout


def disassemble(objdump, path):
    """Every instruction as [address, size, mnemonic, condition, target]."""
    insns = []
    for line in run(objdump, path).splitlines():
        m = INSN.match(line)
        if not m:
            continue
        addr = int(m.group(1), 16)
        size = len(m.group(2).split())
        mnemonic, operands = m.group(3), m.group(4).strip()
        cond = target = None
        if mnemonic in ("jmpa", "calla"):
            parts = [p.strip() for p in operands.split(",")]
            cond = parts[0]
            # The operand is a caddr: an offset in the segment the instruction
            # is in, which is the only segment these can reach.
            target = (addr & 0xFF0000) | (int(parts[1]) & 0xFFFF)
        insns.append([addr, size, mnemonic, cond, target])
    return insns


def relaxable(insns):
    """Which JMPA and CALLA could be short, once everything settles."""
    chosen = set()
    while True:
        # Where each instruction would be with the shrinks decided so far.
        moved, delta = {}, 0
        for i, (addr, _, _, _, _) in enumerate(insns):
            moved[addr] = addr - delta
            if i in chosen:
                delta += 2

        grew = False
        for i, (addr, _, mnemonic, cond, target) in enumerate(insns):
            if i in chosen or target is None:
                continue
            if mnemonic == "calla" and cond != "cc_UC":
                continue  # CALLR has no condition field.
            if target not in moved:
                continue  # Not an instruction in this image.
            distance = moved[target] - (moved[addr] + 2)
            if distance % 2 == 0 and REACH_LO <= distance // 2 <= REACH_HI:
                chosen.add(i)
                grew = True
        if not grew:
            return chosen


def linked(objdump, paths):
    print(f"{'image':24} {'text':>6}  {'long':>5} {'win':>6} {'win%':>6}"
          f"  {'short':>5} {'stake':>6} {'stake%':>6}")
    for path in paths:
        insns = disassemble(objdump, path)
        text = sum(size for _, size, _, _, _ in insns)
        longs = [x for x in insns if x[4] is not None]
        shorts = [x for x in insns if x[2] in ("jmpr", "callr")]
        win, stake = 2 * len(relaxable(insns)), 2 * len(shorts)
        print(f"{path.rsplit('/', 1)[-1]:24} {text:6}  {len(longs):5} {win:6}"
              f" {100.0 * win / text:6.2f}  {len(shorts):5} {stake:6}"
              f" {100.0 * stake / text:6.2f}")


def symbols(objdump, path):
    """Every defined symbol as name -> (section, offset within it)."""
    syms = {}
    for line in run(objdump, path, ["--syms"]).splitlines():
        m = re.match(r"([0-9a-f]{8})\s+(\S+)\s+(\S+)\s+([0-9a-f]{8})\s+(\S+)$",
                     line)
        if m:
            syms[m.group(5)] = (m.group(2), int(m.group(1), 16))
    return syms


def objects(objdump, paths):
    print(f"{'object':40} {'calla':>6} {'same section':>13} {'in reach':>9}")
    totals = [0, 0, 0]
    for path in paths:
        syms = symbols(objdump, path)
        counts = [0, 0, 0]
        section = pending = None
        for line in run(objdump, path, ["-r"]).splitlines():
            m = re.match(r"Disassembly of section (\S+):", line)
            if m:
                section = m.group(1).rstrip(":")
                continue
            m = INSN.match(line)
            if m:
                pending = (int(m.group(1), 16), m.group(3))
                continue
            m = re.match(r"\s*[0-9a-f]+:\s+(R_C166_\S+)\s+(\S+)", line)
            if not m or not pending or pending[1] != "calla":
                continue
            counts[0] += 1
            name, addend = m.group(2), 0
            if "+0x" in name:
                name, tail = name.split("+0x")
                addend = int(tail, 16)
            # A static callee is reduced to its section plus an offset, which
            # has no entry of its own in the symbol table.
            where = syms.get(name) or ((name, 0) if name == section else None)
            if not where or where[0] != section:
                continue
            counts[1] += 1
            distance = where[1] + addend - (pending[0] + 2)
            if distance % 2 == 0 and REACH_LO <= distance // 2 <= REACH_HI:
                counts[2] += 1
        print(f"{path.rsplit('/', 1)[-1]:40} {counts[0]:6} {counts[1]:13}"
              f" {counts[2]:9}")
        totals = [a + b for a, b in zip(totals, counts)]
    print(f"{'total':40} {totals[0]:6} {totals[1]:13} {totals[2]:9}"
          f"  = {2 * totals[2]} bytes")


def main(argv):
    if len(argv) < 4 or argv[1] not in ("--linked", "--objects"):
        sys.exit(__doc__)
    (linked if argv[1] == "--linked" else objects)(argv[2], argv[3:])


if __name__ == "__main__":
    main(sys.argv)
