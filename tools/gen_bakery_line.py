"""Draws the live view of the demo projects/bakery: the bread line seen from diagonally above
(isometric, like projects/factory), and writes the screen screens/live.json. The pictures are
simple shaded solids drawn here: change the shapes or colours and run it again.

    python -I tools/gen_bakery_line.py            (needs Pillow: pip install pillow)

It also puts "Live" into the navigation of the template (screens/frame.json) and the screen into
project.json (both only once). The machines follow the tags the project already has
(<machine>_state 0 stopped / 1 running / 2 fault / 3 warning, <machine>_qty, conveyor_running ...).

Geometry: a grid cell is 72 x 36 px on the screen (2:1), one step in z is one px.
    screen x = OX + (gx - gy) * 36        screen y = OY + (gx + gy) * 18 - z
"""
import json, math, os
from PIL import Image, ImageDraw

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PROJ = os.path.join(ROOT, 'projects', 'bakery')
ASSETS = os.path.join(PROJ, 'assets', 'line')
REL = 'assets/line/'
SS = 4                                  # drawn 4 times as large and scaled down
CW, CH = 36, 18                         # half a cell on the screen
OX, OY = 200, 150                       # the screen position of the grid point (0, 0)
GW, GD = 21.6, 5.0                      # the floor
BY0, BY1 = 3.0, 3.6                     # the conveyor runs along gx between these gy
BC = (BY0 + BY1) / 2
BELT_Z = 16
STATIONS = [1.4, 3.8, 6.2, 8.6, 11.4, 14.2, 16.6, 19.1]
CYCLE = 3000                            # ms for a product from one station to the next

# the machines: tag prefix, the name above it, its box (gx0, gx1, gy0, gy1, height), where it stands
#   tunnel: over the conveyor (the products go through it); back: behind it (an arm reaches over)
MACHINES = [
    ('feeder',    'Feed',   (0.7, 2.1, 2.5, 4.1, 50), 'tunnel', 'equip_feeder'),
    ('mixer',     'Mix',    (3.1, 4.5, 0.9, 2.5, 42), 'back',   'equip_mixer'),
    ('divider',   'Divide', (5.5, 6.9, 2.5, 4.1, 46), 'tunnel', 'equip_divider'),
    ('proofing',  'Proof',  (7.7, 9.5, 2.3, 4.3, 78), 'tunnel', 'equip_proofing'),
    ('oven',      'Bake',   (10.1, 12.7, 2.5, 4.1, 48), 'tunnel', 'equip_oven'),
    ('cooling',   'Cool',   (13.5, 14.9, 0.7, 2.5, 72), 'back',   'equip_cooling'),
    ('slicer',    'Slice',  (15.9, 17.3, 2.5, 4.1, 46), 'tunnel', 'equip_slicer'),
    ('packaging', 'Pack',   (18.3, 19.9, 2.4, 4.2, 56), 'tunnel', 'equip_packaging'),
]

BG, PANEL, ACCENT, TEXT, MUTED = '#101827', '#192538', '#26c6da', '#e5edf5', '#91a0b4'
STATE_BG = {'0': '#3b4a5e', '1': '#36c98f', '2': '#f06464', '3': '#f5b942', 'default': '#3b4a5e'}
STATE_TEXT = {'0': TEXT, 'default': '#101827'}


def pt(gx, gy, z=0.0):
    return (OX + (gx - gy) * CW, OY + (gx + gy) * CH - z)


def shade(c, f):
    return tuple(max(0, min(255, int(round(v * f)))) for v in c[:3]) + ((c[3],) if len(c) > 3 else ())


def hexc(s, a=255):
    s = s.lstrip('#')
    return (int(s[0:2], 16), int(s[2:4], 16), int(s[4:6], 16), a)


