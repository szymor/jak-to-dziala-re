#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Regression harness: the same scripted screens in the original (Wine) and in the port.

Usage:
  python3 -I tools/regress.py list
  python3 -I tools/regress.py capture-orig [NAME...]   reference shots from the original (slow)
  python3 -I tools/regress.py capture-port [NAME...]   shots from the port (needs build/twtw)
  python3 -I tools/regress.py compare [NAME...]        diff port vs reference, write the report
  python3 -I tools/regress.py run [NAME...]            capture-port + compare

Every scenario starts from a fresh launch, clicks through the title sequence until the Workshop
is on screen (matched against its bitmap, not timed), then runs its steps. Coordinates are the
game's 640x480 client coordinates; the original's window frame offset is added for Wine.

Comparison covers the client area Wine shows (634 x 448). Masked out, because only Wine differs
there: the title strip of every pop-up the scenario declares (Windows draws it), the shadow's
in-between pixels that Wine paints white (the original's PatBlt keeps the image), and any
rectangles a scenario adds (the blinking A-Z lamp, font-rendered text: Wine substitutes the font).
Wine erases what the original leaves alone: the original grows a pop-up by its shadow width
without repainting, so the shadow's light squares and two 4x4 corners keep the picture beneath;
Wine fills them with the window class's white brush.

Everything is written to work/regress/ (shots are derived from the game's pictures and are never
committed): orig/NAME.png, port/NAME.png, diff/NAME.png, report.txt.
"""
import os
import subprocess
import sys
import time

import numpy as np
from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, 'tools'))
import dkmm  # noqa: E402

OUT = os.path.join(ROOT, 'work', 'regress')
CD = os.path.join(ROOT, 'work', 'cd')
ISO = os.path.join(ROOT, 'Jak To Działa.iso')
CLIENT_W, CLIENT_H = 634, 448          # the part of the client area Wine's window shows
FRAME = (3, 29)                        # client origin inside the original's window
PAGE = (94, 4)                         # page panel origin
WORKSHOP = 984
NAV_PANEL = 989
SETTLE = (PAGE[0] + 200, PAGE[1] + 10)   # no hotspot there on the Workshop (from the data)

# Nav buttons (client coordinates of their centres).
NAV = {'warsztat': (47, 39), 'urzadzenia': (47, 111), 'dzialy': (47, 190), 'historia': (47, 267),
       'wynalazcy': (47, 336), 'wroc': (47, 385), 'indeks': (47, 407), 'opcje': (47, 425),
       'pomoc': (47, 441)}


def nav(name, wait=4.0):
    return [('click',) + NAV[name], ('wait', wait)]


# name: (steps, pop-ups on screen [(object, x, y) client position or just object], extra masks
#        [(x, y, w, h) client rect], description)
SCENARIOS = {
    'workshop': ([], [], [], 'the Workshop after the title sequence'),
    'principles': (nav('dzialy'), [], [], 'Działy nauki hub'),
    'timeline': (nav('historia'), [], [], 'Historia hub'),
    'inventors': (nav('wynalazcy'), [], [], 'Wynalazcy hub'),
    'az_dial': (nav('urzadzenia'), [], [(PAGE[0] + 334, PAGE[1] + 350, 18, 8)],
                'A-Z dial (the OK lamp blinks: masked)'),
    'options': (nav('opcje'), [987], [], 'Options pop-up'),
    'stories': (nav('opcje') + [('click', 366, 281), ('wait', 2)], [988], [],
                'mammoth stories pop-up over Options'),
    'battery': (nav('indeks', 3) + [('click', 297, 219), ('wait', 1), ('click', 280, 381), ('wait', 5)],
                [274], [], 'Index → battery page with its zoom pop-up'),
    'glossary_stack': (nav('indeks', 3) + [('click', 297, 219), ('wait', 1), ('click', 280, 381), ('wait', 5),
                                          ('click', 428, 335), ('wait', 2)],
                       [274, 281], [], 'glossary pop-up stacked over the zoom pop-up'),
    'help': (nav('pomoc'), [676], [], 'help contents'),
    'help_page': (nav('pomoc') + [('click', 434, 326), ('wait', 2)], [(674, 676)], [],
                  'help page in the help window'),
    'help_scrolled': (nav('pomoc') + [('click', 434, 326), ('wait', 2), ('click', 592, 373), ('wait', 0.7),
                                      ('click', 592, 373), ('wait', 0.7)],
                      [(674, 676)], [], 'help page scrolled two lines'),
    'index_typed': (nav('indeks', 3) + [('type', 'zeg'), ('wait', 1)], [812], [],
                    'Index after typing "zeg" (text areas masked: font)'),
    'hint_popup': (nav('indeks', 3) + [('type', 'sterowiec'), ('wait', 1), ('key', 'Return'), ('wait', 5),
                                       ('click', PAGE[0] + 160, PAGE[1] + 360), ('wait', 2.5)],
                   [47], [], 'airship page with its POWŁOKA hint pop-up'),
    'wiatr_end': (nav('indeks', 3) + [('type', 'turbina'), ('key', 'space'), ('type', 'w'), ('wait', 1),
                                      ('key', 'Return'), ('wait', 5), ('click', PAGE[0] + 445, PAGE[1] + 221),
                                      ('wait', 14)],
                  [], [], 'wind turbine after its WIATR animation (page repainted at the end)'),
    'dial_letter': (nav('urzadzenia') + [('click', PAGE[0] + 267, PAGE[1] + 289), ('wait', 2)], [],
                    [(PAGE[0] + 334, PAGE[1] + 350, 18, 8)], 'A-Z dial after clicking a letter (needle moved)'),
    'calc_control': (nav('indeks', 3) + [('type', 'kalkulator'), ('wait', 1), ('key', 'Return'), ('wait', 5)],
                     [], [], 'calculator page with its animation control panel'),
}

_dk = None


def dk():
    global _dk
    if _dk is None:
        _dk = dkmm.Dkmm(open(os.path.join(CD, 'DKCODE', 'TWTW.PNG'), 'rb').read())
    return _dk


def obj(i):
    return dkmm.parse_object(dk().object_bytes(i))


def s16(v):
    return v - 65536 if v > 32767 else v


def popup_rect(spec):
    """Client rect (x, y, w, h incl. the 8-px strip) of a declared pop-up. (id, other) = shown at
    the position of 'other' (help pages share the contents' window)."""
    i, at = (spec, spec) if isinstance(spec, int) else spec
    o, p = obj(i), obj(at)
    x, y = PAGE[0] + s16(p['pos'][0]), PAGE[1] + s16(p['pos'][1])
    return x, y, o['size'][0], o['size'][1] + 8


def index_masks():
    """Text areas of the Index screen (edit field and list), from the object's rectangles."""
    o, (x0, y0, _, _) = obj(812), popup_rect(812)
    out = []
    for c in o['children']:
        if c['class'] in (12, 13):
            r = c['rect']
            out.append((x0 + r[0], y0 + 8 + r[1], r[2] - r[0], r[3] - r[1]))
    return out


# ---------------------------------------------------------------------------------- drivers

class Orig:
    tool = os.path.join(ROOT, 'tools', 'orig.sh')

    def sh(self, *a):
        subprocess.run([self.tool, *map(str, a)], check=True, stdout=subprocess.DEVNULL)

    def start(self):
        self.sh('start')

    def stop(self):
        subprocess.run([self.tool, 'stop'], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

    def click(self, x, y):
        self.sh('click', x + FRAME[0], y + FRAME[1])

    def key(self, k):
        self.sh('key', k)

    def shot(self, path):
        self.sh('shot', path)
        a = np.asarray(Image.open(path).convert('RGB'))
        Image.fromarray(a[FRAME[1]:FRAME[1] + CLIENT_H, FRAME[0]:FRAME[0] + CLIENT_W]).save(path)


class Port:
    tool = os.path.join(ROOT, 'tools', 'port.sh')

    def sh(self, *a):
        subprocess.run([self.tool, *map(str, a)], check=True, stdout=subprocess.DEVNULL)

    def start(self):
        self.sh('start')

    def stop(self):
        subprocess.run([self.tool, 'stop'], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

    def click(self, x, y):
        self.sh('click', x, y)

    def key(self, k):
        self.sh('key', k)

    def shot(self, path):
        self.sh('shot', path)
        a = np.asarray(Image.open(path).convert('RGB'))
        Image.fromarray(a[:CLIENT_H, :CLIENT_W]).save(path)


def page_bitmap(i):
    o = obj(i)
    for c in o['children']:
        if c['class'] == 3 and c.get('file'):
            name = dk().string(c['file'])                    # may carry its own folder ("nav\\nav")
            d = dk().string(o['dir']) if o['dir'] else ''
            parts = [p for p in (d + '\\' + name).split('\\') if p]
            path = os.path.join(CD, *parts[:-1], parts[-1] + '.DIB')
            for cand in (path, os.path.join(CD, *[q.upper() for q in parts[:-1]], parts[-1].upper() + '.DIB')):
                if os.path.exists(cand):
                    return np.asarray(Image.open(cand).convert('RGB')).astype(int)
    return None


def on_page(shot, ref):
    """Does the client shot show the page bitmap 'ref' (top band of the page area)?"""
    a = np.asarray(Image.open(shot).convert('RGB')).astype(int)
    band = a[PAGE[1]:PAGE[1] + 60, PAGE[0]:PAGE[0] + 400]
    return (np.abs(band - ref[:60, :400]).sum(2) == 0).mean() > 0.95


def nav_shown(shot, nav):
    """Is the nav panel (object 989) on screen? The credits play full-screen over the Workshop
    picture, so the page alone doesn't tell them apart."""
    a = np.asarray(Image.open(shot).convert('RGB')).astype(int)
    band = a[0:200, 0:94]
    return (np.abs(band - nav[0:200, 0:94]).sum(2) == 0).mean() > 0.95


def to_workshop(d, tmp):
    ref, navbmp = page_bitmap(WORKSHOP), page_bitmap(NAV_PANEL)
    for _ in range(10):
        time.sleep(3)
        d.shot(tmp)
        if on_page(tmp, ref) and nav_shown(tmp, navbmp):
            # The credits (page 1587, ~40 s) play over the same picture. One click on a spot that
            # has no hotspot on the Workshop moves on from the credits (their full-screen hotspot
            # goes to the Workshop) and does nothing on the Workshop itself.
            d.click(*SETTLE)
            time.sleep(2.5)
            return True
        d.click(300, 240)
    return False


def capture(kind, names):
    d = Orig() if kind == 'orig' else Port()
    outdir = os.path.join(OUT, kind)
    os.makedirs(outdir, exist_ok=True)
    tmp = os.path.join(OUT, f'.{kind}-tmp.png')
    for name in names:
        steps = SCENARIOS[name][0]
        d.stop()
        d.start()
        try:
            if not to_workshop(d, tmp):
                print(f'{name}: the Workshop never appeared, skipped')
                continue
            for st in steps:
                if st[0] == 'click':
                    d.click(st[1], st[2])
                elif st[0] == 'wait':
                    time.sleep(st[1])
                elif st[0] == 'key':
                    d.key(st[1])
                elif st[0] == 'type':
                    for ch in st[1]:
                        d.key(ch)
            d.shot(os.path.join(outdir, name + '.png'))
            print(f'{name}: captured')
        finally:
            d.stop()


# ---------------------------------------------------------------------------------- compare

def masks_for(name):
    _, pops, extra, _ = SCENARIOS[name]
    m = np.zeros((CLIENT_H, CLIENT_W), bool)
    shadows = []
    for spec in pops:
        x, y, w, h = popup_rect(spec)
        # The title strip is painted by the program (FUN_1028_d588 / FUN_1028_df40) in system
        # colours: COLOR_WINDOWFRAME for the system box's frame, COLOR_(IN)ACTIVECAPTION inside
        # the caption. Wine's colours differ from Windows 3.1's (which the port uses); the rest
        # of the strip (box face, bar, the caption's black outline) is compared.
        def box(x0, y0, x1, y1):
            m[max(y0, 0):max(y1, 0), max(x0, 0):max(x1, 0)] = True
        box(x, y, x + 12, y + 1)                  # frame: top
        box(x, y, x + 1, y + 9)                   # left
        box(x + 12, y + 1, x + 13, y + 8)         # right
        box(x + 1, y + 8, x + 12, y + 9)          # bottom
        box(x + 13, y + 1, x + w - 1, y + 8)      # caption inside
        shadows.append((x, y, w, h))
    for (x, y, w, h) in extra + (index_masks() if name == 'index_typed' else []):
        m[max(y, 0):y + h, max(x, 0):x + w] = True
    return m, shadows


def compare(names):
    os.makedirs(os.path.join(OUT, 'diff'), exist_ok=True)
    lines, failed = [], 0
    for name in names:
        po, pp = os.path.join(OUT, 'orig', name + '.png'), os.path.join(OUT, 'port', name + '.png')
        if not (os.path.exists(po) and os.path.exists(pp)):
            lines.append(f'{name:16} MISSING ({"no reference" if not os.path.exists(po) else "no port shot"})')
            failed += 1
            continue
        O = np.asarray(Image.open(po).convert('RGB')).astype(int)
        P = np.asarray(Image.open(pp).convert('RGB')).astype(int)
        d = np.abs(O - P).sum(2) > 0
        m, shadows = masks_for(name)
        ys, xs = np.mgrid[0:CLIENT_H, 0:CLIENT_W]
        for (x, y, w, h) in shadows:
            # The original grows the pop-up by the shadow width without repainting (MoveWindow,
            # bRepaint = FALSE; FUN_1028_dc3e), so the shadow's light squares and the two 4x4
            # corners keep the picture underneath. Wine erases them with the class's white brush.
            band = (((xs >= x + w) & (xs < x + w + 4) & (ys >= y + 4) & (ys < y + h + 4)) |
                    ((ys >= y + h) & (ys < y + h + 4) & (xs >= x + 4) & (xs < x + w + 4)))
            corners = (((xs >= x + w) & (xs < x + w + 4) & (ys >= y) & (ys < y + 4)) |
                       ((xs >= x) & (xs < x + 4) & (ys >= y + h) & (ys < y + h + 4)))
            white = (O == 255).all(2)
            m |= (band & (((xs + ys) & 1) == 1) & white) | (corners & white)
        rest = d & ~m
        n = int(rest.sum())
        img = (O * 0.35).astype(np.uint8)
        img[m] = (img[m] * 0.4 + np.array([0, 0, 90])).astype(np.uint8)
        img[rest] = [255, 0, 255]
        Image.fromarray(img).save(os.path.join(OUT, 'diff', name + '.png'))
        if n:
            yy, xx = np.nonzero(rest)
            where = f'bbox x {xx.min()}-{xx.max()} y {yy.min()}-{yy.max()}'
        else:
            where = ''
        status = 'ok' if n == 0 else 'DIFF'
        failed += n != 0
        lines.append(f'{name:16} {status:4} {n:6d} px  masked {int((d & m).sum()):5d}  {where}')
    report = '\n'.join(lines) + f'\n{len(names) - failed}/{len(names)} identical\n'
    open(os.path.join(OUT, 'report.txt'), 'w').write(report)
    print(report, end='')
    return failed


def main(argv):
    if not argv or argv[0] not in ('list', 'capture-orig', 'capture-port', 'compare', 'run'):
        sys.exit(__doc__)
    names = argv[1:] or list(SCENARIOS)
    for n in names:
        if n not in SCENARIOS:
            sys.exit(f'unknown scenario {n}')
    if argv[0] == 'list':
        for n in names:
            print(f'{n:16} {SCENARIOS[n][3]}')
    elif argv[0] == 'capture-orig':
        capture('orig', names)
    elif argv[0] == 'capture-port':
        capture('port', names)
    elif argv[0] == 'compare':
        sys.exit(1 if compare(names) else 0)
    else:
        capture('port', names)
        sys.exit(1 if compare(names) else 0)


if __name__ == '__main__':
    main(sys.argv[1:])
