#!/usr/bin/env python3
"""Reader for DKCODE/TWTW.PNG, the compiled ("scrunged") DKMM page file.
See docs/formats/dkmm.md for the layout. Work in progress: the container,
the object table, object headers and the common child classes are decoded;
some rare child classes are not yet (they are reported, not guessed).

Usage:
  python3 -I tools/dkmm.py summary work/cd/DKCODE/TWTW.PNG
  python3 -I tools/dkmm.py object  work/cd/DKCODE/TWTW.PNG INDEX [INDEX...]
  python3 -I tools/dkmm.py dump    work/cd/DKCODE/TWTW.PNG OUT.txt
"""
import collections
import struct
import sys


def be16(d, o):
    return struct.unpack_from('>H', d, o)[0]


def be32(d, o):
    return struct.unpack_from('>I', d, o)[0]


class Dkmm:
    def __init__(self, data):
        self.d = d = data
        o = 0
        self.header = []
        for _ in range(6):
            n = be16(d, o)
            self.header.append(d[o + 2:o + 2 + n].decode('cp1250'))
            o += 2 + n
        o += 4                                     # u16 0, u16 1
        self.tag = d[o:o + 2].decode('ascii')      # "OF"
        o += 2
        pool_size, self.objtab_off = struct.unpack_from('>II', d, o)
        o += 8
        self.pool_off = o                          # u16 count, then strings
        count = be16(d, o)
        p = o + 2
        self.strings = []
        for _ in range(count):
            n = be16(d, p)
            self.strings.append(d[p + 2:p + 2 + n].decode('cp1250'))
            p += 2 + n
        assert p == self.pool_off + pool_size, 'string pool size mismatch'
        self.dir_off, self.dir_end = p, self.objtab_off
        n = be32(d, self.objtab_off)
        self.obj_base = self.objtab_off + 4 + 4 * n
        self.obj_off = list(struct.unpack_from(f'>{n}I', d, self.objtab_off + 4))
        order = sorted(range(n), key=lambda i: self.obj_off[i])
        self.obj_end = {}
        for a, b in zip(order, order[1:] + [None]):
            self.obj_end[a] = self.obj_base + (self.obj_off[b] if b is not None else len(d) - self.obj_base)

    def string(self, ref):
        """String references are 1-based; 0 means no string."""
        return self.strings[ref - 1] if 0 < ref <= len(self.strings) else None

    def object_bytes(self, i):
        return self.d[self.obj_base + self.obj_off[i]:self.obj_end[i]]


# ---------------------------------------------------------------- objects

ACTION_WORDS = {1: 3, 2: 2, 3: 3, 4: 7, 7: 10, 12: 1}   # payload words after the type word


class ParseError(Exception):
    pass


class Words:
    def __init__(self, b):
        self.w = [struct.unpack_from('>H', b, k)[0] for k in range(0, len(b) - 1, 2)]
        self.p = 0

    def get(self, n=None):
        if n is not None:
            return [self.get() for _ in range(n)]
        if self.p >= len(self.w):
            raise ParseError('unexpected end of object')
        v = self.w[self.p]
        self.p += 1
        return v


def parse_actions(W, count):
    out = []
    for _ in range(count):
        t = W.get()
        if t not in ACTION_WORDS:
            raise ParseError(f'unknown action type {t}')
        out.append({'type': t, 'args': W.get(ACTION_WORDS[t])})
    return out


def parse_hotspot(W):
    h = {'id': W.get(), 'shape': W.get()}
    h['rect'] = W.get(4)
    h['cursor'] = W.get()
    h['flags'] = W.get(3)
    h['events'] = []
    for _ in range(W.get()):
        ev = W.get()
        h['events'].append({'event': ev, 'actions': parse_actions(W, W.get())})
    return h


def parse_child(W):
    cls = W.get()
    sub = W.get()
    c = {'class': cls, 'sub': sub}
    if cls == 3 and sub == 1:                  # bitmap
        c['unk'] = W.get(4)
        c['file'] = W.get()
    elif cls == 3 and sub == 2:                # hotspot
        c.update(parse_hotspot(W))
        c['tail'] = W.get(3)
    elif cls == 5 and sub == 2:                # hotspot without region (see doc)
        c.update(parse_hotspot(W))
        c['tail'] = W.get(2)
    elif cls == 7 and sub == 2:                # hotspot with extra media fields
        c.update(parse_hotspot(W))
        c['tail'] = W.get(10)
    else:
        raise ParseError(f'child class {cls} sub {sub} not decoded yet')
    return c


def parse_object(b):
    W = Words(b)
    h = W.get(18)
    o = {'type': h[0], 'w1': h[1], 'pos': (h[2], h[3]), 'dir': h[4], 'w5': h[5],
         'w6_8': h[6:9], 'nchildren': h[9], 'w10_11': h[10:12], 'id': h[12],
         'size': (h[16], h[17]), 'children': []}
    if h[6:9] != [1, 1, 1]:
        raise ParseError('irregular header')
    for _ in range(o['nchildren']):
        o['children'].append(parse_child(W))
    o['trailer'] = W.w[W.p:]
    return o


def fmt_object(dk, i):
    b = dk.object_bytes(i)
    lines = []
    try:
        o = parse_object(b)
    except ParseError as e:
        return f'#{i} offset {dk.obj_base + dk.obj_off[i]:#x} len {len(b)}: not decoded ({e})'
    s = dk.string
    lines.append(f"#{i} type {o['type']} pos {o['pos']} size {o['size']} id {o['id']} "
                 f"dir {s(o['dir'])!r} w1 {o['w1']}")
    for c in o['children']:
        if 'file' in c:
            lines.append(f"    bitmap {s(c['file'])!r} unk {c['unk']}")
        else:
            ev = '; '.join(f"ev{e['event']}: " + ', '.join(f"a{a['type']}{a['args']}" for a in e['actions'])
                           for e in c['events'])
            lines.append(f"    class{c['class']} hotspot id {c['id']} shape {c['shape']} rect {c['rect']} "
                         f"cursor {s(c['cursor'])!r} flags {c['flags']} [{ev}] tail {c['tail']}")
    lines.append(f"    trailer {o['trailer']}")
    return '\n'.join(lines)


def main(argv):
    cmd, path = argv[0], argv[1]
    dk = Dkmm(open(path, 'rb').read())
    if cmd == 'summary':
        print('header', dk.header, 'tag', dk.tag)
        print(f'strings {len(dk.strings)} at {dk.pool_off:#x}; directory block {dk.dir_off:#x}..{dk.dir_end:#x}; '
              f'object table at {dk.objtab_off:#x}, {len(dk.obj_off)} objects, base {dk.obj_base:#x}')
        stats = collections.Counter()
        for i in range(len(dk.obj_off)):
            try:
                parse_object(dk.object_bytes(i))
                stats['decoded'] += 1
            except ParseError as e:
                stats[str(e)] += 1
        for k, v in stats.most_common():
            print(f'  {v:5} {k}')
    elif cmd == 'object':
        for i in argv[2:]:
            print(fmt_object(dk, int(i)))
    elif cmd == 'dump':
        with open(argv[2], 'w', encoding='utf-8') as f:
            for i in range(len(dk.obj_off)):
                f.write(fmt_object(dk, i) + '\n')
    else:
        sys.exit(__doc__)


if __name__ == '__main__':
    main(sys.argv[1:] or ['help', ''])
