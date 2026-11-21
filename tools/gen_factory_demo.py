"""Draws the pictures of the demo projects/factory (an isometric view of a production line) and
writes its screens. The pictures are simple shaded solids, made by this script: change the
shapes or colours here and run it again.

    python -I tools/gen_factory_demo.py            (needs Pillow: pip install pillow)

Writes projects/factory (800x480) and projects/factory35 (480x320: the same pictures at 0.6 of
their size, for the 3.5" LCD of the Raspberry Pi).

Geometry: a grid cell is 64 x 32 px on the screen (2:1), one step in z is one px.
    screen x = OX + (gx - gy) * 32        screen y = OY + (gx + gy) * 16 - z
"""
import json, os, shutil
from PIL import Image, ImageDraw

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = ASSETS = None                     # set by build()
K = 1.0                                 # the pictures and positions are multiplied by this when written
SS = 4                                  # drawn 4 times as large and scaled down: smooth edges

W, H = 800, 480                         # the size everything is drawn for (before K)
GW, GD = 16, 6                          # the floor: cells along gx and gy
OX, OY = 240, 104                       # the screen position of the grid point (0, 0)
BY0, BY1 = 3.0, 3.6                     # the conveyor runs along gx between these gy
BC = (BY0 + BY1) / 2
BELT_Z = 17                             # the height of its surface
STATIONS = [1.6, 5.6, 9.6, 13.6]        # gx where the four machines work on the conveyor
MACHINE_H = [50, 70, 60, 46]
# Machines 1 and 4 stand over the ends of the conveyor (the products come out of one and go
# into the other); 2 and 3 stand behind it and reach over it with an arm.
BODY = [(1.0, 2.2, 2.6, 4.0), (5.0, 6.2, 1.4, 2.7), (9.0, 10.2, 1.4, 2.7), (13.0, 14.2, 2.6, 4.0)]     # gx0, gx1, gy0, gy1


def pt(gx, gy, z=0.0):
    return (OX + (gx - gy) * 32.0, OY + (gx + gy) * 16.0 - z)


def shade(c, f):
    return tuple(max(0, min(255, int(round(v * f)))) for v in c[:3]) + ((c[3],) if len(c) > 3 else ())


def hexc(s, a=255):
    s = s.lstrip('#')
    return (int(s[0:2], 16), int(s[2:4], 16), int(s[4:6], 16), a)


class Canvas:
    """A picture with its own origin: everything is drawn in screen coordinates, the picture
    is the bounding box given (x0, y0, x1, y1)."""

    def __init__(self, box):
        self.x0, self.y0 = int(box[0]), int(box[1])
        self.w, self.h = int(box[2] - box[0]), int(box[3] - box[1])
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
        self.d.line([self._p(p) for p in pts], fill=fill, width=max(1, int(width * SS)))

    def ellipse(self, cx, cy, rx, ry, fill):
        a, b = self._p((cx - rx, cy - ry)), self._p((cx + rx, cy + ry))
        self._layer(lambda d: d.ellipse([a, b], fill=fill))

    def box(self, gx0, gx1, gy0, gy1, z0, z1, color, edge=True):
        """A solid: the top, the face towards +gy (left on the screen) and towards +gx (right)."""
        top = [pt(gx0, gy0, z1), pt(gx1, gy0, z1), pt(gx1, gy1, z1), pt(gx0, gy1, z1)]
        left = [pt(gx0, gy1, z1), pt(gx1, gy1, z1), pt(gx1, gy1, z0), pt(gx0, gy1, z0)]
        right = [pt(gx1, gy0, z1), pt(gx1, gy1, z1), pt(gx1, gy1, z0), pt(gx1, gy0, z0)]
        ol = shade(color, 0.45) if edge else None
        self.poly(left, shade(color, 0.78), ol)
        self.poly(right, shade(color, 0.6), ol)
        self.poly(top, color, ol)

    def face_x(self, gx, gy0, gy1, z0, z1, color):       # a rectangle on a face towards +gx
        self.poly([pt(gx, gy0, z1), pt(gx, gy1, z1), pt(gx, gy1, z0), pt(gx, gy0, z0)], color)

    def face_y(self, gy, gx0, gx1, z0, z1, color):       # ... towards +gy
        self.poly([pt(gx0, gy, z1), pt(gx1, gy, z1), pt(gx1, gy, z0), pt(gx0, gy, z0)], color)

    def shadow(self, gx0, gx1, gy0, gy1, a=55):          # on the floor, a little to the right
        self.poly([pt(gx0 + 0.15, gy0 + 0.1), pt(gx1 + 0.3, gy0 + 0.1), pt(gx1 + 0.3, gy1 + 0.15), pt(gx0 + 0.15, gy1 + 0.15)], (0, 0, 0, a))

    def save(self, name):
        x0, y0 = int(round(self.x0 * K)), int(round(self.y0 * K))
        w, h = max(1, int(round(self.w * K))), max(1, int(round(self.h * K)))
        im = self.im.resize((w, h), Image.LANCZOS)
        im.save(os.path.join(ASSETS, name), optimize=True)
        return {'x': x0, 'y': y0, 'w': w, 'h': h, 'src': 'assets/' + name}


