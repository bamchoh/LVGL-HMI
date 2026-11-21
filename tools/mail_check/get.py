"""Fetch pure Python packages from PyPI and unpack them into <folder>/pkgs - for the checks only:
servers that are not written by us.

  python3 get.py <folder> [package ...]      (default: aiosmtpd and what it needs)

A wheel is taken where there is one; else the source archive, of which only the package's own
folder is unpacked (nothing of it is run here)."""
import io, json, os, sys, tarfile, urllib.request, zipfile
out = os.path.join(os.path.dirname(os.path.abspath(__file__)) if len(sys.argv) < 2 else sys.argv[1], 'pkgs')
os.makedirs(out, exist_ok=True)
for name in (sys.argv[2:] or ['aiosmtpd', 'atpublic', 'attrs']):
    meta = json.load(urllib.request.urlopen('https://pypi.org/pypi/%s/json' % name, timeout=30))
    wheels = [u for u in meta['urls'] if u['packagetype'] == 'bdist_wheel' and u['filename'].endswith('none-any.whl')]
    sdists = [u for u in meta['urls'] if u['packagetype'] == 'sdist' and u['filename'].endswith('.tar.gz')]
    if wheels:
        data = urllib.request.urlopen(wheels[0]['url'], timeout=60).read()
        zipfile.ZipFile(io.BytesIO(data)).extractall(out)
        print(name, meta['info']['version'], len(data), 'bytes (wheel)')
        continue
    if not sdists:
        print('no pure wheel and no source archive for', name); sys.exit(1)
    data = urllib.request.urlopen(sdists[0]['url'], timeout=60).read()
    pkg = name.lower().replace('-', '_')
    n = 0
    with tarfile.open(fileobj=io.BytesIO(data), mode='r:gz') as tar:
        for m in tar.getmembers():
            parts = m.name.split('/')
            # <name>-<version>/<pkg>/...  or  <name>-<version>/src/<pkg>/...
            at = 1 if len(parts) > 2 and parts[1] == pkg else 2 if len(parts) > 3 and parts[1] == 'src' and parts[2] == pkg else 0
            if not at or not m.isfile() or '..' in parts or m.name.startswith('/'): continue
            rel = os.path.join(*parts[at:])
            dest = os.path.join(out, rel)
            os.makedirs(os.path.dirname(dest), exist_ok=True)
            with open(dest, 'wb') as f: f.write(tar.extractfile(m).read())
            n += 1
    if n == 0:
        print('the source archive of', name, 'has no folder', pkg); sys.exit(1)
    print(name, meta['info']['version'], len(data), 'bytes (source archive, %d files)' % n)
