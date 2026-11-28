#!/usr/bin/env python3
"""Put together what is passed on of a build: the programs, what the designer needs beside it,
two example projects, and the licences. Used by .github/workflows/release.yml; can be run by hand.

    python -I tools/package.py --build build --kind windows --out dist/tsukuraku-1.0.0-windows-x64
    python -I tools/package.py --build build-fbdev-aarch64 --kind linux --out dist/tsukuraku-1.0.0-linux-aarch64

windows: hml_designer.exe and hml_runtime.exe with designer/, skills/, fonts/, icons/
linux:   hml_runtime with tools/rpi (the systemd units and scripts for a Raspberry Pi)
both:    examples/ (projects/demo, projects/lcd35), README.md, README.ja.md, README.zh-CN.md, CHANGELOG.md, docs/, LICENSE, NOTICE and
         licenses/ - the licence text of every third-party component that the build downloaded
         (taken from <build>/_deps/<name>-src), which has to go along with the binaries.
"""
import argparse
import os
import re
import shutil
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# not linked into what is passed on (tests only), or generated copies of another one
SKIP = {'doctest', 'open62541_c', 'open62541_h'}
LICENSE_NAME = re.compile(r'^(licen[cs]e|copying|copyright|notice)', re.I)


def copy_tree(src, dst, ignore=()):
    shutil.copytree(src, dst, ignore=shutil.ignore_patterns('__pycache__', 'logs', 'pki', 'project.local.json', '*.db', *ignore), dirs_exist_ok=True)


def third_party_licenses(build, out):
    deps = os.path.join(build, '_deps')
    os.makedirs(out, exist_ok=True)
    found = []
    for d in sorted(os.listdir(deps)) if os.path.isdir(deps) else []:
        if not d.endswith('-src'):
            continue
        name = d[:-4]
        if name in SKIP:
            continue
        src = os.path.join(deps, d)
        files = [f for f in sorted(os.listdir(src)) if LICENSE_NAME.match(f) and os.path.isfile(os.path.join(src, f))]
        for f in files:
            shutil.copyfile(os.path.join(src, f), os.path.join(out, name + '-' + f))
        if name == 'lua' and not files:
            # Lua keeps its licence (MIT) at the end of lua.h
            text = open(os.path.join(src, 'src', 'lua.h'), encoding='utf-8', errors='replace').read()
            m = re.search(r'/\*{10,}\s*\n\* Copyright \(C\).*?\*{10,}/', text, re.S)
            if not m:
                sys.exit('package: the licence block of Lua was not found in lua.h')
            open(os.path.join(out, 'lua-LICENSE.txt'), 'w', encoding='utf-8', newline='\n').write(m.group(0) + '\n')
            files = ['lua.h']
        if name == 'sqlite' and not files:
            open(os.path.join(out, 'sqlite-PUBLIC-DOMAIN.txt'), 'w', encoding='utf-8', newline='\n').write(
                'SQLite is in the public domain: https://www.sqlite.org/copyright.html\n')
            files = ['(public domain)']
        if not files:
            sys.exit('package: no licence text found for "%s" (%s): add it to tools/package.py' % (name, src))
        found.append(name)
    if not found:
        sys.exit('package: no third-party sources under %s' % deps)
    return found


def main():
    ap = argparse.ArgumentParser(description='Stage a release folder.')
    ap.add_argument('--build', required=True, help='the build folder (the one with bin/ and _deps/)')
    ap.add_argument('--kind', required=True, choices=['windows', 'linux'])
    ap.add_argument('--out', required=True, help='the folder to make (it is removed first)')
    a = ap.parse_args()
    build = os.path.abspath(a.build)
    out = os.path.abspath(a.out)
    bin_dir = os.path.join(build, 'bin')
    shutil.rmtree(out, ignore_errors=True)
    os.makedirs(out)

    if a.kind == 'windows':
        for exe in ('hml_designer.exe', 'hml_runtime.exe'):
            shutil.copyfile(os.path.join(bin_dir, exe), os.path.join(out, exe))
        for d in ('designer', 'skills', 'fonts', 'icons'):
            copy_tree(os.path.join(bin_dir, d), os.path.join(out, d))
    else:
        dst = os.path.join(out, 'hml_runtime')
        shutil.copyfile(os.path.join(bin_dir, 'hml_runtime'), dst)
        os.chmod(dst, 0o755)
        copy_tree(os.path.join(ROOT, 'tools', 'rpi'), os.path.join(out, 'tools', 'rpi'))

    for p in ('demo', 'lcd35'):
        copy_tree(os.path.join(ROOT, 'projects', p), os.path.join(out, 'examples', p))
    for f in ('README.md', 'README.ja.md', 'README.zh-CN.md', 'CHANGELOG.md', 'LICENSE', 'NOTICE', 'SECURITY.md'):
        shutil.copyfile(os.path.join(ROOT, f), os.path.join(out, f))
    # the README links to docs/ (the quick start and its pictures)
    copy_tree(os.path.join(ROOT, 'docs'), os.path.join(out, 'docs'))
    names = third_party_licenses(build, os.path.join(out, 'licenses'))
    print('packaged %s: %s' % (a.kind, out))
    print('third-party licences: ' + ', '.join(names))


if __name__ == '__main__':
    main()