def bbox(points, pad=3):
    xs = [p[0] for p in points]; ys = [p[1] for p in points]
    return (min(xs) - pad, min(ys) - pad, max(xs) + pad, max(ys) + pad)


def box_points(gx0, gx1, gy0, gy1, z0, z1):
    return [pt(x, y, z) for x in (gx0, gx1) for y in (gy0, gy1) for z in (z0, z1)]


# ---------------------------------------------------------------- the floor (one picture, the whole screen)
def draw_floor():
    c = Canvas((0, 0, W, H))
    for i in range(H):                                                  # the dark of the hall, lighter in the middle
        f = 1.0 - abs(i - H * 0.45) / H
        c.d.line([(0, i * SS), (W * SS, i * SS)], fill=(int(12 + 12 * f), int(18 + 16 * f), int(27 + 24 * f), 255), width=SS)
    c.box(0, GW, 0, GD, -18, 0, hexc('#5d6b7a'), edge=False)           # the slab the hall stands on
    for gx in range(GW):
        for gy in range(GD):
            col = hexc('#75838f') if (gx + gy) % 2 == 0 else hexc('#6c7985')
            c.poly([pt(gx, gy), pt(gx + 1, gy), pt(gx + 1, gy + 1), pt(gx, gy + 1)], col)
    # the walkway in front of the line, the place of the pallets (yellow), the shipping area (blue)
    for gy in (4.35, 5.65):
        c.line([pt(0.3, gy), pt(GW - 0.3, gy)], hexc('#f2c230'), 1.6)
    c.poly([pt(14.5, 2.7), pt(15.75, 2.7), pt(15.75, 3.95), pt(14.5, 3.95)], hexc('#f2c230', 70), hexc('#f2c230'))
    c.poly([pt(0.4, 4.5), pt(2.7, 4.5), pt(2.7, 5.5), pt(0.4, 5.5)], hexc('#4fc3f7', 60), hexc('#4fc3f7'))
    # things along the back wall
    for gx, col, h in ((0.3, '#455a64', 46), (1.4, '#546e7a', 46), (14.6, '#455a64', 42)):       # cabinets
        c.shadow(gx, gx + 0.9, 0.25, 0.9)
        c.box(gx, gx + 0.9, 0.25, 0.9, 0, h, hexc(col))
        c.face_y(0.9, gx + 0.15, gx + 0.75, h - 20, h - 8, hexc('#90caf9'))
        c.face_y(0.9, gx + 0.15, gx + 0.4, 8, 14, hexc('#66bb6a'))
    for gx in (2.9, 3.9):                                               # tanks
        cx, cy = pt(gx + 0.45, 0.65)
        c.ellipse(cx + 6, cy + 3, 24, 11, (0, 0, 0, 55))
        c.ellipse(cx, cy, 20, 10, shade(hexc('#b0bec5'), 0.7))
        c.poly([(cx - 20, cy), (cx + 20, cy), (cx + 20, cy - 48), (cx - 20, cy - 48)], hexc('#b0bec5'))
        c.poly([(cx + 4, cy), (cx + 20, cy), (cx + 20, cy - 48), (cx + 4, cy - 48)], shade(hexc('#b0bec5'), 0.8))
        c.ellipse(cx, cy - 48, 20, 10, hexc('#cfd8dc'))
        c.line([(cx - 20, cy - 20), (cx + 20, cy - 20)], hexc('#1e88e5'), 2)
    for gx in (7.0, 8.0):                                               # crates of material
        c.shadow(gx, gx + 0.8, 0.3, 1.0)
        c.box(gx, gx + 0.8, 0.3, 1.0, 0, 22, hexc('#b08d57'))
        c.box(gx + 0.1, gx + 0.7, 0.35, 0.95, 22, 40, hexc('#c49a6c'))
    c.shadow(11.3, 13.7, 0.25, 0.85)                                    # a shelf
    c.box(11.3, 13.7, 0.25, 0.85, 0, 8, hexc('#37474f'))
    for gx in (11.5, 12.3, 13.1):
        c.box(gx, gx + 0.5, 0.3, 0.8, 8, 22, hexc('#ef9a50'))
    c.box(11.3, 13.7, 0.25, 0.85, 26, 30, hexc('#37474f'))
    for gx in (11.5, 12.3, 13.1):
        c.box(gx, gx + 0.5, 0.3, 0.8, 30, 44, hexc('#4db6ac'))
    # the conveyor: shadow, legs, frame
    c.shadow(1.0, 14.0, BY0, BY1, 45)
    for gx in range(1, 14):
        c.box(gx + 0.42, gx + 0.58, BY0 + 0.05, BY1 - 0.05, 0, 12, hexc('#37474f'), edge=False)
    c.box(1.0, 14.1, BY0 - 0.06, BY1 + 0.06, 12, BELT_Z, hexc('#546e7a'))
    return c.save('floor.png')


