"""Draws the preset icons that come with the designer (tools/icons/png/<size>/<name>.png).

White pictograms on a transparent background: on a screen they take any color with
"image_recolor" (green running, red fault ...). Drawn on a 24 x 24 grid at a large scale and
reduced, so every size is smooth. Run with Pillow installed:

    python -I tools/icons/gen_icons.py

The PNG files are kept in the repository: building the designer needs no Python.
"""
import math
import os
import sys

from PIL import Image, ImageDraw, ImageFont

SIZES = (24, 32, 48, 64)
S = 480                 # drawing size
U = S / 24              # one unit of the 24 grid
W = 2.0                 # line width in units

OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'png')


class Pen:
    def __init__(self):
        self.img = Image.new('L', (S, S), 0)
        self.d = ImageDraw.Draw(self.img)

    @staticmethod
    def p(x, y):
        return (x * U, y * U)

    def box(self, x0, y0, x1, y1):
        return [x0 * U, y0 * U, x1 * U, y1 * U]

    def line(self, pts, w=W, fill=255):
        px = [self.p(x, y) for x, y in pts]
        self.d.line(px, fill=fill, width=int(w * U), joint='curve')
        r = w * U / 2
        for x, y in px:                                  # round caps
            self.d.ellipse([x - r, y - r, x + r, y + r], fill=fill)

    def circle(self, cx, cy, r, w=W, filled=False, fill=255):
        b = self.box(cx - r, cy - r, cx + r, cy + r)
        if filled:
            self.d.ellipse(b, fill=fill)
        else:
            self.d.ellipse(b, outline=fill, width=int(w * U))

    def rect(self, x0, y0, x1, y1, r=0, w=W, filled=False, fill=255):
        b = self.box(x0, y0, x1, y1)
        if filled:
            self.d.rounded_rectangle(b, radius=r * U, fill=fill)
        else:
            self.d.rounded_rectangle(b, radius=r * U, outline=fill, width=int(w * U))

    def poly(self, pts, filled=True, w=W, fill=255):
        px = [self.p(x, y) for x, y in pts]
        if filled:
            self.d.polygon(px, fill=fill)
        else:
            self.line(pts + [pts[0]], w=w, fill=fill)

    def arc(self, cx, cy, r, a0, a1, w=W, fill=255):
        """Angles in degrees, 0 = right, clockwise (as Pillow)."""
        self.d.arc(self.box(cx - r, cy - r, cx + r, cy + r), a0, a1, fill=fill, width=int(w * U))
        hw = w / 2                                        # round ends
        for a in (a0, a1):
            t = math.radians(a)
            x, y = cx + (r - hw) * math.cos(t), cy + (r - hw) * math.sin(t)
            self.circle(x, y, hw, filled=True, fill=fill)


def at(cx, cy, r, deg):
    t = math.radians(deg)
    return (cx + r * math.cos(t), cy + r * math.sin(t))


ICONS = {}


def icon(name, group):
    def reg(fn):
        ICONS[name] = (group, fn)
        return fn
    return reg


# ---------------------------------------------------------------- equipment

@icon('motor', 'equipment')
def _(p):
    p.rect(3, 6.5, 17, 17.5, r=2)
    p.line([(17, 12), (21.5, 12)])
    p.line([(5, 20.5), (15, 20.5)])
    p.line([(10, 17.5), (10, 20.5)])
    for x in (7, 10, 13):
        p.line([(x, 9.5), (x, 14.5)], w=1.6)


@icon('pump', 'equipment')
def _(p):
    p.circle(10.5, 13.5, 6.5)
    p.rect(10.5, 4, 21.5, 8.5, r=1, filled=True)
    p.circle(10.5, 13.5, 2, filled=True)
    p.line([(4.5, 21.5), (16.5, 21.5)])


@icon('fan', 'equipment')
def _(p):
    p.circle(12, 12, 9.5)
    for a in (-90, 30, 150):
        x, y = at(12, 12, 4.4, a)
        p.circle(x, y, 3.0, filled=True)
    p.circle(12, 12, 2.2, filled=True)


@icon('heater', 'equipment')
def _(p):
    for x0 in (6.5, 12, 17.5):
        pts = [(x0 + 1.6 * math.sin((y - 3) / 16 * 2 * math.pi * 1.5), y) for y in [3 + i * 0.5 for i in range(29)]]
        p.line(pts, w=1.8)
    p.line([(3.5, 21.5), (20.5, 21.5)])


