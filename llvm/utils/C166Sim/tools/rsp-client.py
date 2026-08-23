#!/usr/bin/env python3
"""A GDB remote serial protocol client, for testing c166-sim --gdb.

A debugger is what should be on the other end of that protocol, but no debugger
knows the C166 architecture yet, so this stands in: it speaks the framing and
prints what came back, which is enough to check that the stub answers correctly
and to drive it from a test.

  rsp-client.py -- <simulator and its arguments> < script

The script is one command per line.  A line starting with '#' is a comment and a
blank line is skipped.  Commands:

  send <packet>   send a packet and print the reply
  expect <text>   fail unless the last reply was exactly this
  match <text>    fail unless the last reply contained this
  reg <name>      print one register by name, looked up in the target
                  description the stub serves
  print <text>    echo a line, so that output is readable
"""

import argparse
import subprocess
import sys
import re


class Connection:
    def __init__(self, argv):
        self.proc = subprocess.Popen(
            argv, stdin=subprocess.PIPE, stdout=subprocess.PIPE
        )
        self.regs = None

    def _write(self, data):
        self.proc.stdin.write(data)
        self.proc.stdin.flush()

    def _read_byte(self):
        b = self.proc.stdout.read(1)
        if not b:
            raise EOFError("the simulator closed the connection")
        return b

    def send(self, body):
        payload = body.encode()
        checksum = sum(payload) & 0xFF
        self._write(b"$" + payload + b"#%02x" % checksum)
        # The acknowledgement comes first.
        while True:
            ack = self._read_byte()
            if ack == b"+":
                break
            if ack == b"-":
                raise RuntimeError("the simulator rejected %r" % body)
        return self.recv()

    def recv(self):
        while self._read_byte() != b"$":
            pass
        body = b""
        while True:
            c = self._read_byte()
            if c == b"#":
                break
            body += c
        self._read_byte()
        self._read_byte()
        self._write(b"+")
        return body.decode()

    def target_xml(self):
        """The target description, fetched in whatever chunks it comes in."""
        xml = ""
        offset = 0
        while True:
            reply = self.send("qXfer:features:read:target.xml:%x,%x" % (offset, 0x400))
            if not reply:
                raise RuntimeError("the simulator has no target description")
            xml += reply[1:]
            offset += len(reply) - 1
            if reply[0] == "l":
                return xml

    def registers(self):
        """Name to (offset in the g packet, width in bits), from that
        description: the order it lists them in is the order the packet holds
        them, which is the protocol's rule rather than a guess."""
        if self.regs is None:
            self.regs = {}
            offset = 0
            for name, bits in re.findall(
                r'<reg name="([^"]+)" bitsize="(\d+)"', self.target_xml()
            ):
                self.regs[name] = (offset, int(bits))
                offset += int(bits) // 4
        return self.regs

    def read_register(self, name):
        where = self.registers().get(name)
        if where is None:
            raise RuntimeError("no register called %r" % name)
        offset, bits = where
        digits = self.send("g")[offset : offset + bits // 4]
        # Little endian, so the bytes come back least significant first.
        value = 0
        for i in range(bits // 8):
            value |= int(digits[2 * i : 2 * i + 2], 16) << (8 * i)
        return value, bits


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("command", nargs=argparse.REMAINDER)
    args = parser.parse_args()
    argv = args.command
    if argv and argv[0] == "--":
        argv = argv[1:]
    if not argv:
        sys.exit("usage: rsp-client.py -- <simulator and its arguments>")

    conn = Connection(argv)
    last = None
    status = 0

    for line in sys.stdin:
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        verb, _, rest = line.partition(" ")
        if verb == "send":
            last = conn.send(rest)
            print("%s -> %s" % (rest, last))
        elif verb == "expect":
            if last != rest:
                print("FAIL: expected %r, got %r" % (rest, last))
                status = 1
        elif verb == "match":
            if last is None or rest not in last:
                print("FAIL: %r is not in %r" % (rest, last))
                status = 1
        elif verb == "reg":
            value, bits = conn.read_register(rest)
            last = "%0*x" % (bits // 4, value)
            print("%s = %s" % (rest, last))
        elif verb == "print":
            print(rest)
        else:
            sys.exit("unknown command %r" % verb)

    try:
        conn.send("k")
    except (EOFError, RuntimeError, BrokenPipeError):
        pass
    conn.proc.stdin.close()
    conn.proc.wait()
    sys.exit(status)


if __name__ == "__main__":
    main()
