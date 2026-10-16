#!/usr/bin/env python3
"""Compare the simulation state dumps written by two machines when a CRC mismatch happens.

Each machine writes <UserData>/Desync_frame<N>_player<P>.crc at the moment the mismatch is
detected (all machines detect it on the same logic frame).  The files are a raw serialization of
every object, the partition manager and the rest of the logic state, interleaved with readable
labels (object template names, "MARKER:..." strings).

This script finds the first byte where the two dumps differ and prints the readable labels just
before it, which tells you which object / subsystem diverged first.

usage: compare_desync_dumps.py Desync_frame1234_player0.crc Desync_frame1234_player1.crc
"""
import re
import sys


def labels_before(data, offset, count=6):
    window = data[max(0, offset - 4096):offset]
    found = re.findall(rb"[\x20-\x7e]{4,}", window)
    return [f.decode("ascii", "replace") for f in found[-count:]]


def main():
    if len(sys.argv) != 3:
        print(__doc__)
        return 2
    a = open(sys.argv[1], "rb").read()
    b = open(sys.argv[2], "rb").read()
    n = min(len(a), len(b))
    first = next((i for i in range(n) if a[i] != b[i]), None)
    if first is None:
        if len(a) == len(b):
            print("Dumps are identical: the divergence is in state that the CRC does not cover,")
            print("or the dumps were taken on different frames.")
            return 0
        first = n
        print(f"Dumps share the first {n} bytes, then one is longer ({len(a)} vs {len(b)} bytes).")
    else:
        print(f"First difference at byte {first} of {n}.")
    print("Last readable labels before the difference (closest last):")
    for label in labels_before(a, first):
        print("   ", label)
    lo = max(0, first - 16)
    print(f"bytes {lo}..{first + 16}:")
    print("   A:", a[lo:first + 16].hex(" "))
    print("   B:", b[lo:first + 16].hex(" "))
    return 1


if __name__ == "__main__":
    sys.exit(main())