class Canvas:
    def __init__(self, box):
        self.x0, self.y0 = int(math.floor(box[0])), int(math.floor(box[1]))
        self.w, self.h = int(math.ceil(box[2])) - self.x0, int(math.ceil(box[3])) - self.y0
        self.im = Image.new('RGBA', (self.w * SS, self.h * SS), (0, 0, 0, 0))
        self.d = ImageDraw.Draw(self.im, 'RGBA')

    def _p(self, p):
        return ((p[0] - self.x0) * SS, (p[1] - self.y0) * SS)

    def _layer(self, draw):
        layer = Image.new('RGBA', self.im.size, (0, 0, 0, 0))
        draw(ImageDraw.Draw(layer))
        self.im = Image.alpha_composite(self.im, layer)
        self.d = ImageDraw.Draw(self.im, 'RGBA')

    def poly(self, pts, fill, outline=None):
        q = [self._p(p) for p in pts]
        self._layer(lambda d: d.polygon(q, fill=fill))
        if outline:
            self.d.line(q + [q[0]], fill=outline, width=SS)

    def line(self, pts, fill, width=1.0):
        self.d.line([self._p(p) for p in pts], fill=fill, width=max(1, int(width * SS)), joint='curve')

    def ellipse(self, cx, cy, rx, ry, fill, outline=None):
        a, b = self._p((cx - rx, cy - ry)), self._p((cx + rx, cy + ry))
        self._layer(lambda d: d.ellipse([a, b], fill=fill, outline=outline, width=SS))

    def top(self, gx0, gx1, gy0, gy1, z, color, outline=None):
        self.poly([pt(gx0, gy0, z), pt(gx1, gy0, z), pt(gx1, gy1, z), pt(gx0, gy1, z)], color, outline)

    def left(self, gx0, gx1, gy, z0, z1, color, outline=None):      # the face towards +gy
        self.poly([pt(gx0, gy, z1), pt(gx1, gy, z1), pt(gx1, gy, z0), pt(gx0, gy, z0)], color, outline)

    def right(self, gx, gy0, gy1, z0, z1, color, outline=None):     # the face towards +gx
        self.poly([pt(gx, gy0, z1), pt(gx, gy1, z1), pt(gx, gy1, z0), pt(gx, gy0, z0)], color, outline)

    def box(self, gx0, gx1, gy0, gy1, z0, z1, color, edge=True, faces='lrt'):
        ol = shade(color, 0.45) if edge else None
        if 'l' in faces: self.left(gx0, gx1, gy1, z0, z1, shade(color, 0.8), ol)
        if 'r' in faces: self.right(gx1, gy0, gy1, z0, z1, shade(color, 0.62), ol)
        if 't' in faces: self.top(gx0, gx1, gy0, gy1, z1, color, ol)

    def cylinder(self, gx, gy, r, z0, z1, color):
        """An upright cylinder: r in grid units (drawn as an ellipse of 2:1)."""
        cx, cy0 = pt(gx, gy, z0)
        _, cy1 = pt(gx, gy, z1)
        rx, ry = r * CW * 1.41, r * CH * 1.41
        self.ellipse(cx, cy0, rx, ry, shade(color, 0.6))
        self.poly([(cx - rx, cy0), (cx + rx, cy0), (cx + rx, cy1), (cx - rx, cy1)], shade(color, 0.85))
        self.poly([(cx + rx * 0.25, cy0 + ry * 0.97), (cx + rx, cy0), (cx + rx, cy1), (cx + rx * 0.25, cy1 + ry * 0.97)], shade(color, 0.68))
        self.ellipse(cx, cy1, rx, ry, color, shade(color, 0.5))

    def shadow(self, gx0, gx1, gy0, gy1, a=70):
        self.poly([pt(gx0 + 0.15, gy0 + 0.1), pt(gx1 + 0.35, gy0 + 0.1), pt(gx1 + 0.35, gy1 + 0.2), pt(gx0 + 0.15, gy1 + 0.2)], (0, 0, 0, a))

    def save(self, name):
        im = self.im.resize((self.w, self.h), Image.LANCZOS)
        im.save(os.path.join(ASSETS, name), optimize=True)
        return {'x': self.x0, 'y': self.y0, 'w': self.w, 'h': self.h, 'src': REL + name}


def bbox(points, pad=4):
    xs = [p[0] for p in points]; ys = [p[1] for p in points]
    return (min(xs) - pad, min(ys) - pad, max(xs) + pad, max(ys) + pad)


def box_pts(gx0, gx1, gy0, gy1, z0, z1):
    return [pt(x, y, z) for x in (gx0, gx1) for y in (gy0, gy1) for z in (z0, z1)]


STEEL, STEEL_D, DARK = hexc('#cfd8e3'), hexc('#8996a8'), hexc('#1b2535')
WARM, DOUGH, CRUST, CRUST_D = hexc('#ffb347'), hexc('#f3dcae'), hexc('#c27a3a'), hexc('#8a4f22')


# ---------------------------------------------------------------- the hall
def draw_floor():
    c = Canvas((0, 64, 980, 640))
    c.poly([pt(0, 0), pt(GW, 0), pt(GW, GD), pt(0, GD)], hexc('#1a2638'))
    c.box(0, GW, 0, GD, -14, 0, hexc('#23324a'), edge=False, faces='lr')        # the slab
    for gx in range(int(GW) + 1):
        for gy in range(int(GD)):
            if (gx + gy) % 2 == 0 and gx < GW - 0.5:
                c.top(gx, min(gx + 1, GW), gy, gy + 1, 0, hexc('#1f2d42'))
    for gy in (4.45,):                                                          # the walkway
        c.line([pt(0.3, gy), pt(GW - 0.3, gy)], hexc('#26c6da', 120), 1.4)
    c.line([pt(0.3, 0.2), pt(GW - 0.3, 0.2)], hexc('#26c6da', 60), 1.0)
    # along the back: flour silos, a tank, cabinets
    for gx in (0.9, 1.9):
        c.cylinder(gx, 0.75, 0.42, 0, 92, hexc('#e9edf2'))
        cx, cy = pt(gx, 0.75, 70)
        c.line([(cx - 14, cy), (cx + 14, cy)], hexc('#26c6da'), 2)
    c.cylinder(6.3, 0.8, 0.38, 0, 58, hexc('#b8c4d2'))                          # water
    for gx in (8.6, 9.6, 16.5, 17.5):
        c.shadow(gx, gx + 0.8, 0.3, 0.9)
        c.box(gx, gx + 0.8, 0.3, 0.9, 0, 46, hexc('#3d4f68'))
        c.left(gx + 0.12, gx + 0.68, 0.9, 26, 38, hexc('#26c6da', 140))
        c.left(gx + 0.12, gx + 0.3, 0.9, 8, 13, hexc('#36c98f'))
    # flour sacks on a pallet
    c.box(3.0, 4.0, 3.95, 4.35, 0, 0.1, hexc('#000000', 0)) if False else None
    # the conveyor: legs and frame
    c.shadow(0.6, 20.4, BY0, BY1, 55)
    for gx in range(1, 21):
        c.box(gx + 0.42, gx + 0.56, BY0 + 0.06, BY1 - 0.06, 0, 10, hexc('#2c3a50'), edge=False)
    c.box(0.6, 20.4, BY0 - 0.06, BY1 + 0.06, 10, BELT_Z, hexc('#4a5b75'))
    return c.save('floor.png')