@icon('flame', 'equipment')
def _(p):
    pts = []
    for i in range(0, 361, 6):
        t = math.radians(i)
        r = 7.0
        x, y = 12 + r * math.sin(t), 14.5 - r * math.cos(t)
        if 140 < i < 220:                               # (bottom stays round)
            pass
        pts.append((x, y))
    # a teardrop: the round bottom and a tip at the top
    drop = [(12, 2.5)]
    for i in range(-50, 231, 5):
        drop.append(at(12, 14.5, 7, i))
    p.poly(drop)
    inner = [(12, 10.5)] + [at(12, 17, 3, a) for a in range(-40, 221, 10)]
    p.poly(inner, fill=0)


@icon('valve', 'equipment')
def _(p):
    p.poly([(3, 9), (12, 14), (3, 19)], filled=False, w=1.8)
    p.poly([(21, 9), (12, 14), (21, 19)], filled=False, w=1.8)
    p.line([(12, 14), (12, 5.5)])
    p.line([(7.5, 4.5), (16.5, 4.5)])


@icon('tank', 'equipment')
def _(p):
    p.rect(5.5, 2.5, 18.5, 21.5, r=3)
    p.rect(7.5, 12.5, 16.5, 19.5, r=1.2, filled=True)


@icon('conveyor', 'equipment')
def _(p):
    p.rect(1.5, 13, 22.5, 20, r=3.5)
    for x in (5.5, 12, 18.5):
        p.circle(x, 16.5, 1.4, filled=True)
    p.rect(8, 5, 16, 11, r=0.8, filled=True)


@icon('mixer', 'equipment')
def _(p):
    p.d.chord(p.box(3, 3, 21, 21), 0, 180, outline=255, width=int(W * U))
    p.line([(3, 12), (21, 12)])
    p.line([(12, 1.5), (12, 9)])
    p.d.ellipse(p.box(8.5, 5.5, 15.5, 17.5), outline=255, width=int(1.6 * U))
    p.line([(12, 5.5), (12, 17.5)], w=1.6)


@icon('oven', 'equipment')
def _(p):
    p.rect(2.5, 3.5, 21.5, 20.5, r=2)
    p.rect(5.5, 10, 18.5, 17.5, r=1, w=1.6)
    for x in (6.5, 10, 13.5):
        p.circle(x, 6.8, 1.1, filled=True)
    p.line([(9, 13.5), (15, 13.5)], w=1.6)


@icon('cooling', 'equipment')
def _(p):
    for a in (0, 60, 120):
        x0, y0 = at(12, 12, 9.5, a)
        x1, y1 = at(12, 12, 9.5, a + 180)
        p.line([(x0, y0), (x1, y1)], w=1.8)
        for end in (a, a + 180):                        # branches open outwards
            base = at(12, 12, 5.2, end)
            p.line([at(base[0], base[1], 3.0, end - 45), base, at(base[0], base[1], 3.0, end + 45)], w=1.6)


@icon('slicer', 'equipment')
def _(p):
    # a loaf cut into slices
    p.d.chord(p.box(2, 5, 22, 15), 180, 360, fill=255)
    p.rect(2, 9.5, 22, 20, r=1.5, filled=True)
    for x in (7.2, 12, 16.8):
        p.line([(x + 1.2, 6), (x - 1.2, 20)], w=1.4, fill=0)


@icon('package', 'equipment')
def _(p):
    pts = [(12, 2.5), (20.5, 7), (20.5, 17), (12, 21.5), (3.5, 17), (3.5, 7)]
    p.poly(pts, filled=False, w=1.8)
    p.line([(3.5, 7), (12, 11.5), (20.5, 7)], w=1.8)
    p.line([(12, 11.5), (12, 21.5)], w=1.8)


@icon('thermometer', 'equipment')
def _(p):
    p.rect(9.5, 2.5, 14.5, 15, r=2.5, w=1.8)
    p.circle(12, 18, 3.8, filled=True)
    p.line([(12, 8), (12, 17)], w=2.2)
    for y in (6, 9, 12):
        p.line([(16.5, y), (18.5, y)], w=1.4)


@icon('humidity', 'equipment')
def _(p):
    drop = [(12, 2.5)] + [at(12, 14.5, 6.8, a) for a in range(-35, 216, 5)]
    p.poly(drop, filled=False, w=1.9)
    p.arc(12, 14.5, 3.8, 40, 120, w=1.6)


