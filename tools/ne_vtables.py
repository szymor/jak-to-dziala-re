#!/usr/bin/env python3
"""Find C++ virtual tables in a 16-bit MSVC NE executable.

The tables sit in code segments as runs of 4-byte far pointers (offset, segment
selector), the selector being the Ghidra-style segment value (0x1000 + 8*(n-1)).
Usage: python3 -I tools/ne_vtables.py TWTW.EXE [SEG_HEX]   e.g. 1050
"""
import struct
import sys


def main(path, only=None):
    d = open(path, 'rb').read()
    ne = struct.unpack_from('<I', d, 0x3c)[0]
    nseg = struct.unpack_from('<H', d, ne + 0x1c)[0]
    st = ne + struct.unpack_from('<H', d, ne + 0x22)[0]
    sh = struct.unpack_from('<H', d, ne + 0x32)[0]
    segs = []
    for i in range(nseg):
        o, ln, fl, mn = struct.unpack_from('<4H', d, st + 8 * i)
        segs.append((o << sh, ln or 0x10000, fl))
    code_sel = {0x1000 + 8 * i: segs[i][1] for i in range(nseg) if not segs[i][2] & 1}
    for i, (base, size, fl) in enumerate(segs):
        sel = 0x1000 + 8 * i
        if fl & 1 or (only and sel != only):
            continue
        data = bytearray(d[base:base + size])
        # apply internal segment fixups: the file holds relocation chains in the selector words
        if fl & 0x100:
            r = base + size
            for k in range(struct.unpack_from('<H', d, r)[0]):
                atype, rtype, src, a, b = struct.unpack_from('<BBHHH', d, r + 2 + 8 * k)
                if rtype & 3 != 0 or a == 0xFF or atype not in (2, 3):
                    continue
                value = 0x1000 + 8 * (a - 1)
                p, seen = src, set()
                while p + 2 <= len(data) and p not in seen:
                    seen.add(p)
                    nxt = struct.unpack_from('<H', data, p if atype == 2 else p + 2)[0]
                    if atype == 2:
                        struct.pack_into('<H', data, p, value)
                    else:
                        struct.pack_into('<HH', data, p, b, value)
                    if rtype & 4 or nxt == 0xFFFF:
                        break
                    p = nxt
        k = 0
        while k + 4 <= len(data):
            run = []
            j = k
            while j + 4 <= len(data):
                off, s = struct.unpack_from('<HH', data, j)
                if s in code_sel and off < code_sel[s]:
                    run.append((off, s))
                    j += 4
                else:
                    break
            if len(run) >= 4:
                print(f'{sel:04x}:{k:04x} {len(run)} entries: ' + ' '.join(f'{s:04x}:{o:04x}' for o, s in run))
                k = j
            else:
                k += 2


if __name__ == '__main__':
    main(sys.argv[1], int(sys.argv[2], 16) if len(sys.argv) > 2 else None)
