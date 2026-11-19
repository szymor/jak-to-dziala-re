#!/usr/bin/env python3
"""Reader for DKCODE/TWTW.PNG, the compiled ("scrunged") DKMM page file.
See docs/formats/dkmm.md for the layout. All object records but one (the
Index screen's list box) and the index table decode exactly; the template
section of the directory block is not decoded yet. Undecoded parts are
reported, not guessed.

Usage:
  python3 -I tools/dkmm.py summary work/cd/DKCODE/TWTW.PNG
  python3 -I tools/dkmm.py object  work/cd/DKCODE/TWTW.PNG INDEX [INDEX...]
  python3 -I tools/dkmm.py dump    work/cd/DKCODE/TWTW.PNG OUT.txt
  python3 -I tools/dkmm.py index   work/cd/DKCODE/TWTW.PNG
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

    def parse_index(self, o):
        """Index table: u16 n, n x {u16 strref, u16 strlen, u32 0, u16 page, u16 k, k x u32 popup}."""
        d = self.d
        n = be16(d, o)
        o += 2
        out = []
        for _ in range(n):
            ref, ln, z, page, k = struct.unpack_from('>HHIHH', d, o)
            o += 12
            text = self.string(ref)
            if z or text is None or len(text) != ln:
                return None, o
            out.append({'text': text, 'page': page, 'popups': list(struct.unpack_from(f'>{k}I', d, o))})
            o += 4 * k
        return out, o

    def find_index(self):
        """The index table is the tail of the directory block; the templates before it are
        not decoded yet, so locate the table by requiring an exact parse up to the block end."""
        for o in range(self.dir_off, self.dir_end - 2):
            try:
                idx, end = self.parse_index(o)
            except struct.error:
                continue
            if idx and end == self.dir_end:
                return o, idx
        raise ValueError('index table not found')

    def object_bytes(self, i):
        return self.d[self.obj_base + self.obj_off[i]:self.obj_end[i]]


# ---------------------------------------------------------------- objects

ACTION_WORDS = {1: 3, 2: 2, 3: 3, 4: 7, 7: 10, 12: 1, 13: 2}   # payload words after the type word


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
    h['events'] = parse_events(W)
    return h


def parse_child(W):
    cls = W.get()
    if cls == 10:                              # scrolling bitmap view (help pages)
        t = W.get(45)
        return {'class': 10, 'slots': 2, 'id': t[0], 'scrollbar_rect': t[1:5], 'cursor': t[5], 'name': t[7],
                'scrollbar_bitmaps': t[14:22], 'view_rect': t[31:35], 'file': t[44], 'raw': t}
    sub = W.get()
    c = {'class': cls, 'sub': sub}
    if cls == 6:                               # navigation button: hotspot + button animation
        t = W.get(20)
        c.update(id=sub, rect=t[2:6], cursor=t[6], anim=t[12], raw=t)
    elif cls == 3 and sub == 1:                  # bitmap
        c['unk'] = W.get(4)
        c['file'] = W.get()
    elif cls == 2:                             # animation: hotspot + playback fields
        c.update(parse_hotspot(W))
        if W.get() != 0:
            raise ParseError('hotspot terminator')
        t = W.get(16)
        c['anim'] = {'rect': t[1:5], 'file': t[5], 'cursor': t[6], 'params': t[7:]}
        c['tail'] = [t[0]]
    elif cls in (15, 16):                      # bitmap button (OK / Cancel): hotspot + images
        c.update(parse_hotspot(W))
        if W.get() != 0:
            raise ParseError('hotspot terminator')
        t = W.get({15: 8, 16: 5}[cls])
        c.update(images=t[:2], tail=t[2:])
    elif cls in (3, 4, 5, 7) and (sub == 2 or cls in (3, 4)):   # hotspot variants
        c.update(parse_hotspot(W))           # (sub is 2, or a name id on a few singletons)
        if W.get() != 0:
            raise ParseError('hotspot terminator')
        c['tail'] = W.get({3: 2, 4: 3, 5: 0, 7: 8}[cls])
    else:
        raise ParseError(f'child class {cls} sub {sub} not decoded yet')
    return c


def parse_events(W):
    out = []
    for _ in range(W.get()):
        ev = W.get()
        out.append({'event': ev, 'actions': parse_actions(W, W.get())})
    return out


def parse_trailer(W):
    """nEvents, events, 0, nTable, table, nLinks, links (see doc)."""
    events = parse_events(W)
    if W.get() != 0:
        raise ParseError('trailer: expected 0')
    n = W.get()
    table = {}
    if n:                                      # A-Z dial: targets and letter animations
        targets = [(W.get() << 16) | W.get() for _ in range(n)]
        anims = W.get(n)
        table = {'targets': targets, 'anims': anims, 'extra': W.get()}
    links = [tuple(struct.unpack('>4I', struct.pack('>8H', *W.get(8)))) for _ in range(W.get())]
    if W.p != len(W.w):
        raise ParseError(f'{len(W.w) - W.p} words left after trailer')
    return events, links, table


def parse_object(b):
    W = Words(b)
    otype = W.get()
    if otype == 0:                             # template / frame: short header
        o = {'type': 0, 'name': W.get(), 'pos': (0, 0), 'dir': 0, 'w1': None}
    else:
        h = W.get(8)
        o = {'type': otype, 'w1': h[0], 'pos': (h[1], h[2]), 'dir': h[3], 'w5': h[4], 'w6_8': h[5:8]}
        if h[5] != 1 or h[6] not in (1, 2):
            raise ParseError(f'irregular header {h[5:8]}')
    core = W.get(9)                            # nchildren, 1, 2, id, 0, 0, 0, width, height
    if core[1:3] != [1, 2] or core[4:7] != [0, 0, 0]:
        raise ParseError(f'irregular header core {core}')
    o.update(nchildren=core[0], id=core[3], size=(core[7], core[8]), children=[])
    left = o['nchildren']
    while left > 0:
        c = parse_child(W)
        o['children'].append(c)
        left -= c.get('slots', 1)   # a class-10 view counts its nested content as a child
    if otype == 0:
        buttons = [c for c in o['children'] if c['class'] == 6]
        if W.get(2) != [0, 0]:
            raise ParseError('template trailer')
        ids = W.get(len(buttons))
        targets = [(W.get() << 16) | W.get() for _ in buttons]
        o['events'], o['links'], o['table'] = [], list(zip(ids, targets)), {}
        if W.p != len(W.w):
            raise ParseError(f'{len(W.w) - W.p} words left after template trailer')
    else:
        o['events'], o['links'], o['table'] = parse_trailer(W)
    return o


def fmt_object(dk, i):
    b = dk.object_bytes(i)
    try:
        o = parse_object(b)
    except ParseError as e:
        return f'#{i} offset {dk.obj_base + dk.obj_off[i]:#x} len {len(b)}: not decoded ({e})'
    s = dk.string

    def ev(evl):
        return '; '.join(f"ev{e['event']}: " + ', '.join(f"a{a['type']}{a['args']}" for a in e['actions'])
                         for e in evl)

    lines = [f"#{i} type {o['type']} pos {o['pos']} size {o['size']} id {o['id']} "
             f"dir {s(o['dir'])!r} template {o.get('w1')}"]
    for c in o['children']:
        k = c['class']
        if k == 3 and c.get('sub') == 1:
            lines.append(f"    bitmap {s(c['file'])!r}")
        elif k == 10:
            lines.append(f"    scrollview id {c['id']} view {c['view_rect']} bar {c['scrollbar_rect']} "
                         f"file {s(c['file'])!r}")
        elif k == 6:
            lines.append(f"    navbutton id {c['id']} rect {c['rect']} anim {s(c['anim'])!r}")
        else:
            extra = ''
            if 'anim' in c:
                extra = f" anim {s(c['anim']['file'])!r} at {c['anim']['rect']} params {c['anim']['params']}"
            if 'images' in c:
                extra = f" images {[s(x) for x in c['images']]}"
            lines.append(f"    class{k} hotspot id {c['id']} shape {c['shape']} rect {c['rect']} "
                         f"cursor {s(c['cursor'])!r} [{ev(c['events'])}] tail {c.get('tail')}{extra}")
    if o['events']:
        lines.append(f"    object events [{ev(o['events'])}]")
    if o['links']:
        lines.append(f"    links {o['links']}")
    if o['table']:
        lines.append(f"    dial targets {o['table']['targets']}")
    return '\n'.join(lines)


def main(argv):
    cmd, path = argv[0], argv[1]
    dk = Dkmm(open(path, 'rb').read())
    if cmd == 'index':
        off, idx = dk.find_index()
        print(f'# index table at {off:#x}, {len(idx)} entries: text, page object, popup objects')
        for e in idx:
            print(f"{e['text']}\t{e['page']}\t{' '.join(map(str, e['popups']))}")
        return
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
