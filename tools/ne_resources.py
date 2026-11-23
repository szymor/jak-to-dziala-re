#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Extract the resources of a Win16 NE executable (TWTW.EXE).

Usage: python3 -I tools/ne_resources.py work/cd/DKCODE/TWTW.EXE work/res

Writes into the output directory:
  BITMAP_<id>.bmp        bitmaps with a BITMAPFILEHEADER prepended
  CURSOR_<id>.cur        cursors as single-image .cur files (hotspot kept)
  ICON_<id>.ico          icons as single-image .ico files
  STRING.tsv             string tables: id, text (CP1250 decoded)
  DIALOG.txt             dialog templates decoded (Win16 DLGTEMPLATE)
  <TYPE>_<id>.bin        everything else, raw
  index.tsv              type, id, size, flags of every resource
"""
import os
import struct
import sys

TYPES = {1: 'CURSOR', 2: 'BITMAP', 3: 'ICON', 4: 'MENU', 5: 'DIALOG', 6: 'STRING', 7: 'FONTDIR',
         8: 'FONT', 9: 'ACCELERATOR', 10: 'RCDATA', 12: 'GROUP_CURSOR', 14: 'GROUP_ICON',
         16: 'VERSION'}
CTRL = {0x80: 'BUTTON', 0x81: 'EDIT', 0x82: 'STATIC', 0x83: 'LISTBOX', 0x84: 'SCROLLBAR',
        0x85: 'COMBOBOX'}


def resources(d):
    ne = struct.unpack_from('<I', d, 0x3c)[0]
    rt = ne + struct.unpack_from('<H', d, ne + 0x24)[0]
    shift = struct.unpack_from('<H', d, rt)[0]

    def name(v):
        if v & 0x8000:
            return v & 0x7fff
        n = d[rt + v]
        return d[rt + v + 1:rt + v + 1 + n].decode('latin-1')

    o = rt + 2
    while True:
        t = struct.unpack_from('<H', d, o)[0]
        if t == 0:
            break
        count = struct.unpack_from('<H', d, o + 2)[0]
        o += 8
        tname = TYPES.get(t & 0x7fff, str(t & 0x7fff)) if t & 0x8000 else name(t)
        for _ in range(count):
            off, ln, flags, rid = struct.unpack_from('<4H', d, o)
            o += 12
            yield tname, name(rid), flags, d[off << shift:(off << shift) + (ln << shift)]


def cstr(b, i):
    j = b.index(b'\0', i)
    return b[i:j].decode('cp1250'), j + 1


def dialog(b):
    style, n, x, y, cx, cy = struct.unpack_from('<IB4H', b, 0)
    i = 13
    out = []
    menu = ''
    if b[i] == 0xff:
        menu, i = str(struct.unpack_from('<H', b, i + 1)[0]), i + 3
    else:
        menu, i = cstr(b, i)
    cls, i = cstr(b, i)
    title, i = cstr(b, i)
    font = ''
    if style & 0x40:  # DS_SETFONT
        pt = struct.unpack_from('<H', b, i)[0]
        face, i = cstr(b, i + 2)
        font = f' font={pt}pt "{face}"'
    out.append(f'  style={style:#010x} rect=({x},{y},{cx},{cy}) menu={menu!r} class={cls!r} '
               f'title={title!r}{font}')
    for _ in range(n):
        cx_, cy_, w, h, cid, cstyle = struct.unpack_from('<5HI', b, i)
        i += 14
        if b[i] & 0x80:
            ccls, i = CTRL.get(b[i], hex(b[i])), i + 1
        else:
            ccls, i = cstr(b, i)
        if b[i] == 0xff:
            text, i = f'#{struct.unpack_from("<H", b, i + 1)[0]}', i + 3
        else:
            text, i = cstr(b, i)
        extra = b[i]
        i += 1 + extra
        out.append(f'    {ccls:10} id={cid:5} rect=({cx_},{cy_},{w},{h}) style={cstyle:#010x} '
                   f'text={text!r}')
    return '\n'.join(out)


def strings(b, block):
    i = 0
    for k in range(16):
        n = b[i]
        if n:
            yield (block - 1) * 16 + k, b[i + 1:i + 1 + n].decode('cp1250')
        i += 1 + n


def main(exe, out):
    d = open(exe, 'rb').read()
    os.makedirs(out, exist_ok=True)
    idx, strs, dlgs = [], [], []
    for t, rid, flags, b in resources(d):
        idx.append(f'{t}\t{rid}\t{len(b)}\t{flags:#06x}')
        if t == 'BITMAP':
            hs = struct.unpack_from('<I', b, 0)[0]
            bpp = struct.unpack_from('<H', b, 14)[0]
            ncol = struct.unpack_from('<I', b, 32)[0] or (1 << bpp if bpp <= 8 else 0)
            fh = b'BM' + struct.pack('<IHHI', 14 + len(b), 0, 0, 14 + hs + 4 * ncol)
            open(os.path.join(out, f'BITMAP_{rid}.bmp'), 'wb').write(fh + b)
        elif t in ('CURSOR', 'ICON'):
            if t == 'CURSOR':
                hx, hy = struct.unpack_from('<HH', b, 0)
                b = b[4:]
            else:
                hx = hy = 0
            w, h2 = struct.unpack_from('<ii', b, 4)
            bpp = struct.unpack_from('<H', b, 14)[0]
            typ = 2 if t == 'CURSOR' else 1
            entry = struct.pack('<BBBBHHII', w & 0xff, (h2 // 2) & 0xff, 0 if bpp >= 8 else 1 << bpp,
                                0, hx if typ == 2 else 1, hy if typ == 2 else bpp, len(b), 22)
            ext = 'cur' if typ == 2 else 'ico'
            open(os.path.join(out, f'{t}_{rid}.{ext}'), 'wb').write(
                struct.pack('<HHH', 0, typ, 1) + entry + b)
            idx[-1] += f'\thotspot=({hx},{hy}) size={w}x{h2 // 2} bpp={bpp}'
        elif t == 'STRING':
            strs.extend(strings(b, rid))
        elif t == 'DIALOG':
            dlgs.append(f'DIALOG {rid}\n{dialog(b)}')
        else:
            open(os.path.join(out, f'{t}_{rid}.bin'), 'wb').write(b)
    with open(os.path.join(out, 'STRING.tsv'), 'w', encoding='utf-8') as f:
        for k, s in sorted(strs):
            f.write(f'{k}\t{s!r}\n')
    with open(os.path.join(out, 'DIALOG.txt'), 'w', encoding='utf-8') as f:
        f.write('\n\n'.join(dlgs) + '\n')
    with open(os.path.join(out, 'index.tsv'), 'w', encoding='utf-8') as f:
        f.write('type\tid\tsize\tflags\tnotes\n' + '\n'.join(idx) + '\n')
    print(f'{len(idx)} resources, {len(strs)} strings, {len(dlgs)} dialogs -> {out}')


if __name__ == '__main__':
    main(sys.argv[1], sys.argv[2])