def draw_belt(frame, frames):
    """The moving surface of the conveyor: dark, with light marks that move from frame to frame."""
    z = BELT_Z + 0.5
    c = Canvas(bbox([pt(1.0, BY1, z), pt(14.1, BY0, z), pt(1.0, BY0, z), pt(14.1, BY1, z)]))
    c.poly([pt(1.05, BY0, z), pt(14.05, BY0, z), pt(14.05, BY1, z), pt(1.05, BY1, z)], hexc('#263238'))
    step = 0.5
    gx = 1.05 + step * frame / frames
    while gx < 14.0:
        c.line([pt(gx, BY0 + 0.04, z), pt(gx, BY1 - 0.04, z)], hexc('#607d8b'), 1.4)
        gx += step
    return c.save('belt%d.png' % frame)


def draw_product(kind):
    """A product on the conveyor, drawn around the grid point (0, 0)."""
    col = {'raw': '#cfd8dc', 'made': '#42a5f5', 'ok': '#66bb6a'}[kind]
    z = BELT_Z + 1
    c = Canvas(bbox(box_points(-0.2, 0.2, -0.2, 0.2, z, z + 14)))
    c.box(-0.2, 0.2, -0.2, 0.2, z, z + 14, hexc(col))
    if kind == 'ok':
        c.line([pt(-0.09, 0.0, z + 14), pt(-0.01, 0.08, z + 14), pt(0.11, -0.09, z + 14)], (255, 255, 255, 255), 1.3)
    return c.save('product_%s.png' % kind)


ACCENT = ['#ff9800', '#1e88e5', '#8e24aa', '#00897b']


def draw_machine(i):
    x0, x1, y0, y1 = BODY[i]
    h = MACHINE_H[i]
    body, accent = hexc('#e8ecef'), hexc(ACCENT[i])
    c = Canvas(bbox(box_points(x0, x1 + 0.6, y0, y1 + 0.2, 0, h + 34)))
    c.shadow(x0, x1, y0, y1)
    c.box(x0, x1, y0, y1, 0, h, body)
    c.box(x0, x1, y0, y1, h, h + 5, accent)                                         # the coloured roof
    c.face_y(y1, x0 + 0.15, x1 - 0.15, 5, 9, accent)
    if i in (0, 3):
        # the opening of the conveyor (on the face towards +gx for the loader; the unloader takes
        # the products in on the far side, so its opening is not seen) and a window on the front
        if i == 0:
            c.face_x(x1, BY0 - 0.05, BY1 + 0.05, 12, 40, hexc('#1c252c'))
        c.face_y(y1, x0 + 0.2, x1 - 0.2, h - 28, h - 10, hexc('#263238'))
        c.face_y(y1, x0 + 0.25, x1 - 0.25, h - 26, h - 12, hexc('#4dd0e1', 150))
    else:
        c.face_y(y1, x0 + 0.2, x1 - 0.2, h - 34, h - 12, hexc('#263238'))           # a screen on the front
        c.face_y(y1, x0 + 0.25, x1 - 0.25, h - 32, h - 14, hexc('#4dd0e1', 150))
        c.face_y(y1, x0 + 0.25, x0 + 0.5, 16, 24, hexc('#66bb6a'))
        c.face_y(y1, x0 + 0.6, x0 + 0.85, 16, 24, hexc('#ef5350'))
    if i == 0:                                                                      # the loader: a hopper on top
        t, g, cy = h + 5, (x0 + x1) / 2, (y0 + y1) / 2
        c.poly([pt(g - 0.25, cy + 0.3, t), pt(g + 0.25, cy + 0.3, t), pt(g + 0.45, cy + 0.5, t + 20), pt(g - 0.45, cy + 0.5, t + 20)], shade(accent, 0.8))
        c.poly([pt(g + 0.25, cy - 0.3, t), pt(g + 0.25, cy + 0.3, t), pt(g + 0.45, cy + 0.5, t + 20), pt(g + 0.45, cy - 0.5, t + 20)], shade(accent, 0.62))
        c.poly([pt(g - 0.45, cy - 0.5, t + 20), pt(g + 0.45, cy - 0.5, t + 20), pt(g + 0.45, cy + 0.5, t + 20), pt(g - 0.45, cy + 0.5, t + 20)], hexc('#4e342e'))
    if i == 3:                                                                      # the unloader: a chute to the pallet
        c.poly([pt(x1, BY0, 30), pt(x1, BY1, 30), pt(x1 + 0.45, BY1, 12), pt(x1 + 0.45, BY0, 12)], hexc('#78909c'), hexc('#455a64'))
    return c.save('machine%d.png' % (i + 1))


