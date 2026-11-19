#!/usr/bin/env python3
"""Reader for the DK "funky!" ANI animation format (see docs/formats/ani.md).

Usage:
  python3 -I tools/ani.py info FILE.ANI
  python3 -I tools/ani.py export FILE.ANI OUT_DIR [--bg PAGE.DIB] [--pal ANY.DIB]
  python3 -I tools/ani.py hash CD_DIR      (compare with: twtw-tool ani-hash)

export writes OUT_DIR/frame_NNN.png (each frame composited over the previous
one, starting from --bg or a black canvas) and OUT_DIR/audio.wav.
The palette is taken from --bg, else --pal, else a grey ramp.
Needs Pillow for export only.
"""
import struct
import sys
import wave

MAGIC = b'funky!'
FILE_HDR = 0x1e
CHUNK_HDR = 0x1c


def parse(d):
    ver, nframes, _z, fps, preroll_s = struct.unpack_from('<5H', d, 0)
    fmt = struct.unpack_from('<HHIIHH', d, 0x0e)   # WAVEFORMAT + wBitsPerSample
    hdr = {'version': ver, 'frames': nframes, 'fps': fps, 'preroll_s': preroll_s,
           'unk_0a': d[0x0a:0x0e].hex(),
           'wave': dict(zip(('tag', 'channels', 'rate', 'byte_rate', 'align', 'bits'), fmt))}
    chunks = []
    o = FILE_HDR
    while o < len(d):
        kind, asz, x0, y0, x1, y1, raw, comp = struct.unpack_from('<HI4HII', d, o)
        if d[o + 22:o + 28] != MAGIC:
            raise ValueError(f'no chunk magic at {o:#x}')
        a = o + CHUNK_HDR
        v = a + asz
        chunks.append({'offset': o, 'kind': kind, 'audio': d[a:v], 'rect': (x0, y0, x1, y1),
                       'raw': raw, 'video': d[v:v + comp]})
        o = v + comp
    hdr['preroll_chunks'] = len(chunks) - nframes
    return hdr, chunks


def unrle(s, n):
    """Byte RLE: b & 0x80 -> copy (b & 0x7f) literal bytes; else repeat next byte b times."""
    out = bytearray()
    i = 0
    while i < len(s) and len(out) < n:
        b = s[i]
        i += 1
        if b & 0x80:
            k = b & 0x7f
            out += s[i:i + k]
            i += k
        else:
            out += s[i:i + 1] * b
            i += 1
    if len(out) < n:
        raise ValueError(f'short frame {len(out)} < {n}')
    return bytes(out[:n])    # a few frames carry one extra byte at the end


def video_frames(hdr, chunks):
    """Yield (frame_index, rect, w, h, pixels or None) in display order."""
    for j, c in enumerate(chunks[hdr['preroll_chunks']:]):
        if not c['video']:
            yield j, None, 0, 0, None       # no change this frame
            continue
        x0, y0, x1, y1 = c['rect']
        w, h = x1 - x0, y1 - y0
        stride = w + (w & 1)
        px = unrle(c['video'], c['raw'])
        if stride * h != c['raw']:
            raise ValueError('raw size mismatch')
        rows = b''.join(px[r * stride:r * stride + w] for r in range(h))
        yield j, c['rect'], w, h, rows


def dib_palette(path):
    d = open(path, 'rb').read()
    hs = struct.unpack_from('<I', d, 14)[0]
    p = d[14 + hs:14 + hs + 1024]
    return [c for i in range(256) for c in (p[4 * i + 2], p[4 * i + 1], p[4 * i])]


def cmd_info(path):
    hdr, chunks = parse(open(path, 'rb').read())
    print({k: v for k, v in hdr.items()})
    for i, c in enumerate(chunks):
        print(f"{i:4d} {c['offset']:#08x} audio={len(c['audio']):5d} rect={c['rect']} "
              f"raw={c['raw']} comp={len(c['video'])}")


def cmd_export(path, out, bg=None, pal=None):
    import os
    from PIL import Image
    hdr, chunks = parse(open(path, 'rb').read())
    os.makedirs(out, exist_ok=True)
    frames = list(video_frames(hdr, chunks))
    if bg:
        can = Image.open(bg).convert('P')
        palette = dib_palette(bg)
    else:
        rects = [f[1] for f in frames if f[1]]
        can = Image.new('P', (max(r[2] for r in rects), max(r[3] for r in rects)), 0)
        palette = dib_palette(pal) if pal else [v for i in range(256) for v in (i, i, i)]
    can.putpalette(palette)
    for j, rect, w, h, px in frames:
        if px:
            can.paste(Image.frombytes('P', (w, h), px), rect[:2])
        can.save(os.path.join(out, f'frame_{j:03d}.png'))
    wf = hdr['wave']
    with wave.open(os.path.join(out, 'audio.wav'), 'wb') as w:
        w.setnchannels(wf['channels'])
        w.setsampwidth(wf['bits'] // 8)
        w.setframerate(wf['rate'])
        w.writeframes(b''.join(c['audio'] for c in chunks))
    print(f"{len(frames)} frames at {hdr['fps']} fps -> {out}")


def fnv(h, b):
    for x in b:
        h = ((h ^ x) * 1099511628211) & 0xFFFFFFFFFFFFFFFF
    return h


def cmd_hash(cd):
    """One line per ANI, same format as twtw-tool ani-hash (frames, fps, preroll, frame hash, audio)."""
    import os
    for dp, dns, fns in sorted(os.walk(cd)):
        dns.sort()
        for fn in sorted(fns):
            if not fn.upper().endswith('.ANI'):
                continue
            rel = os.path.relpath(os.path.join(dp, fn), cd).replace(os.sep, '/').upper()
            try:
                hdr, chunks = parse(open(os.path.join(dp, fn), 'rb').read())
            except ValueError as e:
                print(f'{rel} FAIL {e}')
                continue
            h = 1469598103934665603
            for j, rect, w, hh, px in video_frames(hdr, chunks):
                if not px:
                    h = fnv(h, struct.pack('<H', j & 0xFFFF))
                    continue
                h = fnv(h, struct.pack('<5H', j & 0xFFFF, *rect))
                h = fnv(h, px)
            audio = b''.join(c['audio'] for c in chunks)
            print(f"{rel} {hdr['frames']} {hdr['fps']} {hdr['preroll_chunks']} {h:016x} {len(audio)} "
                  f"{fnv(1469598103934665603, audio):016x}")


if __name__ == '__main__':
    a = sys.argv[1:]
    if a[:1] == ['info']:
        cmd_info(a[1])
    elif a[:1] == ['hash']:
        cmd_hash(a[1])
    elif a[:1] == ['export']:
        opt = dict(zip(a[3::2], a[4::2]))
        cmd_export(a[1], a[2], opt.get('--bg'), opt.get('--pal'))
    else:
        sys.exit(__doc__)