@icon('gauge', 'equipment')
def _(p):
    p.arc(12, 13.5, 9.5, 135, 405)
    p.line([(12, 13.5), at(12, 13.5, 6.5, -50)])
    p.circle(12, 13.5, 2, filled=True)
    for a in (135, 180, 225, 270, 315, 360, 405):
        p.line([at(12, 13.5, 6.2, a), at(12, 13.5, 4.8, a)], w=1.2) if False else None


@icon('settings', 'interface')
def _(p):
    pts = []
    n = 8
    for i in range(n):
        a = i * 360 / n
        pts += [at(12, 12, 7.6, a - 14), at(12, 12, 10.2, a - 9), at(12, 12, 10.2, a + 9), at(12, 12, 7.6, a + 14)]
    p.poly(pts)
    p.circle(12, 12, 3.4, filled=True, fill=0)


@icon('factory', 'equipment')
def _(p):
    p.poly([(2.5, 21.5), (2.5, 11), (8, 14), (8, 11), (13.5, 14), (13.5, 3.5), (18, 3.5), (18, 9), (21.5, 9), (21.5, 21.5)])
    for x in (5, 10.5, 16):
        p.rect(x, 16.5, x + 2.5, 19, filled=True, fill=0)


@icon('power', 'interface')
def _(p):
    p.arc(12, 13, 8.5, -60, 240)
    p.line([(12, 2.5), (12, 11.5)])


# ---------------------------------------------------------------- interface

@icon('home', 'interface')
def _(p):
    p.line([(2.5, 11.5), (12, 3), (21.5, 11.5)])
    p.line([(5.5, 10), (5.5, 21), (18.5, 21), (18.5, 10)])
    p.rect(10, 14.5, 14, 21, filled=True)


@icon('trend', 'interface')
def _(p):
    p.line([(3, 3), (3, 21), (21, 21)])
    p.line([(6.5, 16.5), (10.5, 10.5), (14, 13.5), (20, 6)])


@icon('report', 'interface')
def _(p):
    p.rect(4, 13, 7.5, 19.5, r=0.6, filled=True)
    p.rect(10.25, 7.5, 13.75, 19.5, r=0.6, filled=True)
    p.rect(16.5, 3.5, 20, 19.5, r=0.6, filled=True)
    p.line([(2.5, 21.5), (21.5, 21.5)])


@icon('alarm', 'interface')
def _(p):
    p.d.chord(p.box(5.5, 3.5, 18.5, 16.5), 180, 360, fill=255)
    p.rect(5.5, 9.5, 18.5, 17.5, filled=True)
    p.rect(3, 17, 21, 19.5, r=1.2, filled=True)
    p.circle(12, 21.2, 1.9, filled=True)
    p.circle(12, 3.2, 1.3, filled=True)


@icon('warning', 'interface')
def _(p):
    p.poly([(12, 2.5), (22.5, 20.5), (1.5, 20.5)], filled=False, w=2)
    p.line([(12, 9), (12, 14)], w=2.2)
    p.circle(12, 17.2, 1.3, filled=True)


@icon('info', 'interface')
def _(p):
    p.circle(12, 12, 9.5)
    p.line([(12, 11), (12, 17)], w=2.2)
    p.circle(12, 7.4, 1.4, filled=True)


@icon('check', 'interface')
def _(p):
    p.circle(12, 12, 9.5)
    p.line([(7.5, 12.3), (10.6, 15.4), (16.6, 9)])


@icon('error', 'interface')
def _(p):
    p.circle(12, 12, 9.5)
    p.line([(8.5, 8.5), (15.5, 15.5)])
    p.line([(15.5, 8.5), (8.5, 15.5)])


@icon('play', 'interface')
def _(p):
    p.poly([(7, 4), (20, 12), (7, 20)])


@icon('stop', 'interface')
def _(p):
    p.rect(5, 5, 19, 19, r=2, filled=True)


@icon('pause', 'interface')
def _(p):
    p.rect(5.5, 4.5, 10, 19.5, r=1, filled=True)
    p.rect(14, 4.5, 18.5, 19.5, r=1, filled=True)


@icon('reset', 'interface')
def _(p):
    p.arc(12, 12.5, 8, -140, 160)
    tip = at(12, 12.5, 8, -140)
    p.poly([(tip[0] - 4.2, tip[1] - 1.2), (tip[0] + 1.2, tip[1] - 3.8), (tip[0] + 1.0, tip[1] + 2.6)])