def draw_belt(frame, frames):
    z = BELT_Z + 0.5
    c = Canvas(bbox([pt(0.6, BY1, z), pt(20.4, BY0, z), pt(0.6, BY0, z), pt(20.4, BY1, z)]))
    c.top(0.65, 20.35, BY0, BY1, z, hexc('#141c28'))
    step = 0.5
    gx = 0.65 + step * frame / frames
    while gx < 20.3:
        c.line([pt(gx, BY0 + 0.05, z), pt(gx, BY1 - 0.05, z)], hexc('#3b4a5e'), 1.3)
        gx += step
    return c.save('belt%d.png' % frame)


# ---------------------------------------------------------------- what moves on the conveyor
def draw_product(kind):
    z = BELT_Z + 1
    c = Canvas(bbox(box_pts(-0.32, 0.32, -0.32, 0.32, z, z + 22)))
    cx, cy = pt(0, 0, z)
    if kind == 'ingredients':                    # a tub of flour, water and yeast
        c.box(-0.22, 0.22, -0.2, 0.2, z, z + 10, hexc('#dfe6ee'))
        c.top(-0.17, 0.17, -0.15, 0.15, z + 10, hexc('#fbfbf6'))
    elif kind == 'dough':                        # a big lump
        c.ellipse(cx, cy - 6, 15, 9, shade(DOUGH, 0.8))
        c.ellipse(cx - 1, cy - 9, 12, 7, DOUGH)
    elif kind == 'balls':                        # pieces rounded off
        for dx, dy in ((-0.13, -0.1), (0.13, 0.1), (0.13, -0.12), (-0.12, 0.12)):
            px, py = pt(dx, dy, z)
            c.ellipse(px, py - 3, 5.5, 3.6, shade(DOUGH, 0.82))
            c.ellipse(px - 0.5, py - 4.5, 4.2, 2.6, DOUGH)
    elif kind in ('risen', 'baked'):             # in a tin
        c.box(-0.26, 0.26, -0.17, 0.17, z, z + 7, hexc('#7d8a9c'))
        dome = DOUGH if kind == 'risen' else CRUST
        tx, ty = pt(0, 0, z + 7)
        c.ellipse(tx, ty - 2, 17, 7, shade(dome, 0.75))
        c.ellipse(tx - 1, ty - 4, 15, 5.5, dome)
    else:                                        # a loaf, sliced or not
        c.box(-0.26, 0.26, -0.15, 0.15, z, z + 11, CRUST)
        tx, ty = pt(0, 0, z + 11)
        c.ellipse(tx, ty, 15, 6, shade(CRUST, 1.08))
        if kind == 'sliced':
            for k in range(-3, 4):
                g = k * 0.07
                c.line([pt(g, 0.15, z + 1), pt(g, 0.15, z + 11)], hexc('#f6e3c0'), 0.9)
    return c.save('p_%s.png' % kind)


def draw_pack():
    """A packed loaf on the stack at the end."""
    z = 0
    c = Canvas(bbox(box_pts(-0.3, 0.3, -0.2, 0.2, 0, 16)))
    c.box(-0.26, 0.26, -0.16, 0.16, z, z + 12, hexc('#e8f4f8'))
    c.left(-0.12, 0.12, 0.16, z + 3, z + 9, hexc('#26c6da'))
    return c


def draw_stack():
    c = Canvas(bbox(box_pts(20.4, 21.5, 2.6, 4.0, 0, 44)))
    c.shadow(20.5, 21.4, 2.7, 3.9)
    c.box(20.5, 21.4, 2.7, 3.9, 0, 6, hexc('#8d6e63'))
    for layer in range(3):
        for gx, gy in ((20.62, 2.85), (20.62, 3.35), (21.0, 2.85), (21.0, 3.35)):
            z = 6 + layer * 12
            c.box(gx, gx + 0.34, gy, gy + 0.42, z, z + 11, hexc('#e8f4f8'))
            c.left(gx + 0.08, gx + 0.26, gy + 0.42, z + 3, z + 8, hexc('#26c6da'))
    return c.save('stack.png')