def draw_arm(i):
    """What machine 2 / 3 holds over the conveyor (drawn after the products: it is above them)."""
    x0, x1, y0, y1 = BODY[i]
    g = STATIONS[i]
    accent = hexc(ACCENT[i])
    z1 = MACHINE_H[i] - 4
    c = Canvas(bbox(box_points(g - 0.3, g + 0.3, y1, BY1 + 0.1, 30, z1 + 2)))
    c.box(g - 0.22, g + 0.22, y1, BY1 + 0.05, z1 - 9, z1, accent)                   # the beam
    if i == 2:                                                                      # the camera under it
        c.box(g - 0.14, g + 0.14, BC - 0.14, BC + 0.14, z1 - 19, z1 - 9, hexc('#37474f'))
        c.ellipse(*pt(g, BC + 0.14, z1 - 15), 3, 3, hexc('#e040fb'))
    return c.save('arm%d.png' % (i + 1))


def draw_press_head():
    """The part of the press that moves: drawn in its upper position."""
    g = STATIONS[1]
    z1 = MACHINE_H[1] - 13
    c = Canvas(bbox(box_points(g - 0.2, g + 0.2, BC - 0.2, BC + 0.2, z1 - 20, z1)))
    c.box(g - 0.06, g + 0.06, BC - 0.06, BC + 0.06, z1 - 12, z1, hexc('#cfd8dc'))
    c.box(g - 0.18, g + 0.18, BC - 0.18, BC + 0.18, z1 - 18, z1 - 12, hexc('#90a4ae'))
    return c.save('press_head.png')


def draw_scan():
    """The light of the inspection on the product under the camera."""
    cx, cy = pt(STATIONS[2], BC, BELT_Z + 16)
    c = Canvas((cx - 24, cy - 14, cx + 24, cy + 14))
    for r, a in ((22, 45), (15, 90), (9, 150), (4, 255)):
        c.ellipse(cx, cy, r, r / 2.0, (224, 64, 251, a))
    return c.save('scan.png')


def draw_lamp(kind):
    """The signal tower of a machine, drawn around the grid point (0, 0, 0): three lights, one lit."""
    lit = {'off': None, 'green': 0, 'yellow': 1, 'red': 2}[kind]
    cols = [hexc('#43a047'), hexc('#fdd835'), hexc('#e53935')]
    cx, cy = pt(0, 0)
    c = Canvas((cx - 14, cy - 44, cx + 14, cy + 4))
    c.poly([(cx - 1.5, cy), (cx + 1.5, cy), (cx + 1.5, cy - 12), (cx - 1.5, cy - 12)], hexc('#455a64'))
    for k in range(3):
        y = cy - 12 - k * 9
        on = lit == k
        col = cols[k] if on else shade(cols[k], 0.35)
        if on:
            c.ellipse(cx, y - 4.5, 13, 10, cols[k][:3] + (80,))
        c.poly([(cx - 5, y), (cx + 5, y), (cx + 5, y - 8), (cx - 5, y - 8)], col)
        c.poly([(cx + 1, y), (cx + 5, y), (cx + 5, y - 8), (cx + 1, y - 8)], shade(col, 0.8))
    return c.save('lamp_%s.png' % kind)


def draw_alert(i):
    """A red frame on the floor around a machine (pulses while the machine has a fault)."""
    x0, x1, y0, y1 = BODY[i]
    pts = [pt(x0 - 0.3, y0 - 0.3), pt(x1 + 0.3, y0 - 0.3), pt(x1 + 0.3, y1 + 0.3), pt(x0 - 0.3, y1 + 0.3)]
    c = Canvas(bbox(pts, 4))
    c.poly(pts, (229, 57, 53, 80))
    c.line(pts + [pts[0]], (255, 82, 82, 255), 2.5)
    return c.save('alert%d.png' % (i + 1))


PALLET = (15.12, 3.32)


def draw_pallet(n):
    """The pallet behind the unloader with n boxes on it (0..4)."""
    gx, gy = PALLET
    c = Canvas(bbox(box_points(gx - 0.6, gx + 0.6, gy - 0.6, gy + 0.6, 0, 26)))
    c.box(gx - 0.5, gx + 0.5, gy - 0.5, gy + 0.5, 0, 6, hexc('#8d6e63'))
    places = [(-0.24, -0.24), (0.24, -0.24), (-0.24, 0.24), (0.24, 0.24)]
    for k in sorted(range(4), key=lambda k: places[k][0] + places[k][1]):          # the far ones first
        if k < n:
            px, py = places[k]
            c.box(gx + px - 0.2, gx + px + 0.2, gy + py - 0.2, gy + py + 0.2, 6, 20, hexc('#66bb6a'))
    return c.save('pallet%d.png' % n)


