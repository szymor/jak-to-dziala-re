#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Unpack the MS COMPRESS (SZDD) files from DKCODE/ (*.??_) for reference.

Only the 5 WinG files are SZDD; the other *.??_ files (VfW 1.1 / OLE2 runtime)
use the KWAJ variant, which 7-Zip cannot read. They are skipped (not needed
for the port).

Usage: python3 -I tools/unpack_szdd.py work/cd/DKCODE work/redist

Decompression is done by 7-Zip. SZDD may store the replaced last character of
the extension in its header; many files on this CD leave it empty, so a
truncated extension is completed from the usual Windows 3.1 extensions.
"""
import os
import shutil
import subprocess
import sys
import tempfile

COMPLETE = {'DL': 'DLL', 'DR': 'DRV', 'WN': 'WND', 'AC': 'ACM', '38': '386', 'EX': 'EXE',
            'HL': 'HLP', 'RE': 'REG', 'TL': 'TLB'}


def main(src, dst):
    os.makedirs(dst, exist_ok=True)
    for fn in sorted(os.listdir(src)):
        p = os.path.join(src, fn)
        with open(p, 'rb') as f:
            hdr = f.read(14)
        if hdr[:8] != b'SZDD\x88\xf0\x27\x33':
            continue
        with tempfile.TemporaryDirectory(dir=dst) as tmp:
            subprocess.run(['7z', 'x', '-y', f'-o{tmp}', p], check=True, stdout=subprocess.DEVNULL)
            (out,) = os.listdir(tmp)
            stem, _, ext = out.upper().rpartition('.')
            if len(ext) < 3:
                ext = (chr(hdr[9]).upper() and ext + chr(hdr[9]).upper()) if hdr[9] else COMPLETE.get(ext, ext)
            real = f'{stem}.{ext}'
            shutil.move(os.path.join(tmp, out), os.path.join(dst, real))
        print(f'{fn:14} -> {real}')


if __name__ == '__main__':
    main(sys.argv[1], sys.argv[2])