# ---------------------------------------------------------------- the machines
def machine_canvas(b, extra=40):
    x0, x1, y0, y1, h = b
    return Canvas(bbox(box_pts(x0, x1 + 0.4, y0, y1 + 0.3, 0, h + extra)))


def draw_body(i):
    """The whole machine (for a tunnel: drawn before the products; its front comes again after)."""
    key, _, b, kind, _ = MACHINES[i]
    x0, x1, y0, y1, h = b
    c = machine_canvas(b, 70)
    c.shadow(x0, x1, y0, y1)
    c.box(x0, x1, y0, y1, 0, h, STEEL)
    if kind == 'tunnel':
        c.right(x1, BY0 - 0.04, BY1 + 0.04, 10, 34, hexc('#0d1420'))         # where the products come out
    decorate(c, i, front=False)
    return c.save('m_%s.png' % key)


def draw_front(i):
    """The front wall and the roof of a tunnel machine: over the products inside it."""
    key, _, b, _, _ = MACHINES[i]
    x0, x1, y0, y1, h = b
    c = machine_canvas(b, 70)
    ol = shade(STEEL, 0.45)
    c.left(x0, x1, y1, 0, h, shade(STEEL, 0.8), ol)
    c.top(x0, x1, y0, y1, h, STEEL, ol)
    decorate(c, i, front=True)
    return c.save('f_%s.png' % key)


def decorate(c, i, front):
    key, _, b, kind, _ = MACHINES[i]
    x0, x1, y0, y1, h = b
    if kind == 'tunnel' and not front:
        return                                    # (the front picture has the rest)
    c.top(x0, x1, y0, y1, h, STEEL, shade(STEEL, 0.45))
    c.box(x0, x1, y0, y1, h, h + 4, hexc(ACCENT))                           # the cyan band of the roof
    c.left(x0 + 0.12, x1 - 0.12, y1, 4, 7, hexc(ACCENT))
    if key == 'feeder':                          # a hopper with flour
        g, m = (x0 + x1) / 2, (y0 + y1) / 2
        t = h + 4
        c.poly([pt(g - 0.3, m + 0.35, t), pt(g + 0.3, m + 0.35, t), pt(g + 0.5, m + 0.55, t + 22), pt(g - 0.5, m + 0.55, t + 22)], shade(STEEL, 0.82))
        c.poly([pt(g + 0.3, m - 0.35, t), pt(g + 0.3, m + 0.35, t), pt(g + 0.5, m + 0.55, t + 22), pt(g + 0.5, m - 0.55, t + 22)], shade(STEEL, 0.64))
        c.top(g - 0.5, g + 0.5, m - 0.55, m + 0.55, t + 22, hexc('#f7f5ee'))
        panel(c, x0, x1, y1, h)
    elif key == 'mixer':                         # the bowl on top
        g, m = (x0 + x1) / 2, (y0 + y1) / 2
        c.cylinder(g, m, 0.5, h + 4, h + 24, hexc('#dfe6ee'))
        cx, cy = pt(g, m, h + 24)
        c.ellipse(cx, cy, 0.42 * CW * 1.41, 0.42 * CH * 1.41, DOUGH)
        c.box(x1 - 0.25, x1 - 0.05, y0 + 0.2, y0 + 0.45, h + 4, h + 44, STEEL)    # the column of the hook
        panel(c, x0, x1, y1, h)
    elif key == 'divider':
        g, m = (x0 + x1) / 2, (y0 + y1) / 2
        c.box(g - 0.35, g + 0.35, m - 0.35, m + 0.35, h + 4, h + 20, STEEL)
        c.top(g - 0.25, g + 0.25, m - 0.25, m + 0.25, h + 20, DOUGH)
        panel(c, x0, x1, y1, h)
    elif key == 'proofing':                      # a warm, damp room with racks behind glass
        c.left(x0 + 0.2, x1 - 0.2, y1, 12, h - 10, hexc('#0d1420'))
        c.left(x0 + 0.25, x1 - 0.25, y1, 14, h - 12, hexc('#ffb347', 70))
        for z in range(22, h - 12, 12):
            c.line([pt(x0 + 0.3, y1, z), pt(x1 - 0.3, y1, z)], hexc('#2b3a52'), 1.4)
            for k in range(4):
                gx = x0 + 0.45 + k * (x1 - x0 - 0.9) / 3
                px, py = pt(gx, y1, z + 3)
                c.ellipse(px, py, 4, 2.6, DOUGH)
    elif key == 'oven':                          # a long tunnel oven: a glowing window, a chimney
        c.left(x0 + 0.25, x1 - 0.25, y1, 16, 30, hexc('#0d1420'))
        c.left(x0 + 0.3, x1 - 0.3, y1, 18, 28, hexc('#ff8a3d', 150))
        c.box(x1 - 0.6, x1 - 0.3, y0 + 0.2, y0 + 0.5, h + 4, h + 36, hexc('#56657c'))
        for k in range(3):
            gx = x0 + 0.4 + k * 0.35
            c.ellipse(*pt(gx, y1, 9), 2.6, 2.6, hexc('#e5edf5'))
    elif key == 'cooling':                       # a tower with two fans on the roof
        c.left(x0 + 0.2, x1 - 0.2, y1, 10, h - 12, hexc('#2b3a52'))
        for z in range(14, h - 14, 6):
            c.line([pt(x0 + 0.25, y1, z), pt(x1 - 0.25, y1, z)], hexc('#56657c'), 1.2)
        for g in (x0 + 0.42, x0 + 0.98):
            cx, cy = pt(g, (y0 + y1) / 2, h + 4)
            c.ellipse(cx, cy, 0.3 * CW * 1.41, 0.3 * CH * 1.41, hexc('#0d1420'))
    elif key == 'slicer':
        g, m = (x0 + x1) / 2, (y0 + y1) / 2
        c.box(g - 0.45, g + 0.45, m - 0.3, m + 0.3, h + 4, h + 16, hexc('#56657c'))
        panel(c, x0, x1, y1, h)
    elif key == 'packaging':                     # a roll of film on top
        c.box(x0 + 0.3, x1 - 0.3, y0 + 0.4, y1 - 0.4, h + 4, h + 10, STEEL_D)
        for k in range(6):
            gx = x0 + 0.4 + k * 0.16
            c.line([pt(gx, y0 + 0.55, h + 16), pt(gx, y1 - 0.55, h + 16)], hexc('#bfe9f0'), 2.2)
        panel(c, x0, x1, y1, h)


