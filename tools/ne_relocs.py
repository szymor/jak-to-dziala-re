#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""List NE relocation records that point at a given segment:offset (internal references).

Usage: python3 -I tools/ne_relocs.py TWTW.EXE SEG OFFSET
  SEG is the 1-based NE segment number (Ghidra segment 1000 = 1, 1008 = 2, ...),
  OFFSET is hex. Prints the source segment/offset of every fixup and walks its
  chain (additive=0 chains), giving the Ghidra address of each patched location.
"""
import struct
import sys


def main(path, seg, off):
    d = open(path, 'rb').read()
    ne = struct.unpack_from('<I', d, 0x3c)[0]
    nseg = struct.unpack_from('<H', d, ne + 0x1c)[0]
    st = ne + struct.unpack_from('<H', d, ne + 0x22)[0]
    sh = struct.unpack_from('<H', d, ne + 0x32)[0]
    for i in range(nseg):
        o, ln, fl, mn = struct.unpack_from('<4H', d, st + 8 * i)
        if not o or not fl & 0x100:
            continue
        base = o << sh
        size = ln or 0x10000
        data = d[base:base + size]
        r = base + size
        n = struct.unpack_from('<H', d, r)[0]
        for k in range(n):
            atype, rtype, src, a, b = struct.unpack_from('<BBHHH', d, r + 2 + 8 * k)
            if rtype & 3 == 0 and a == seg and b == off:
                locs = []
                p = src
                additive = rtype & 4
                while True:
                    locs.append(p)
                    if additive or p + 2 > len(data):
                        break
                    nxt = struct.unpack_from('<H', data, p)[0]
                    if nxt == 0xFFFF or nxt in locs:
                        break
                    p = nxt
                sel = 0x1000 + 8 * i
                print(f'from segment {i + 1} (Ghidra {sel:04x}), type {atype}:',
                      ' '.join(f'{sel:04x}:{x:04x}' for x in locs))


if __name__ == '__main__':
    main(sys.argv[1], int(sys.argv[2]), int(sys.argv[3], 16))