@icon('user', 'interface')
def _(p):
    p.circle(12, 8, 4.3)
    p.arc(12, 23, 8.5, 180, 360)


@icon('lock', 'interface')
def _(p):
    p.arc(12, 9.5, 4.8, 180, 360)
    p.line([(7.2, 9.5), (7.2, 11)])
    p.line([(16.8, 9.5), (16.8, 11)])
    p.rect(4.5, 11, 19.5, 21.5, r=2, filled=True)
    p.circle(12, 15.3, 1.6, filled=True, fill=0)
    p.rect(11.3, 15.5, 12.7, 18.5, filled=True, fill=0)


@icon('recipe', 'interface')
def _(p):
    p.rect(4.5, 2.5, 19.5, 21.5, r=2)
    for y, x1 in ((8, 16), (12, 16), (16, 13)):
        p.line([(8, y), (x1, y)], w=1.8)


@icon('io', 'interface')
def _(p):
    p.line([(9.5, 2.5), (9.5, 7.5)])
    p.line([(14.5, 2.5), (14.5, 7.5)])
    p.rect(5.5, 7.5, 18.5, 15, r=2, filled=True)
    p.line([(12, 15), (12, 21.5)])


@icon('back', 'interface')
def _(p):
    p.line([(4, 12), (20.5, 12)])
    p.line([(10.5, 5.5), (4, 12), (10.5, 18.5)])


@icon('forward', 'interface')
def _(p):
    p.line([(3.5, 12), (20, 12)])
    p.line([(13.5, 5.5), (20, 12), (13.5, 18.5)])


@icon('menu', 'interface')
def _(p):
    for y in (6, 12, 18):
        p.line([(3.5, y), (20.5, y)])


@icon('list', 'interface')
def _(p):
    for y in (6, 12, 18):
        p.circle(4.5, y, 1.5, filled=True)
        p.line([(9, y), (20.5, y)])


@icon('clock', 'interface')
def _(p):
    p.circle(12, 12, 9.5)
    p.line([(12, 6.5), (12, 12), (16, 14.5)])


@icon('process', 'interface')
def _(p):
    for x in (2.5, 9.5, 16.5):
        p.rect(x, 8.5, x + 5, 15.5, r=1, filled=True)
    p.line([(7.5, 12), (9.5, 12)], w=1.6)
    p.line([(14.5, 12), (16.5, 12)], w=1.6)


# ----------------------------------------------------------------

def render(fn):
    pen = Pen()
    fn(pen)
    return pen.img


def write_all():
    names = sorted(ICONS)
    for size in SIZES:
        os.makedirs(os.path.join(OUT, str(size)), exist_ok=True)
    for name in names:
        mask = render(ICONS[name][1])
        for size in SIZES:
            a = mask.resize((size, size), Image.LANCZOS)
            img = Image.merge('RGBA', (Image.new('L', (size, size), 255),) * 3 + (a,))
            img.save(os.path.join(OUT, str(size), name + '.png'), optimize=True)
    # a sheet of all the icons, to look at them (and for an AI to see them)
    cols, cell = 8, 112
    rows = (len(names) + cols - 1) // cols
    sheet = Image.new('RGBA', (cols * cell, rows * cell), (25, 37, 56, 255))
    d = ImageDraw.Draw(sheet)
    try:
        font = ImageFont.truetype('arial.ttf', 13)
    except OSError:
        font = ImageFont.load_default()
    for i, name in enumerate(names):
        x, y = (i % cols) * cell, (i // cols) * cell
        ic = Image.open(os.path.join(OUT, '48', name + '.png'))
        sheet.alpha_composite(ic, (x + (cell - 48) // 2, y + 18))
        tw = d.textlength(name, font=font)
        d.text((x + (cell - tw) / 2, y + 78), name, fill=(229, 237, 245, 255), font=font)
    sheet.save(os.path.join(OUT, 'preview.png'), optimize=True)
    with open(os.path.join(OUT, 'icons.txt'), 'w', encoding='utf-8', newline='\n') as f:
        for group in ('equipment', 'interface'):
            f.write(group + ': ' + ' '.join(n for n in names if ICONS[n][0] == group) + '\n')
    print(f'{len(names)} icons x {len(SIZES)} sizes -> {OUT}')


if __name__ == '__main__':
    sys.exit(write_all())
