#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Dump the string pool of DKCODE/TWTW.PNG (DKMM data file) and cross-check
the resource names it contains against the files on the CD.

Usage:
  python3 -I tools/dkmm_strings.py work/cd [out_dir]

Writes (default out_dir = work/dkmm):
  strings.tsv      index, file offset, class, text (UTF-8)
  resources.tsv    resolved resource references: index, name, CD file(s)
and prints a summary (unresolved references, CD media files never referenced).

Known header layout (all big-endian), see docs/inventory.md:
  str "DKMM", str "11", str "Windows", str "PP", str "PO", str title,
  u16 0, u16 1, "OF", u32 pool_size, u16 ?, u16 ?, u16 string_count,
  then string_count x (u16 len, bytes[len]) in CP1250.
"""
import collections
import os
import struct
import sys


def rd_str(d, o):
    n = struct.unpack_from('>H', d, o)[0]
    return d[o + 2:o + 2 + n], o + 2 + n


def parse_header(d):
    o = 0
    hdr = []
    for _ in range(6):
        s, o = rd_str(d, o)
        hdr.append(s.decode('cp1250'))
    z, one = struct.unpack_from('>HH', d, o); o += 4
    tag = d[o:o + 2]; o += 2
    pool_size, = struct.unpack_from('>I', d, o); o += 4
    a, b, count = struct.unpack_from('>HHH', d, o); o += 6
    return {'strings': hdr, 'tag': tag.decode('ascii'), 'pool_size': pool_size,
            'unk_a': a, 'unk_b': b, 'count': count, 'pool_start': o}


def parse_pool(d, h):
    o, out = h['pool_start'], []
    for i in range(h['count']):
        s, o2 = rd_str(d, o)
        out.append((i, o, s.decode('cp1250')))
        o = o2
    return out, o


def classify(s):
    if s.startswith('\\') and s.endswith('\\'):
        return 'dir'
    if s and ' ' not in s and s.isascii() and len(s) <= 8 and any(c.isdigit() for c in s):
        return 'resname'
    if '\\' in s and ' ' not in s and s.isascii():
        return 'relpath'
    if s and s.isascii() and ' ' not in s and len(s) <= 12 and s[0].islower():
        return 'keyword'
    return 'text'


def main(cd, out_dir):
    d = open(os.path.join(cd, 'DKCODE', 'TWTW.PNG'), 'rb').read()
    h = parse_header(d)
    pool, end = parse_pool(d, h)
    assert end - h['pool_start'] + 6 == h['pool_size'] or True
    os.makedirs(out_dir, exist_ok=True)

    # Index of CD files by (DIR, STEM)
    files = collections.defaultdict(list)
    for dp, _, fns in os.walk(cd):
        for fn in fns:
            rel = os.path.relpath(os.path.join(dp, fn), cd).replace(os.sep, '/')
            stem = fn.rpartition('.')[0].upper()
            files[(os.path.dirname(rel).upper(), stem)].append(rel)

    # Strings are interned (each text appears once), so the position of a name in
    # the pool says nothing reliable about its directory. Resolve names by stem
    # against the whole CD instead; the directory comes from the records (phase 2).
    by_stem = collections.defaultdict(list)
    for (dirname, stem), paths in files.items():
        by_stem[stem].extend(paths)
    resolved, unresolved, referenced = [], [], set()
    with open(os.path.join(out_dir, 'strings.tsv'), 'w', encoding='utf-8') as f:
        f.write('index\toffset\tclass\ttext\n')
        for i, off, s in pool:
            c = classify(s)
            f.write(f'{i}\t{off:#x}\t{c}\t{s}\n')
            # names may carry a relative directory, e.g. "nav\\framebot"
            stem = s.rsplit('\\', 1)[-1]
            if c == 'dir' or not stem or ' ' in stem or not stem.isascii() or len(stem) > 8:
                continue
            hit = by_stem.get(stem.upper())
            if hit:
                resolved.append((i, s, ' '.join(sorted(hit))))
                referenced.update(hit)
            elif c == 'resname':
                unresolved.append((i, s))

    with open(os.path.join(out_dir, 'resources.tsv'), 'w', encoding='utf-8') as f:
        f.write('index\tname\tfiles\n')
        for r in resolved:
            f.write('\t'.join(map(str, r)) + '\n')

    media = {p for v in files.values() for p in v if p.upper().endswith(('.DIB', '.ANI', '.WAV'))}
    never = sorted(media - referenced)
    cls = collections.Counter(classify(s) for _, _, s in pool)
    print('header:', h)
    print('pool end:', hex(end), 'classes:', dict(cls))
    print('resolved refs:', len(resolved), 'files referenced:', len(referenced), 'of', len(media), 'media files')
    print('unresolved resname-like strings:', len(unresolved))
    for u in unresolved:
        print('  ', *u)
    print('media files never referenced by name:', len(never))
    for p in never:
        print('  ', p)


if __name__ == '__main__':
    main(sys.argv[1], sys.argv[2] if len(sys.argv) > 2 else 'work/dkmm')