def panel(c, x0, x1, y1, h):
    """A small screen and two buttons on the front."""
    c.left(x0 + 0.2, x0 + 0.75, y1, h - 26, h - 10, hexc('#0d1420'))
    c.left(x0 + 0.24, x0 + 0.71, y1, h - 24, h - 12, hexc('#26c6da', 150))
    c.left(x1 - 0.5, x1 - 0.34, y1, h - 22, h - 16, hexc('#36c98f'))
    c.left(x1 - 0.3, x1 - 0.14, y1, h - 22, h - 16, hexc('#f06464'))


def draw_arm(i):
    """What a machine behind the conveyor holds over it (drawn after the products)."""
    key, _, b, _, _ = MACHINES[i]
    x0, x1, y0, y1, h = b
    g = STATIONS[i]
    c = Canvas(bbox(box_pts(g - 0.4, g + 0.4, y1 - 0.1, BY1 + 0.2, 20, h + 6)))
    if key == 'mixer':                           # a chute down to the conveyor
        c.box(g - 0.2, g + 0.2, y1, BY0 - 0.05, h - 12, h - 2, STEEL)
        c.box(g - 0.16, g + 0.16, BY0 - 0.2, BC + 0.05, 30, h - 4, shade(STEEL, 0.9))
    else:                                        # a hood that blows cold air on the bread
        c.box(g - 0.34, g + 0.34, y1, BY1 + 0.08, h - 30, h - 20, hexc('#56657c'))
        c.box(g - 0.3, g + 0.3, BY0 - 0.1, BY1 + 0.08, h - 40, h - 30, hexc('#3d4f68'))
    return c.save('arm_%s.png' % key)


def draw_hook(frame):
    """The mixer's hook turning in the bowl (4 frames)."""
    _, _, b, _, _ = MACHINES[1]
    x0, x1, y0, y1, h = b
    g, m = (x0 + x1) / 2, (y0 + y1) / 2
    c = Canvas(bbox(box_pts(g - 0.6, g + 0.6, m - 0.6, m + 0.6, h, h + 48)))
    a = frame * math.pi / 2
    hx, hy = g + 0.26 * math.cos(a), m + 0.26 * math.sin(a)
    c.line([pt(x1 - 0.15, y0 + 0.32, h + 42), pt(hx, hy, h + 42)], STEEL, 4)
    c.line([pt(hx, hy, h + 42), pt(hx, hy, h + 26)], hexc('#e5edf5'), 3)
    c.ellipse(*pt(hx, hy, h + 25), 3, 2, hexc('#e5edf5'))
    return c.save('hook%d.png' % frame)


def draw_fan(frame):
    """The blades of the two fans of the cooling tower (3 frames: they turn)."""
    _, _, b, _, _ = MACHINES[5]
    x0, x1, y0, y1, h = b
    c = Canvas(bbox(box_pts(x0, x1, y0, y1, h, h + 8)))
    for g in (x0 + 0.42, x0 + 0.98):
        m = (y0 + y1) / 2
        for k in range(3):
            a = frame * math.pi * 2 / 9 + k * math.pi * 2 / 3
            p = [pt(g, m, h + 5), pt(g + 0.27 * math.cos(a), m + 0.27 * math.sin(a), h + 5), pt(g + 0.27 * math.cos(a + 0.6), m + 0.27 * math.sin(a + 0.6), h + 5)]
            c.poly(p, hexc('#9fb3c8'))
        c.ellipse(*pt(g, m, h + 5), 2.5, 1.5, hexc('#e5edf5'))
    return c.save('fan%d.png' % frame)


