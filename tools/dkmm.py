#!/usr/bin/env python3
"""Reader for DKCODE/TWTW.PNG, the compiled ("scrunged") DKMM page file.
See docs/formats/dkmm.md for the layout. All object records but one (the
Index screen's list box), the template section and the index table decode
exactly; the two base-template headers are kept raw. Undecoded parts are
reported, not guessed.

Usage:
  python3 -I tools/dkmm.py summary work/cd/DKCODE/TWTW.PNG
  python3 -I tools/dkmm.py object  work/cd/DKCODE/TWTW.PNG INDEX [INDEX...]
  python3 -I tools/dkmm.py dump    work/cd/DKCODE/TWTW.PNG OUT.txt
  python3 -I tools/dkmm.py index   work/cd/DKCODE/TWTW.PNG
  python3 -I tools/dkmm.py templates work/cd/DKCODE/TWTW.PNG
  python3 -I tools/dkmm.py canonical work/cd/DKCODE/TWTW.PNG   (compare with: twtw-tool dkmm-dump)
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
            out.append({'ref': ref, 'text': text, 'page': page, 'popups': list(struct.unpack_from(f'>{k}I', d, o))})
            o += 4 * k
        return out, o

    # ---------------------------------------------------- template section

    def _panel(self, o):
        """u16 kind, u16 name, u16 has_obj, [u32 obj], rgb, u16 w, h, x, y."""
        d = self.d
        kind, name, has = struct.unpack_from('>HHH', d, o)
        if kind not in (1, 2, 3) or has not in (0, 1):
            return None
        p = o + 6
        obj = None
        if has:
            obj = be32(d, p)
            p += 4
        rgb = d[p:p + 3].hex()
        w, h, x, y = struct.unpack_from('>4H', d, p + 3)
        if max(w, x) > 640 or max(h, y) > 480:
            return None
        return {'kind': kind, 'name': name, 'object': obj, 'rgb': rgb, 'rect': (x, y, w, h)}, p + 11

    def _panel_list(self, o):
        n = be16(self.d, o)
        if not 2 <= n <= 12:
            return None
        out, p = [], o + 2
        for _ in range(n):
            r = self._panel(p)
            if not r:
                return None
            out.append(r[0])
            p = r[1]
        return out, p

    def _styles(self, o, end):
        d = self.d
        n = be16(d, o)
        p, out = o + 2, []
        for _ in range(n):
            name, ref = struct.unpack_from('>HH', d, p)
            colours = [d[p + 4 + 3 * k:p + 7 + 3 * k].hex() for k in range(4)]
            val = be16(d, p + 16)
            ln = be16(d, p + 26)
            if ln > 40 or any(d[p + 18:p + 26]):
                return None
            face = d[p + 28:p + 28 + ln]
            p += 28 + ln
            if d[p:p + 2] != b'\0\0':
                return None
            p += 2
            out.append({'name': name, 'ref': ref, 'colours': colours, 'value': val,
                        'face': face.decode('cp1250', 'replace')})
        return out if p == end - 2 and d[end - 2:end] == b'\0\0' else None

    def parse_templates(self):
        """Template section = directory block up to the index table. See docs/formats/dkmm.md.
        Base-template headers are kept raw (partly understood); everything else is parsed
        and the section must tile exactly up to the index table."""
        d = self.d
        end, _ = self.find_index()
        ver, w, h, nbase = struct.unpack_from('>4H', d, self.dir_off)
        o = self.dir_off + 8
        bases = []
        for _ in range(nbase):
            p = o
            while not self._panel_list(p):
                p += 1
                if p >= end:
                    raise ValueError('base template panel list not found')
            panels, q = self._panel_list(p)
            bases.append({'kind': be16(d, o), 'name': be16(d, o + 2), 'header_raw': d[o:p].hex(),
                          'panels': panels})
            if be16(d, q) != 0:
                raise ValueError('base template trailer')
            o = q + 2
        derived = []
        while self._styles(o, end) is None:
            kind, name = struct.unpack_from('>HH', d, o)
            t = {'kind': kind, 'name': name}
            p = o + 4
            if kind in (1, 3):
                t['head'] = list(struct.unpack_from('>3H', d, p))
                t['flags'] = list(struct.unpack_from('>HH', d, p + 14)) + [d[p + 30], d[p + 31]] + \
                    list(struct.unpack_from('>HH', d, p + 32))
                n = be16(d, p + 40)
                t['panels'] = list(struct.unpack_from(f'>{n}H', d, p + 42))
                p += 42 + 2 * n
            elif kind == 2:
                t['body_raw'] = d[p:p + 38].hex()
                t['size'] = struct.unpack_from('>HH', d, p + 4)
                p += 38
            else:
                raise ValueError(f'unknown template kind {kind} at {o:#x}')
            t['tail'] = list(struct.unpack_from('>5H', d, p))
            t['rgb'] = d[p + 10:p + 13].hex()
            t['rect'] = struct.unpack_from('>4H', d, p + 13)
            pad = 4 if kind == 2 else 2
            if any(d[p + 21:p + 21 + pad]):
                raise ValueError(f'template padding at {p + 21:#x}')
            o = p + 21 + pad
            derived.append(t)
            if o >= end:
                raise ValueError('template section overran the index table')
        return {'version': ver, 'screen': (w, h), 'bases': bases, 'templates': derived,
                'styles': self._styles(o, end)}

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
    if cls == 10:                              # scroll bar control
        t = W.get(29)
        return {'class': 10, 'id': t[0], 'rect': t[1:5], 'cursor': t[5], 'target': t[7],
                'bitmaps': t[14:22], 'step': t[28], 'raw': t}
    if cls == 9:                               # scrolled bitmap content (help pages)
        t = W.get(15)
        return {'class': 9, 'id': t[0], 'rect': t[1:5], 'file': t[14], 'raw': t}
    if cls == 14:                              # list controller: links list/edit/scroll bar/OK
        t = W.get(22)
        return {'class': 14, 'id': t[0], 'raw': t,
                'links': [t[k] for k in range(14, 21, 2) if t[k - 1] == 1]}
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
    elif cls == 12:                            # text edit field: hotspot + text style
        c.update(parse_hotspot(W))
        if W.get() != 0:
            raise ParseError('hotspot terminator')
        t = W.get(4)
        c.update(tail=t[:3], style=t[3])
    elif cls == 13:                            # list box: hotspot + colours, data source, style
        c.update(parse_hotspot(W))
        if W.get() != 0:
            raise ParseError('hotspot terminator')
        b = struct.pack(f'>{len(W.w) - W.p}H', *W.w[W.p:])
        fg, bg = b[0:3].hex(), b[3:6].hex()
        o = 8
        src = b[o + 1:o + 1 + b[o]].decode('cp1250'); o += 1 + b[o]
        flag = b[o]; o += 1
        face = b[o + 1:o + 1 + b[o]].decode('cp1250'); o += 1 + b[o]
        if o % 2:
            raise ParseError('list box tail not word aligned')
        W.p += o // 2
        c.update(colours=(fg, bg), source=src, flag=flag, face=face, style=W.get())
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
        left -= 1
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
            lines.append(f"    scrollbar id {c['id']} rect {c['rect']} target {c['target']} step {c['step']}")
        elif k == 9:
            lines.append(f"    scrolled content id {c['id']} rect {c['rect']} file {s(c['file'])!r}")
        elif k == 14:
            lines.append(f"    list controller id {c['id']} links {c['links']}")
        elif k == 12:
            lines.append(f"    edit field rect {c['rect']} cursor {s(c['cursor'])!r} style {c['style']:#x}")
        elif k == 13:
            lines.append(f"    list box rect {c['rect']} colours {c['colours']} source {c['source']!r} "
                         f"face {c['face']!r} style {c['style']:#x}")
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


def canonical(dk, w):
    """Plain dump shared with the C port (twtw-tool dkmm-dump); diff the two outputs."""
    def ev(evl):
        for e in evl:
            w(f"e {e['event']} {len(e['actions'])}")
            for a in e['actions']:
                w('a ' + ' '.join(str(x) for x in [a['type']] + list(a['args'])))
    w(f'strings {len(dk.strings)}')
    for i, t in enumerate(dk.strings, 1):
        w(f"s {i} {t.encode('cp1250').hex() or '-'}")
    t = dk.parse_templates()
    w(f"screen {t['screen'][0]} {t['screen'][1]}")
    for b in t['bases']:
        w(f"base {b['kind']} {b['name']} {len(b['header_raw']) // 2} {len(b['panels'])}")
        for p in b['panels']:
            x, y, pw, ph = p['rect']
            w(f"p {p['kind']} {p['name']} {int(p['object'] is not None)} {p['object'] or 0} {p['rgb']} {x} {y} {pw} {ph}")
    for x in t['templates']:
        names = x.get('panels', [])
        w(f"tpl {x['kind']} {x['name']} {x['rgb']} " + ' '.join(map(str, x['rect'])) + f" {len(names)}"
          + ''.join(f' {n}' for n in names))
    for st in t['styles']:
        w(f"style {st['name']} {st['ref']} " + ' '.join(st['colours']) + f" {st['value']} {st['face']}")
    _, idx = dk.find_index()
    w(f'index {len(idx)}')
    for e in idx:
        w(' '.join(str(v) for v in ['i', e['ref'], e['page']] + e['popups']))
    w(f'objects {len(dk.obj_off)}')
    for i in range(len(dk.obj_off)):
        try:
            o = parse_object(dk.object_bytes(i))
        except ParseError as e:
            w(f'o {i} FAIL {e}')
            continue
        p = o.get('w6_8', [0, 0, 0])
        w(' '.join(str(v) for v in ['o', i, o['type'], o['w1'] if o['type'] else o['name'], o['pos'][0], o['pos'][1],
                                    o['dir'], o.get('w5', 0), p[0], p[1], p[2], o['nchildren'], o['id'],
                                    o['size'][0], o['size'][1]]))
        for c in o['children']:
            k = c['class']
            if k == 3 and c.get('sub') == 1:
                w(f"c bitmap {c['file']}")
            elif k in (6, 9, 10, 14):
                w(' '.join(str(v) for v in ['c raw', k] + ([c['id']] if k == 6 else []) + list(c['raw'])))
            else:
                w(' '.join(str(v) for v in ['c hot', k, c['sub'], c['id'], c['shape']] + list(c['rect'])
                           + [c['cursor']] + list(c['flags']) + [len(c['events'])]))
                ev(c['events'])
                tail = c.get('tail', [])
                w(' '.join(str(v) for v in ['c tail', len(tail)] + list(tail)))
                if k == 2:
                    a = c['anim']
                    w(' '.join(str(v) for v in ['c anim'] + list(a['rect']) + [a['file'], a['cursor']] + list(a['params'])))
                elif k in (15, 16):
                    w(f"c img {c['images'][0]} {c['images'][1]}")
                elif k == 12:
                    w(f"c style {c['style']}")
                elif k == 13:
                    w(f"c list {c['colours'][0]} {c['colours'][1]} {c['source']} {c['flag']} {c['face']} {c['style']}")
        if o['type'] == 0:
            for bid, tgt in o['links']:
                w(f'k {bid} {tgt}')
        else:
            w(f"r {len(o['events'])}")
            ev(o['events'])
            if o['table']:
                tb = o['table']
                w(' '.join(str(v) for v in ['t', len(tb['targets'])] + tb['targets'] + list(tb['anims']) + [tb['extra']]))
            for ln in o['links']:
                w('k ' + ' '.join(map(str, ln)))


def main(argv):
    cmd, path = argv[0], argv[1]
    dk = Dkmm(open(path, 'rb').read())
    if cmd == 'canonical':
        canonical(dk, print)
        return
    if cmd == 'templates':
        t = dk.parse_templates()
        print(f"version {t['version']} screen {t['screen']}")
        for b in t['bases']:
            print(f"base template kind {b['kind']} name {b['name']:#x} header {len(b['header_raw']) // 2} bytes")
            for p in b['panels']:
                print(f"    panel kind {p['kind']} name {p['name']:#x} object {p['object']} rgb {p['rgb']} "
                      f"rect(x,y,w,h) {p['rect']}")
        for x in t['templates']:
            print(f"template kind {x['kind']} name {x['name']:#x} panels "
                  f"{[hex(p) for p in x.get('panels', [])]} size {x.get('size')} rgb {x['rgb']}")
        for s_ in t['styles']:
            print(f"style {s_['name']:#x} ref {s_['ref']:#x} colours {s_['colours']} value {s_['value']:#x} "
                  f"face {s_['face']!r}")
        return
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
