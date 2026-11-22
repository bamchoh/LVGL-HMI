"""Records a project running, as an animated GIF: runs a copy of it (the logs and settings of the
real one are not touched) with timed taps (--click) and screenshots (--shot-at), marks each tap
with a white ring and joins the pictures.

    python -I tools/record_gif.py <project dir> <out.gif> <end ms> <step ms> <scale> [<x,y,ms>...]

Needs Pillow and a built runtime (build/bin/hml_runtime). The GIFs in projects/*/renders were
made with it, for example
    python -I tools/record_gif.py projects/factory projects/factory/renders/demo.gif 30000 100 1 355,190,7000 299,314,9000
"""
import os, shutil, subprocess, sys, tempfile
from PIL import Image, ImageDraw

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def main():
    if len(sys.argv) < 6:
        print(__doc__); return 2
    src, out, end, step, scale = sys.argv[1], sys.argv[2], int(sys.argv[3]), int(sys.argv[4]), float(sys.argv[5])
    taps = [tuple(int(v) for v in a.split(',')) for a in sys.argv[6:]]
    tmp = os.path.join(tempfile.gettempdir(), 'hml-record-' + os.path.basename(os.path.abspath(src)))
    proj, shots = os.path.join(tmp, 'p'), os.path.join(tmp, 'shots')
    shutil.rmtree(tmp, ignore_errors=True)
    shutil.copytree(src, proj, ignore=shutil.ignore_patterns('renders', 'logs'))
    os.makedirs(shots)
    exe = os.path.join(ROOT, 'build', 'bin', 'hml_runtime.exe' if os.name == 'nt' else 'hml_runtime')
    args = [exe, os.path.join(proj, 'project.json')]
    for x, y, t in taps:
        args += ['--click', f'{x},{y},{t}']
    for t in range(step, end, step):
        args += ['--shot-at', f'{t},{os.path.join(shots, "%06d.png" % t)}']
    args += ['--exit-after', str(end + 500)]
    r = subprocess.run(args, capture_output=True, text=True, encoding='utf-8', errors='replace')
    got = sorted(f for f in os.listdir(shots) if f.endswith('.png'))
    print('frames', len(got), 'exit', r.returncode)
    if not got:
        print(r.stdout[-3000:], r.stderr[-3000:]); return 1
    frames = []
    for f in got:
        t = int(f[:6])
        im = Image.open(os.path.join(shots, f)).convert('RGB')
        d = ImageDraw.Draw(im)
        for x, y, tt in taps:                   # the touch, for a moment
            if 0 <= t - tt < 600:
                rr = 16 + (t - tt) // 30
                d.ellipse([x - rr, y - rr, x + rr, y + rr], outline=(255, 255, 255), width=4)
        if scale != 1:
            im = im.resize((int(im.width * scale), int(im.height * scale)), Image.LANCZOS)
        frames.append(im.quantize(colors=160, method=Image.Quantize.MEDIANCUT))
    frames[0].save(out, save_all=True, append_images=frames[1:], duration=step, loop=0, optimize=True)
    shutil.rmtree(tmp, ignore_errors=True)
    print(out, os.path.getsize(out) // 1024, 'KB')
    return 0


if __name__ == '__main__':
    sys.exit(main())