def draw_glow():
    """The heat of the oven: a soft orange light over its window (pulses while the heater is on)."""
    _, _, b, _, _ = MACHINES[4]
    x0, x1, y0, y1, h = b
    pts = [pt(x0 + 0.1, y1, 34), pt(x1 - 0.1, y1, 34), pt(x1 - 0.1, y1, 12), pt(x0 + 0.1, y1, 12)]
    c = Canvas(bbox(pts, 10))
    c.poly(pts, hexc('#ff8a3d', 90))
    c.left(x0 + 0.3, x1 - 0.3, y1, 18, 28, hexc('#ffc078', 230))
    return c.save('glow.png')


def draw_steam():
    """Damp air rising from the proofer (shown while the humidifier is on)."""
    _, _, b, _, _ = MACHINES[3]
    x0, x1, y0, y1, h = b
    g, m = (x0 + x1) / 2, (y0 + y1) / 2
    cx, cy = pt(g, m, h + 6)
    c = Canvas((cx - 34, cy - 50, cx + 34, cy + 6))
    for dx, dy, r, a in ((-12, -10, 9, 90), (8, -20, 11, 70), (-4, -34, 13, 45), (12, -40, 9, 35)):
        c.ellipse(cx + dx, cy + dy, r, r * 0.8, (230, 240, 250, a))
    return c.save('steam.png')


def draw_blade(frame):
    """The slicer's blades going up and down (2 frames)."""
    _, _, b, _, _ = MACHINES[6]
    x0, x1, y0, y1, h = b
    g = STATIONS[6]
    c = Canvas(bbox(box_pts(g - 0.4, g + 0.4, BY0, BY1, BELT_Z, h + 4)))
    z = BELT_Z + (26 if frame else 14)
    for k in range(-3, 4):
        gx = g + k * 0.08
        c.line([pt(gx, BY0 + 0.05, z + 18), pt(gx, BY0 + 0.05, z)], hexc('#e5edf5'), 1.1)
    return c.save('blade%d.png' % frame)


def draw_lamp(kind):
    lit = {'off': None, 'green': 0, 'yellow': 1, 'red': 2}[kind]
    cols = [hexc('#36c98f'), hexc('#f5b942'), hexc('#f06464')]
    cx, cy = pt(0, 0)
    c = Canvas((cx - 14, cy - 44, cx + 14, cy + 4))
    c.poly([(cx - 1.5, cy), (cx + 1.5, cy), (cx + 1.5, cy - 12), (cx - 1.5, cy - 12)], hexc('#56657c'))
    for k in range(3):
        y = cy - 12 - k * 9
        on = lit == k
        col = cols[k] if on else shade(cols[k], 0.3)
        if on:
            c.ellipse(cx, y - 4.5, 13, 10, cols[k][:3] + (80,))
        c.poly([(cx - 5, y), (cx + 5, y), (cx + 5, y - 8), (cx - 5, y - 8)], col)
        c.poly([(cx + 1, y), (cx + 5, y), (cx + 5, y - 8), (cx + 1, y - 8)], shade(col, 0.8))
    return c.save('lamp_%s.png' % kind)


def draw_alert(i):
    _, _, b, _, _ = MACHINES[i]
    x0, x1, y0, y1, h = b
    pts = [pt(x0 - 0.3, y0 - 0.3), pt(x1 + 0.3, y0 - 0.3), pt(x1 + 0.3, y1 + 0.3), pt(x0 - 0.3, y1 + 0.3)]
    c = Canvas(bbox(pts, 4))
    c.poly(pts, (240, 100, 100, 70))
    c.line(pts + [pts[0]], (240, 100, 100, 255), 2.5)
    return c.save('alert_%s.png' % MACHINES[i][0])


# ---------------------------------------------------------------- the screen
def image(a, **kw):
    w = {'type': 'image', 'x': a['x'], 'y': a['y'], 'src': a['src']}
    w.update(kw)
    return w


def label(text=None, **kw):
    w = {'type': 'label', 'style': {'font': kw.pop('font', 16), 'text_color': kw.pop('color', TEXT)}}
    if text is not None: w['text'] = text
    w['style'].update(kw.pop('style', {}))
    w.update(kw)
    return w