def draw_agv(loaded):
    """The transport vehicle, drawn around the grid point (0, 0)."""
    c = Canvas(bbox(box_points(-0.55, 0.7, -0.4, 0.5, 0, 34)))
    c.shadow(-0.5, 0.45, -0.32, 0.3, 70)
    c.box(-0.45, 0.45, -0.3, 0.3, 3, 13, hexc('#ffb300'))
    c.box(-0.4, 0.4, -0.25, 0.25, 13, 15, hexc('#37474f'))
    c.face_y(0.3, 0.2, 0.4, 6, 10, hexc('#e53935'))
    c.face_y(0.3, -0.4, -0.2, 6, 10, hexc('#e53935'))
    if loaded:
        c.box(-0.32, -0.02, -0.2, 0.2, 15, 29, hexc('#66bb6a'))
        c.box(0.04, 0.34, -0.2, 0.2, 15, 29, hexc('#66bb6a'))
    return c.save('agv_%s.png' % ('full' if loaded else 'empty'))


# ---------------------------------------------------------------- the project
INK = '#eceff1'
STATE_COLORS = {'0': '#455a64', '1': '#2e7d32', '2': '#f9a825', '3': '#c62828', 'default': '#455a64'}
NAMES = ['投入', '加工', '検査', '搬出']


def flat(**kw):
    s = {'radius': 0, 'border_width': 0, 'pad': 0, 'bg_opa': 0}
    s.update(kw)
    return s


def image(a, **kw):
    w = {'type': 'image', 'x': a['x'], 'y': a['y'], 'src': a['src']}
    w.update(kw)
    return w


def label(text=None, tag=None, **kw):
    w = {'type': 'label', 'style': {'font': kw.pop('font', 16), 'text_color': kw.pop('color', INK)}}
    if text is not None: w['text'] = text
    if tag is not None: w['bind'] = {'text': tag} if not isinstance(tag, dict) else {'text': tag}
    w['style'].update(kw.pop('style', {}))
    w.update(kw)
    return w


