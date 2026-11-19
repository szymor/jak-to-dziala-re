#!/usr/bin/env python3
"""Compare the port's DIB decoder (twtw-tool dump-dibs output) with Pillow.

Usage: python3 -I tools/check_dibs.py work/cd work/dibdump
Each OUTDIR/<path with '/'->'_'>.idx holds "W H\\n", W*H indices (top-down) and a
768-byte RGB palette. Exits non-zero on any mismatch.
"""
import os
import sys

from PIL import Image


def main(cd, dump):
    ok = bad = 0
    for dp, _, fns in os.walk(cd):
        for fn in sorted(fns):
            if not fn.upper().endswith('.DIB'):
                continue
            rel = os.path.relpath(os.path.join(dp, fn), cd).replace(os.sep, '/').upper()
            idx = os.path.join(dump, rel.replace('/', '_') + '.idx')
            im = Image.open(os.path.join(dp, fn))
            if im.mode != 'P':
                print('SKIP (not 8-bit)', rel)
                continue
            try:
                raw = open(idx, 'rb').read()
            except OSError:
                print('MISSING', rel)
                bad += 1
                continue
            nl = raw.index(b'\n')
            w, h = map(int, raw[:nl].split())
            px, pal = raw[nl + 1:nl + 1 + w * h], raw[nl + 1 + w * h:]
            ppal = bytes(im.getpalette()[:768]).ljust(768, b'\0')
            if (w, h) != im.size or px != im.tobytes() or pal[:768] != ppal:
                print('MISMATCH', rel, (w, h), im.size, px == im.tobytes(), pal[:768] == ppal)
                bad += 1
            else:
                ok += 1
    print(f'{ok} bitmaps identical to Pillow, {bad} differ')
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1], sys.argv[2]))
