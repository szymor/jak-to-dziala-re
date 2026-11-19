#!/usr/bin/env python3
"""Inventory of the extracted "Jak to działa" CD.

Usage: python3 -I tools/manifest.py work/cd docs/inventory/manifest.csv

Writes one CSV row per file with basic, format-specific metadata. Only
metadata is written (no asset content), so the CSV is safe to commit.
"""
import csv
import hashlib
import os
import re
import struct
import sys

# SSSS n R [k] [A|S]  e.g. AIRP0M0A.ANI, AIRP2Z.DIB, AIRP0M0S.WAV
NAME_RE = re.compile(r'^([A-Z]{3,4})(\d)([A-Z])(\d)?([AS])?$')


def u16(d, o): return struct.unpack_from('<H', d, o)[0]
def u32(d, o): return struct.unpack_from('<I', d, o)[0]


def bmp_info(d):
    if d[:2] != b'BM' or len(d) < 54:
        return {'kind': 'bmp?'}
    hdr = u32(d, 14)
    w, h = struct.unpack_from('<ii', d, 18)
    bpp, comp = u16(d, 28), u32(d, 30)
    clr_used = u32(d, 46)
    ncol = clr_used or (1 << bpp if bpp <= 8 else 0)
    pal = d[14 + hdr:14 + hdr + ncol * 4]
    return {
        'kind': 'bmp',
        'width': w, 'height': h, 'bpp': bpp,
        'compression': {0: 'none', 1: 'rle8', 2: 'rle4'}.get(comp, str(comp)),
        'colors': ncol,
        'palette_sha1': hashlib.sha1(pal).hexdigest()[:12] if pal else '',
    }


def wav_info(d):
    if d[:4] != b'RIFF' or d[8:12] != b'WAVE':
        return {'kind': 'wav?'}
    o, info = 12, {'kind': 'wav'}
    while o + 8 <= len(d):
        cid, sz = d[o:o + 4], u32(d, o + 4)
        if cid == b'fmt ':
            tag, ch, rate, brate, align, bits = struct.unpack_from('<HHIIHH', d, o + 8)
            info.update(format={1: 'pcm', 2: 'ms-adpcm', 0x11: 'ima-adpcm'}.get(tag, hex(tag)),
                        channels=ch, rate=rate, bits=bits, byte_rate=brate)
        elif cid == b'data':
            info['data_bytes'] = sz
            if info.get('byte_rate'):
                info['seconds'] = round(sz / info['byte_rate'], 2)
        o += 8 + sz + (sz & 1)
    return info


def ani_info(d):
    # Provisional layout (see docs/inventory.md); confirmed fields only.
    if len(d) < 0x3a:
        return {'kind': 'ani?'}
    magic_at = d.find(b'funky!', 0, 256)
    return {
        'kind': 'ani',
        'ani_version': u16(d, 0),
        'frames': u16(d, 2),
        'fps': u16(d, 6),
        'ani_field8': u16(d, 8),
        'rate': u32(d, 0x12),
        'bits': u16(d, 0x1c),
        'audio_bytes_per_frame': u32(d, 0x20),
        'seconds': round(u16(d, 2) / u16(d, 6), 2) if u16(d, 6) else '',
        'magic_at': magic_at,
    }


def szdd_info(d):
    if d[:8] != b'SZDD\x88\xf0\x27\x33':
        return {}
    return {'kind': 'szdd', 'unpacked_bytes': u32(d, 10), 'last_char': chr(d[9]) if d[9] else ''}


def ne_info(d):
    if d[:2] != b'MZ' or len(d) < 0x40:
        return {}
    ne = u32(d, 0x3c)
    sig = d[ne:ne + 2]
    if sig == b'NE':
        return {'kind': 'ne', 'segments': u16(d, ne + 0x1c)}
    if sig == b'PE':
        return {'kind': 'pe'}
    return {'kind': 'mz'}


FIELDS = ['path', 'dir', 'name', 'ext', 'size', 'kind', 'subject', 'index', 'role', 'sub', 'media',
          'width', 'height', 'bpp', 'compression', 'colors', 'palette_sha1',
          'format', 'channels', 'rate', 'bits', 'byte_rate', 'data_bytes', 'seconds',
          'ani_version', 'frames', 'fps', 'ani_field8', 'audio_bytes_per_frame', 'magic_at',
          'segments', 'unpacked_bytes', 'last_char']


def main(root, out):
    rows = []
    for dp, dns, fns in os.walk(root):
        dns.sort()
        for fn in sorted(fns):
            if fn.startswith('.'):
                continue
            p = os.path.join(dp, fn)
            rel = os.path.relpath(p, root).replace(os.sep, '/')
            with open(p, 'rb') as f:
                d = f.read()
            stem, _, ext = fn.rpartition('.')
            if not stem:
                stem, ext = fn, ''
            row = {'path': rel, 'dir': os.path.dirname(rel), 'name': stem, 'ext': ext.upper(),
                   'size': len(d), 'kind': 'other'}
            m = NAME_RE.match(stem.upper())
            if m:
                row.update(subject=m[1], index=m[2], role=m[3], sub=m[4] or '', media=m[5] or '')
            e = ext.upper()
            if e == 'DIB' or d[:2] == b'BM':
                row.update(bmp_info(d))
            elif e == 'WAV':
                row.update(wav_info(d))
            elif e == 'ANI':
                row.update(ani_info(d))
            else:
                row.update(szdd_info(d) or ne_info(d))
                if e == 'PNG' and d[:4] == b'\x00\x04DK':
                    row['kind'] = 'dkmm'
            rows.append(row)
    os.makedirs(os.path.dirname(out) or '.', exist_ok=True)
    with open(out, 'w', newline='', encoding='utf-8') as f:
        w = csv.DictWriter(f, FIELDS, extrasaction='ignore', lineterminator='\n')
        w.writeheader()
        w.writerows(rows)
    print(f'{len(rows)} files -> {out}')


if __name__ == '__main__':
    main(sys.argv[1], sys.argv[2])