def build(small=False):
    global OUT, ASSETS, K, W, H, OY
    # The 480x320 screen: everything at 0.6 (800 x 0.6 = 480). 320 / 0.6 = 533 is more than the
    # 480 the scene needs, so it is moved down a little.
    K, W, H, OY = (0.6, 800, 534, 126) if small else (1.0, 800, 480, 104)
    OUT = os.path.join(ROOT, 'projects', 'factory35' if small else 'factory')
    ASSETS = os.path.join(OUT, 'assets')
    SW, SH = int(round(W * K)), int(round(H * K))           # the size of the screen
    def sc(v): return int(round(v * K))
    shutil.rmtree(ASSETS, ignore_errors=True)
    os.makedirs(os.path.join(ASSETS, 'fonts'))
    os.makedirs(os.path.join(OUT, 'screens'), exist_ok=True)
    os.makedirs(os.path.join(OUT, 'scripts'), exist_ok=True)
    for f in ('NotoSansJP-subset.otf', 'OFL.txt', 'README.txt'):
        shutil.copyfile(os.path.join(ROOT, 'projects', 'demo', 'assets', 'fonts', f), os.path.join(ASSETS, 'fonts', f))

    FRAMES = 4
    floor = draw_floor()
    belts = [draw_belt(k, FRAMES) for k in range(FRAMES)]
    products = {k: draw_product(k) for k in ('raw', 'made', 'ok')}
    machines = [draw_machine(i) for i in range(4)]
    arms = {i: draw_arm(i) for i in (1, 2)}
    head = draw_press_head()
    scan = draw_scan()
    lamps = {k: draw_lamp(k) for k in ('off', 'green', 'yellow', 'red')}
    alerts = [draw_alert(i) for i in range(4)]
    pallets = [draw_pallet(n) for n in range(5)]
    agv = {False: draw_agv(False), True: draw_agv(True)}

    def at(a, gx, gy, z=0.0):
        """Where a picture drawn around the grid point (0, 0) goes to stand at (gx, gy, z)."""
        o = pt(0, 0)
        p = pt(gx, gy, z)
        return int(round(a['x'] + (p[0] - o[0]) * K)), int(round(a['y'] + (p[1] - o[1]) * K))

    def state(i): return 'm%d_state' % (i + 1)
    def open_machine(i): return {'action': 'popup', 'screen': 'machine', 'set': {'sel': i + 1}}

    running = 'line_run'
    CYCLE = 3000
    kids = [image(floor)]
    # (from the back to the front: what is written later is drawn over what came before)
    for i in (1, 2):
        fault = {'tag': state(i), 'eq': 3}
        kids.append(image(alerts[i], bind={'visible': fault}, animate={'preset': 'pulse', 'when': fault}))
        kids.append(image(machines[i], on_click=open_machine(i)))
    kids.append(image(belts[0], animate={'prop': 'frames', 'files': [b['src'] for b in belts], 'time': 400, 'when': running}))

    # The products: one for each stretch between two stations. All of them move at the same time
    # from one station to the next and start again. At a station the product that arrives is
    # replaced by the one that leaves (grey -> blue -> green): it looks like one steady flow of
    # products that are worked on. The two ends are inside machines 1 and 4.
    for k, kind in enumerate(('raw', 'made', 'ok')):
        a = products[kind]
        x0, y0 = at(a, STATIONS[k], BC)
        x1, y1 = at(a, STATIONS[k + 1], BC)
        kids.append({'type': 'image', 'x': x0, 'y': y0, 'src': a['src'], 'animate': [
            {'prop': 'x', 'from': x0, 'to': x1, 'time': CYCLE, 'path': 'linear', 'when': running},
            {'prop': 'y', 'from': y0, 'to': y1, 'time': CYCLE, 'path': 'linear', 'when': running}]})

    # over the conveyor: the press (down on the product when it arrives, up while it leaves) and the camera
    on2, on3 = {'tag': state(1), 'eq': 1}, {'tag': state(2), 'eq': 1}
    kids.append(image(head, animate={'prop': 'y', 'from': head['y'] + sc(13), 'to': head['y'], 'time': CYCLE // 2, 'playback': True,
                                     'path': 'ease_in_out', 'when': on2}))
    kids.append(image(arms[1]))
    kids.append(image(scan, bind={'visible': on3}, animate={'preset': 'pulse', 'time': 375, 'when': on3}))
    kids.append(image(arms[2]))

    for i in (0, 3):
        fault = {'tag': state(i), 'eq': 3}
        kids.append(image(alerts[i], bind={'visible': fault}, animate={'preset': 'pulse', 'when': fault}))
        kids.append(image(machines[i], on_click=open_machine(i)))
    kids.append(image(pallets[0], bind={'src': {'tag': 'pallet', 'map': dict([(str(n), pallets[n]['src']) for n in range(5)] + [('default', pallets[0]['src'])])}}))

    for i in range(4):                                       # the signal towers, on the roofs
        x0, x1, y0, y1 = BODY[i]
        lx, ly = at(lamps['off'], x0 + 0.15, y1 - 0.15, MACHINE_H[i] + 5)
        fault = {'tag': state(i), 'eq': 3}
        kids.append({'type': 'image', 'x': lx, 'y': ly, 'src': lamps['off']['src'],
                     'bind': {'src': {'tag': state(i), 'map': {'1': lamps['green']['src'], '2': lamps['yellow']['src'], '3': lamps['red']['src'],
                                                                'default': lamps['off']['src']}}},
                     'animate': {'preset': 'blink', 'when': fault}})

    # the vehicle: takes a full pallet to the shipping area (blue) and comes back empty
    AGV_Y = 5.0
    home, away = (14.4, AGV_Y), (1.6, AGV_Y)
    for loaded in (True, False):
        a = agv[loaded]
        (x0, y0), (x1, y1) = at(a, *home), at(a, *away)
        if not loaded: x0, y0, x1, y1 = x1, y1, x0, y0
        cond = {'tag': 'agv', 'eq': 1 if loaded else 2}
        kids.append({'type': 'image', 'x': x0, 'y': y0, 'src': a['src'], 'bind': {'visible': cond}, 'animate': [
            {'prop': 'x', 'from': x0, 'to': x1, 'time': 5000, 'repeat': 1, 'path': 'ease_in_out', 'when': cond},
            {'prop': 'y', 'from': y0, 'to': y1, 'time': 5000, 'repeat': 1, 'path': 'ease_in_out', 'when': cond}]})
    x0, y0 = at(agv[False], *home)
    kids.append({'type': 'image', 'x': x0, 'y': y0, 'src': agv[False]['src'], 'bind': {'visible': {'tag': 'agv', 'eq': 0}}})

    # the tags above the machines: name, state colour, pieces
    for i in range(4):
        x0, x1, y0, y1 = BODY[i]
        p = pt((x0 + x1) / 2, (y0 + y1) / 2, MACHINE_H[i] + (34 if i == 0 else 16))
        tw, th = (76, 24) if small else (100, 30)
        kids.append({'type': 'panel', 'x': sc(p[0]) - (10 if small else 16), 'y': sc(p[1]) - th, 'w': tw, 'h': th, 'scrollable': False,
                     'style': flat(bg_opa=255, bg_color='#455a64', radius=6, border_width=1, border_color='#0d1117',
                                   pad_left=5 if small else 8, pad_right=5 if small else 8),
                     'bind': {'bg_color': {'tag': state(i), 'map': STATE_COLORS}},
                     'on_click': open_machine(i),
                     'children': [
                         label(NAMES[i], font=14, color='#ffffff', align='left_mid'),
                         label(tag={'tag': 'm%d_count' % (i + 1), 'format': '%d'}, font=14 if small else 16, color='#ffffff', align='right_mid')]})

    # the bar at the top and the controls
    def count(title, tag, x, color='#ffffff'):
        if small:
            return [label(title, font=14, color='#90a4ae', align='left_mid', x=x),
                    label(tag={'tag': tag, 'format': '%d'}, font=16, color=color, align='left_mid', x=x + 32)]
        return [label(title, font=14, color='#90a4ae', align='left_mid', x=x),
                label(tag={'tag': tag, 'format': '%d'}, font=24, color=color, align='left_mid', x=x + (52 if len(title) > 2 else 36))]
    if small:
        bar = [label('第1ライン', font=16, color='#ffffff', align='left_mid', x=8),
               {'type': 'panel', 'align': 'left_mid', 'x': 88, 'w': 68, 'h': 24, 'scrollable': False,
                'style': flat(bg_opa=255, bg_color='#455a64', radius=5),
                'bind': {'bg_color': {'tag': 'line_state', 'map': STATE_COLORS}},
                'children': [label(tag='line_text', font=14, color='#ffffff', align='center')]},
               *count('生産', 'm4_count', 168), *count('不良', 'ng_count', 248, '#ff8a80'), *count('出荷', 'shipped', 320),
               label(tag='clock', font=14, color='#cfd8dc', align='right_mid', x=-8)]
        kids.append({'type': 'panel', 'x': 0, 'y': 0, 'w': SW, 'h': 32, 'scrollable': False, 'style': flat(bg_opa=255, bg_color='#161d27'), 'children': bar})
        kids.append({'type': 'panel', 'x': 8, 'y': SH - 56, 'w': 152, 'h': 48, 'scrollable': False,
                     'style': flat(bg_opa=255, bg_color='#161d27', radius=8, pad_left=10, pad_right=2),
                     'children': [label('運転', font=16, color='#ffffff', align='left_mid'),
                                  {'type': 'switch', 'align': 'right_mid', 'w': 84, 'h': 44, 'bind': 'run',
                                   'style_indicator_checked': {'bg_color': '#2e7d32'}}]})
    else:
        bar = [label('第1ライン', font=20, color='#ffffff', align='left_mid', x=16),
               {'type': 'panel', 'align': 'left_mid', 'x': 124, 'w': 96, 'h': 32, 'scrollable': False,
                'style': flat(bg_opa=255, bg_color='#455a64', radius=6),
                'bind': {'bg_color': {'tag': 'line_state', 'map': STATE_COLORS}},
                'children': [label(tag='line_text', font=16, color='#ffffff', align='center')]},
               *count('生産数', 'm4_count', 244), *count('不良', 'ng_count', 384, '#ff8a80'), *count('出荷', 'shipped', 484),
               label(tag='clock', font=20, color='#cfd8dc', align='right_mid', x=-16)]
        kids.append({'type': 'panel', 'x': 0, 'y': 0, 'w': SW, 'h': 48, 'scrollable': False, 'style': flat(bg_opa=255, bg_color='#161d27'), 'children': bar})
        kids.append(label('機械に触れると詳細が開きます', font=14, color='#90a4ae', x=20, y=380))
        kids.append({'type': 'panel', 'x': 16, 'y': 408, 'w': 216, 'h': 56, 'scrollable': False,
                     'style': flat(bg_opa=255, bg_color='#161d27', radius=8, pad_left=12, pad_right=4),
                     'children': [label('運転', font=18, color='#ffffff', align='left_mid'),
                                  {'type': 'switch', 'align': 'right_mid', 'w': 96, 'h': 48, 'bind': 'run',
                                   'style_indicator_checked': {'bg_color': '#2e7d32'}}]})

    screen = {'name': 'line', 'scrollable': False, 'style': {'bg_color': '#0e141b', 'pad': 0}, 'children': kids}

    def row(y, title, tag, fmt=None):
        t = tag if fmt is None else {'tag': tag, 'format': fmt}
        return [label(title, font=16, color='#90a4ae', x=0, y=y), label(tag=t, font=20, color='#ffffff', x=150, y=y - 3)]

    step, top = (32, 44) if small else (40, 60)              # the rows of values
    by = top + step * 3 + (16 if small else 24)               # the two buttons
    bw = 186
    popup = {'name': 'machine', 'popup': True, 'w': 420, 'h': by + 56 + 8 + 48 + 32, 'dim': True,
             'style': {'bg_color': '#1f2a38', 'radius': 10, 'pad': 16, 'border_width': 0},
             'children': [
                 label(tag='sel_name', font=24, color='#ffffff', x=0, y=0),
                 {'type': 'panel', 'align': 'top_right', 'w': 120, 'h': 36, 'scrollable': False,
                  'style': flat(bg_opa=255, bg_color='#455a64', radius=6),
                  'bind': {'bg_color': {'tag': 'sel_state', 'map': STATE_COLORS}},
                  'children': [label(tag='sel_text', font=16, color='#ffffff', align='center')]},
                 *row(top, '処理数', 'sel_count', '%d 個'),
                 *row(top + step, 'サイクル', 'sel_cycle', '%.1f 秒'),
                 *row(top + step * 2, '稼働率', 'sel_avail', '%d %%'),
                 {'type': 'bar', 'x': 0, 'y': top + step * 3 - (0 if small else 4), 'w': 388, 'h': 10, 'min': 0, 'max': 100, 'bind': {'value': 'sel_avail'},
                  'style_indicator': {'bg_color': '#2e7d32'}},
                 {'type': 'button', 'text': '故障を起こす', 'x': 0, 'y': by, 'w': bw, 'h': 56, 'style': {'bg_color': '#c62828'},
                  'on_click': {'action': 'set', 'tag': 'cmd', 'value': 1}, 'bind': {'enabled': {'tag': 'sel_state', 'ne': 3}}},
                 {'type': 'button', 'text': '復旧する', 'x': 388 - bw, 'y': by, 'w': bw, 'h': 56, 'style': {'bg_color': '#2e7d32'},
                  'on_click': {'action': 'set', 'tag': 'cmd', 'value': 2}, 'bind': {'enabled': {'tag': 'sel_state', 'eq': 3}}},
                 {'type': 'button', 'text': '閉じる', 'align': 'bottom_right', 'w': 120, 'h': 48, 'style': {'bg_color': '#455a64'},
                  'on_click': {'action': 'close'}},
             ]}

    def n(name, init=0, lo=None, hi=None):
        t = {'name': name, 'type': 'int', 'init': init}
        if lo is not None: t['min'], t['max'] = lo, hi
        return t

    def s(name): return {'name': name, 'type': 'string', 'init': ''}
    tags = [{'name': 'run', 'type': 'bool', 'init': True}, {'name': 'line_run', 'type': 'bool', 'init': False},
            n('line_state'), s('line_text'), n('ng_count'), n('shipped'), n('pallet', 0, 0, 4), n('agv', 0, 0, 2), s('clock'),
            n('sel', 1, 1, 4), s('sel_name'), n('sel_state'), s('sel_text'), n('sel_count'),
            {'name': 'sel_cycle', 'type': 'float', 'init': 0}, n('sel_avail'), n('cmd', 0, 0, 2)]
    for i in range(1, 5):
        tags += [n('m%d_state' % i), n('m%d_count' % i)]
    project = {
        'title': '生産ライン監視（デモ）',
        'display': {'width': SW, 'height': SH, 'zoom': 1},
        'theme': {'dark': True, 'primary': '#1565c0', 'secondary': '#ff9800', 'font': 16},
        'font': {'file': 'assets/fonts/NotoSansJP-subset.otf'},
        'start_screen': 'line',
        'tags': tags,
        'drivers': [],
        'scripts': [{'name': 'line', 'trigger': 'interval', 'interval_ms': 200, 'file': 'scripts/line.lua'}],
        'screens': ['screens/line.json', 'screens/machine.json'],
    }
    shutil.copyfile(os.path.join(ROOT, 'projects', 'factory', 'scripts', 'line.lua'), os.path.join(OUT, 'scripts', 'line.lua')) if small else None
    if small:      # the LCD of the Raspberry Pi: its framebuffer and its touch panel (as in projects/lcd35)
        project['display'] = {'width': SW, 'height': SH, 'fbdev': '/dev/fb0', 'touch': {'swap_axes': True, 'calibration': [150, 3850, 3850, 220], 'jitter': 12, 'smooth': 3, 'settle': 1, 'release': 1, 'lift_ms': 60, 'scroll_limit': 8}}
    for path, obj in (('project.json', project), ('screens/line.json', screen), ('screens/machine.json', popup)):
        with open(os.path.join(OUT, path), 'w', encoding='utf-8', newline='\n') as f:
            json.dump(obj, f, ensure_ascii=False, indent=2)
            f.write('\n')
    print('written', OUT, '-', len(os.listdir(ASSETS)) - 1, 'pictures')


if __name__ == '__main__':
    build()
    build(small=True)
