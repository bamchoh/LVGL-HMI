"""Fetch aiosmtpd and what it needs (pure Python wheels) from PyPI and unpack them into ./pkgs.
For the mail check only: a mail server that is not written by us."""
import json, os, urllib.request, zipfile, io, sys
out = os.path.join(os.path.dirname(os.path.abspath(__file__)) if len(sys.argv) < 2 else sys.argv[1], 'pkgs')
os.makedirs(out, exist_ok=True)
for name in (sys.argv[2:] or ['aiosmtpd', 'atpublic', 'attrs']):
    meta = json.load(urllib.request.urlopen('https://pypi.org/pypi/%s/json' % name, timeout=30))
    wheels = [u for u in meta['urls'] if u['packagetype'] == 'bdist_wheel' and u['filename'].endswith('none-any.whl')]
    if not wheels:
        print('no pure wheel for', name); sys.exit(1)
    data = urllib.request.urlopen(wheels[0]['url'], timeout=60).read()
    zipfile.ZipFile(io.BytesIO(data)).extractall(out)
    print(name, meta['info']['version'], len(data), 'bytes')