def build():
    os.makedirs(ASSETS, exist_ok=True)
    for f in os.listdir(ASSETS):
        os.remove(os.path.join(ASSETS, f))
    floor = draw_floor()
    belts = [draw_belt(k, 4) for k in range(4)]
    kinds = ['ingredients', 'dough', 'balls', 'risen', 'baked', 'loaf', 'sliced']
    products = {k: draw_product(k) for k in kinds}
    bodies = [draw_body(i) for i in range(8)]
    fronts = {i: draw_front(i) for i in range(8) if MACHINES[i][3] == 'tunnel'}
    arms = {i: draw_arm(i) for i in range(8) if MACHINES[i][3] == 'back'}
    hooks = [draw_hook(k) for k in range(4)]
    fans = [draw_fan(k) for k in range(3)]
    glow, steam, stack = draw_glow(), draw_steam(), draw_stack()
    blades = [draw_blade(k) for k in range(2)]
    lamps = {k: draw_lamp(k) for k in ('off', 'green', 'yellow', 'red')}
    alerts = [draw_alert(i) for i in range(8)]

    def at(a, gx, gy, z=0.0):
        o, p = pt(0, 0), pt(gx, gy, z)
        return int(round(a['x'] + p[0] - o[0])), int(round(a['y'] + p[1] - o[1]))

    def state(i): return MACHINES[i][0] + '_state'
    def fault(i): return {'tag': state(i), 'eq': 2}
    def go(i): return {'action': 'goto', 'screen': MACHINES[i][4]}
    moving = 'conveyor_running'

    kids = [image(floor)]
    for i in range(8):                                   # the red frames on the floor
        kids.append(image(alerts[i], bind={'visible': fault(i)}, animate={'preset': 'pulse', 'when': fault(i)}))
    for i in range(8):                                   # behind the conveyor
        if MACHINES[i][3] == 'back':
            kids.append(image(bodies[i], on_click=go(i)))
    kids.append(image(belts[0], animate={'prop': 'frames', 'files': [b['src'] for b in belts], 'time': 400, 'when': moving}))
    for i in range(8):                                   # the tunnels, behind what goes through them
        if MACHINES[i][3] == 'tunnel':
            kids.append(image(bodies[i], on_click=go(i)))
    # the products: one between each two stations; all move at once to the next station and start
    # again. At a station the one that arrives is replaced by the one that leaves: dough, pieces,
    # risen in the tin, baked, out of the tin, sliced.
    for k, kind in enumerate(kinds):
        a = products[kind]
        x0, y0 = at(a, STATIONS[k], BC)
        x1, y1 = at(a, STATIONS[k + 1], BC)
        kids.append({'type': 'image', 'x': x0, 'y': y0, 'src': a['src'], 'animate': [
            {'prop': 'x', 'from': x0, 'to': x1, 'time': CYCLE, 'path': 'linear', 'when': moving},
            {'prop': 'y', 'from': y0, 'to': y1, 'time': CYCLE, 'path': 'linear', 'when': moving}]})
    run = lambda i: {'tag': state(i), 'eq': 1}
    kids.append(image(blades[0], animate={'prop': 'frames', 'files': [b['src'] for b in blades], 'time': 500, 'when': run(6)}))
    for i in range(8):
        if MACHINES[i][3] == 'tunnel':
            kids.append(image(fronts[i], on_click=go(i)))
        else:
            kids.append(image(arms[i]))
    kids.append(image(stack))
    kids.append(image(hooks[0], animate={'prop': 'frames', 'files': [h['src'] for h in hooks], 'time': 800, 'when': 'mixer_running'}))
    kids.append(image(fans[0], animate={'prop': 'frames', 'files': [f['src'] for f in fans], 'time': 300, 'when': 'cooling_fan_on'}))
    kids.append(image(glow, bind={'visible': 'oven_heater_on'}, animate={'preset': 'pulse', 'time': 1200, 'when': 'oven_heater_on'}))
    kids.append(image(steam, bind={'visible': 'proofing_humidifier_on'}, animate={'prop': 'opa', 'from': 100, 'to': 30, 'time': 1500, 'playback': True, 'when': 'proofing_humidifier_on'}))
    for i in range(8):                                   # signal towers on the roofs
        key, _, b, _, _ = MACHINES[i]
        x0, x1, y0, y1, h = b
        lx, ly = at(lamps['off'], x0 + 0.18, y1 - 0.18, h + 4)
        kids.append({'type': 'image', 'x': lx, 'y': ly, 'src': lamps['off']['src'],
                     'bind': {'src': {'tag': state(i), 'map': {'1': lamps['green']['src'], '3': lamps['yellow']['src'], '2': lamps['red']['src'], 'default': lamps['off']['src']}}},
                     'animate': {'preset': 'blink', 'when': fault(i)}})
    for i in range(8):                                   # the name tags: state colour, quantity; tap = details
        key, name, b, kind, _ = MACHINES[i]
        x0, x1, y0, y1, h = b
        top = {'feeder': 30, 'mixer': 48, 'divider': 24, 'proofing': 10, 'oven': 40, 'cooling': 12, 'slicer': 20, 'packaging': 20}[key]
        p = pt((x0 + x1) / 2, (y0 + y1) / 2, h + top)
        tw, th = 96, 30
        kids.append({'type': 'panel', 'x': int(p[0] - tw / 2 + 8), 'y': int(p[1] - th - 8), 'w': tw, 'h': th, 'scrollable': False,
                     'style': {'bg_color': STATE_BG['0'], 'radius': 6, 'border_width': 1, 'border_color': '#0d1420', 'pad_left': 8, 'pad_right': 8, 'pad_top': 0, 'pad_bottom': 0},
                     'bind': {'bg_color': {'tag': state(i), 'map': STATE_BG}},
                     'on_click': go(i),
                     'children': [
                         label(name, font=14, align='left_mid', bind={'text_color': {'tag': state(i), 'map': STATE_TEXT}}),
                         label(font=16, align='right_mid', bind={'text': {'tag': key + '_qty', 'format': '%d'}, 'text_color': {'tag': state(i), 'map': STATE_TEXT}})]})

    # the side panel
    def row(y, title, tag, fmt):
        return [label(title, font=14, color=MUTED, x=0, y=y), label(font=24, x=0, y=y + 20, bind={'text': {'tag': tag, 'format': fmt}})]
    side = {'type': 'panel', 'x': 992, 'y': 80, 'w': 272, 'h': 544, 'scrollable': False,
            'style': {'bg_color': PANEL, 'radius': 10, 'border_width': 0, 'pad': 16},
            'children': [
                label('LIVE VIEW', font=16, color=ACCENT, x=0, y=0, style={'text_letter_space': 2}),
                {'type': 'panel', 'x': 0, 'y': 32, 'w': 240, 'h': 40, 'scrollable': False,
                 'style': {'bg_color': STATE_BG['0'], 'radius': 20, 'border_width': 0, 'pad': 0},
                 'bind': {'bg_color': {'tag': 'line_state', 'map': {'0': '#3b4a5e', '1': '#36c98f', '2': '#f06464', '3': '#f5b942', 'default': '#3b4a5e'}}},
                 'children': [label(font=18, align='center', bind={'text': {'tag': 'line_state', 'map': {'0': 'LINE STOPPED', '1': 'LINE RUNNING', '2': 'LINE FAULT', '3': 'LINE WARNING'}},
                                                                    'text_color': {'tag': 'line_state', 'map': {'0': TEXT, 'default': '#101827'}}})]},
                *row(92, "Today's production", 'production_count', '%d pcs'),
                *row(156, 'Conveyor', 'conveyor_speed', '%.1f m/min'),
                *row(220, 'Oven', 'oven_temperature', '%.0f °C'),
                *row(284, 'Proofing', 'proofing_temperature', '%.1f °C'),
                *row(348, 'Film left', 'packaging_film_remaining', '%.0f %%'),
                {'type': 'panel', 'x': 0, 'y': 420, 'w': 240, 'h': 2, 'style': {'bg_color': '#2b3a52', 'border_width': 0, 'radius': 0}},
                *[w for k, (word, col) in enumerate((('Running', '#36c98f'), ('Warning', '#f5b942'), ('Fault', '#f06464'), ('Stopped', '#3b4a5e')))
                  for w in ({'type': 'panel', 'x': (k % 2) * 120, 'y': 436 + (k // 2) * 28, 'w': 14, 'h': 14, 'style': {'bg_color': col, 'radius': 7, 'border_width': 0}},
                            label(word, font=14, color=MUTED, x=(k % 2) * 120 + 22, y=433 + (k // 2) * 28))],
                label('Tap a machine for its details.', font=14, color=MUTED, x=0, y=496),
            ]}
    kids.append(side)
    screen = {'name': 'live', 'template': 'frame', 'template_args': {'title': 'Live View'}, 'children': kids}
    with open(os.path.join(PROJ, 'screens', 'live.json'), 'w', encoding='utf-8', newline='\n') as f:
        json.dump(screen, f, ensure_ascii=False, indent=1)
        f.write('\n')

    # the navigation of the template: "Live" after "Process" (once); 10 buttons share the width
    fp = os.path.join(PROJ, 'screens', 'frame.json')
    frame = json.load(open(fp, encoding='utf-8'))
    nav = next(c for c in frame['children'] if c.get('flex') == 'row' and c.get('y') == 640)
    btns = nav['children']
    if not any(b.get('on_click', {}).get('screen') == 'live' for b in btns):
        k = next(n for n, b in enumerate(btns) if b.get('on_click', {}).get('screen') == 'process')
        live = json.loads(json.dumps(btns[k]))
        live['on_click']['screen'] = 'live'
        live['bind']['bg_color']['map'] = {'live': '#19707c', 'default': '#192538'}
        live['children'][0]['src'] = 'assets/icons/24/factory.png'
        live['children'][1]['text'] = 'Live'
        btns.insert(k + 1, live)
    for b in btns:
        if b['children'][1].get('text') == 'Equipment':
            b['children'][1]['text'] = 'Machines'       # (fits the narrower button)
        b['w'] = 118
        b['style']['pad'] = 0
        b['style']['pad_gap'] = 4
    with open(fp, 'w', encoding='utf-8', newline='\n') as f:
        json.dump(frame, f, ensure_ascii=False, indent=1)
        f.write('\n')
    pp = os.path.join(PROJ, 'project.json')
    proj = json.load(open(pp, encoding='utf-8'))
    if 'screens/live.json' not in proj['screens']:
        proj['screens'].insert(proj['screens'].index('screens/process.json') + 1, 'screens/live.json')
        with open(pp, 'w', encoding='utf-8', newline='\n') as f:
            json.dump(proj, f, ensure_ascii=False, indent=2)
            f.write('\n')
    print('written', os.path.join(PROJ, 'screens', 'live.json'), '-', len(os.listdir(ASSETS)), 'pictures')


if __name__ == '__main__':
    build()
