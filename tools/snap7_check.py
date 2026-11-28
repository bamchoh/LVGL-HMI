"""Checks the S7 driver of the runtime against the Snap7 server (https://snap7.sourceforge.net/), from
the Python package python-snap7: an implementation of S7comm that was not written with this
runtime, so a misunderstanding on our side does not go unnoticed (the unit tests talk to a fake
PLC that we wrote ourselves).

    pip install python-snap7
    python tools/snap7_check.py [--runtime build/bin/hml_runtime.exe] [--port 1102]

The script starts a Snap7 server on a local port with DB1, DB2 and the M, I and Q areas, puts
values into them, runs the runtime with a small project (headless) whose script prints what the
tags hold and then writes other values, and looks at the memory of the server.
Not part of ctest: it needs python-snap7. (With "python -I" a package installed for the user
only is not found.) Exit code 0 = all passed, 2 = could not run.
"""
import argparse, ctypes, json, os, shutil, struct, subprocess, sys, tempfile, time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)

try:
    import snap7
    from snap7.server import Server
except Exception as e:                                   # (also: the package is there, its native library is not)
    print('python-snap7 is needed for this check and could not be loaded (%s: %s).' % (type(e).__name__, e))
    print('Install it with:  pip install python-snap7   (and run this script without "python -I" if it is installed for the user only)')
    sys.exit(2)


def server_areas():
    """The codes of the areas of a Snap7 server, whatever the version of python-snap7 calls them."""
    for module in ('snap7.type', 'snap7.types'):
        try:
            m = __import__(module, fromlist=['x'])
        except ImportError:
            continue
        if hasattr(m, 'SrvArea'):
            a = m.SrvArea
            return {'DB': a.DB, 'M': a.MK, 'I': a.PE, 'Q': a.PA}
        if hasattr(m, 'srvAreaDB'):
            return {'DB': m.srvAreaDB, 'M': m.srvAreaMK, 'I': m.srvAreaPE, 'Q': m.srvAreaPA}
    print('this version of python-snap7 (%s) names the areas of a server in a way this script does not know' % getattr(snap7, '__version__', '?'))
    sys.exit(2)


class Plc:
    """A Snap7 server and its memory."""

    def __init__(self, port):
        areas = server_areas()
        # (bytearrays: python-snap7 3.x serves a bytearray as it is, and takes a copy of a ctypes array)
        self.mem = {'DB1': bytearray(256), 'DB2': bytearray(1000),
                    'M': bytearray(64), 'I': bytearray(64), 'Q': bytearray(64)}
        self.server = Server()
        self.server.register_area(areas['DB'], 1, self.mem['DB1'])
        self.server.register_area(areas['DB'], 2, self.mem['DB2'])
        self.server.register_area(areas['M'], 0, self.mem['M'])
        self.server.register_area(areas['I'], 0, self.mem['I'])
        self.server.register_area(areas['Q'], 0, self.mem['Q'])
        self.server.start(port)

    def put(self, area, at, data):
        for i, b in enumerate(bytes(data)):
            self.mem[area][at + i] = b

    def get(self, area, at, n):
        return bytes(self.mem[area][at:at + n])

    def stop(self):
        try:
            self.server.stop()
            self.server.destroy()
        except Exception:
            pass


def run_project(runtime, port, tags, rows, expect, writes, seconds=8):
    """Runs the runtime with the S7 driver. Its script prints the tags named in `expect` after 3 s
    and then writes `writes`. Returns ({tag: text printed}, the output of the runtime)."""
    d = tempfile.mkdtemp(prefix='hml_snap7_')
    try:
        os.makedirs(os.path.join(d, 'screens'))
        lua = ['n = (n or 0) + 1', 'if n == 6 then']
        for name in expect:
            lua.append('  print("CHK %s=" .. tostring(tag["%s"]))' % (name, name))
        lua.append('end')
        lua.append('if n == 7 then')
        for name, value in writes.items():
            lua.append('  tag["%s"] = %s' % (name, json.dumps(value)))
        lua.append('end')
        driver = {'type': 's7', 'host': '127.0.0.1', 'port': port, 'rack': 0, 'slot': 1, 'status_tag': 'online',
                  'poll_ms': 100, 'timeout_ms': 1000, 'map': rows}
        project = {'title': 'snap7 check', 'display': {'width': 320, 'height': 240}, 'start_screen': 's',
                   'tags': tags + [{'name': 'online', 'type': 'bool', 'init': False}],
                   'drivers': [driver],
                   'scripts': [{'name': 'chk', 'trigger': 'interval', 'interval_ms': 500, 'code': lua}],
                   'screens': ['screens/s.json']}
        with open(os.path.join(d, 'project.json'), 'w', encoding='utf-8') as f:
            json.dump(project, f)
        with open(os.path.join(d, 'screens', 's.json'), 'w', encoding='utf-8') as f:
            json.dump({'name': 's', 'children': []}, f)
        env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy')
        r = subprocess.run([runtime, os.path.join(d, 'project.json'), '--exit-after', str(seconds * 1000)],
                           capture_output=True, env=env, timeout=seconds + 30)
        out = (r.stdout + r.stderr).decode('utf-8', 'replace')
        got = {}
        for line in out.splitlines():
            if 'CHK ' in line:
                k, _, v = line.split('CHK ', 1)[1].partition('=')
                got[k] = v.strip()
        return got, out
    finally:
        shutil.rmtree(d, ignore_errors=True)


class Report:
    def __init__(self):
        self.bad = 0
        self.n = 0

    def check(self, what, got, want, tol=None):
        self.n += 1
        try:
            ok = (abs(float(got) - float(want)) <= tol) if tol is not None else (got == want)
        except (TypeError, ValueError):
            ok = False
        if not ok:
            self.bad += 1
        print('  %-4s %-44s %s%s' % ('ok' if ok else 'FAIL', what, got, '' if ok else '   (expected %s)' % (want,)))

    def warnings(self, out, allow=()):
        for line in out.splitlines():
            if ('WARN' in line or 'ERROR' in line) and not any(a in line for a in allow):
                self.n += 1
                self.bad += 1
                print('  FAIL the runtime said: ' + line.strip())


def T(name, typ): return {'name': name, 'type': typ, 'init': '' if typ == 'string' else (False if typ == 'bool' else 0)}


def check_s7(runtime, port, rep):
    plc = Plc(port)
    try:
        time.sleep(0.3)
        print('s7 against the Snap7 server of python-snap7 %s (port %d)' % (getattr(snap7, '__version__', '?'), port))
        # ---- what the runtime is to read (every number big-endian)
        plc.put('DB1', 0, [0x05])                                        # DBX0.0 and DBX0.2
        plc.put('DB1', 2, [200, 0xC8])                                   # a byte, without and with a sign
        plc.put('DB1', 4, struct.pack('>h', -1234))
        plc.put('DB1', 6, struct.pack('>H', 40000))
        plc.put('DB1', 8, struct.pack('>i', -100000))
        plc.put('DB1', 12, struct.pack('>I', 3000000000))
        plc.put('DB1', 16, struct.pack('>f', 12.5))
        plc.put('DB1', 20, struct.pack('>h', 250))                       # scaled: 250 * 0.1 - 5
        plc.put('DB1', 30, bytes([8, 5]) + b'HELLOXYZ')                  # STRING[8], 5 characters in use
        for i in range(500):                                             # DB2: 500 words, more than one PDU holds
            plc.put('DB2', i * 2, struct.pack('>H', i))
        plc.put('M', 10, [0x08])
        plc.put('M', 20, struct.pack('>H', 0x1234))
        plc.put('M', 24, struct.pack('>f', 1.5))
        plc.put('I', 0, [0x81])
        plc.put('I', 2, struct.pack('>H', 300))
        plc.put('Q', 1, [0x02])
        plc.put('Q', 6, struct.pack('>H', 0x0102))
        # ---- where it is to write
        plc.put('DB1', 100, bytes(60))
        plc.put('DB1', 116, [0x80])
        plc.put('DB1', 120, bytes([10, 10]) + b'XXXXXXXXXX' + b'Z')      # STRING[10], then a byte of something else
        plc.put('M', 5, [0x80])
        plc.put('Q', 0, [0x01])

        tags = [T(n, 'bool') for n in ('x0', 'x1', 'x2', 'm3', 'i0', 'e7', 'q1', 'wbit', 'wm', 'wq')] + \
               [T(n, 'int') for n in ('u8', 's8', 's16', 'u16', 's32', 'u32', 'mw', 'iw', 'aw', 'nodb',
                                      'sp', 'big', 'spu', 'wb8', 'ws8', 'wmw', 'wqw')] + \
               [T(n, 'float') for n in ('f', 'scaled', 'mf', 'spf')] + [T('name', 'string'), T('wname', 'string')] + \
               [T('w%d' % i, 'int') for i in range(500)]
        rows = [
            {'tag': 'x0', 'addr': 'DB1.DBX0.0'}, {'tag': 'x1', 'addr': 'DB1.DBX0.1'}, {'tag': 'x2', 'addr': 'db1.dbx0.2'},
            {'tag': 'u8', 'addr': 'DB1.DBB2'}, {'tag': 's8', 'addr': 'DB1.DBB3', 'dtype': 'int8'},
            {'tag': 's16', 'addr': 'DB1.DBW4'}, {'tag': 'u16', 'addr': 'DB1.DBW6', 'dtype': 'uint16'},
            {'tag': 's32', 'addr': 'DB1.DBD8'}, {'tag': 'u32', 'addr': 'DB1.DBD12', 'dtype': 'uint32'},
            {'tag': 'f', 'addr': 'DB1.DBD16', 'dtype': 'float'},
            {'tag': 'scaled', 'addr': 'DB1.DBW20', 'scale': 0.1, 'offset': -5},
            {'tag': 'name', 'addr': 'DB1.DBB30', 'dtype': 'string', 'length': 8},
            {'tag': 'm3', 'addr': 'M10.3'}, {'tag': 'mw', 'addr': '%MW20'}, {'tag': 'mf', 'addr': 'MD24', 'dtype': 'float'},
            {'tag': 'i0', 'addr': 'I0.0'}, {'tag': 'e7', 'addr': 'E0.7'}, {'tag': 'iw', 'addr': 'IW2'},
            {'tag': 'q1', 'addr': 'Q1.1'}, {'tag': 'aw', 'addr': 'AW6'},
            {'tag': 'nodb', 'addr': 'DB9.DBW0', 'access': 'ro'},                    # no such DB: refused, the rest goes on
            {'tag': 'sp', 'addr': 'DB1.DBW100'}, {'tag': 'big', 'addr': 'DB1.DBD102'},
            {'tag': 'spu', 'addr': 'DB1.DBD106', 'dtype': 'uint32'}, {'tag': 'spf', 'addr': 'DB1.DBD110', 'dtype': 'float'},
            {'tag': 'wb8', 'addr': 'DB1.DBB114'}, {'tag': 'ws8', 'addr': 'DB1.DBB115', 'dtype': 'int8'},
            {'tag': 'wbit', 'addr': 'DB1.DBX116.3'},
            {'tag': 'wname', 'addr': 'DB1.DBB120', 'dtype': 'string', 'length': 10},
            {'tag': 'wm', 'addr': 'M5.1'}, {'tag': 'wmw', 'addr': 'MW6'},
            {'tag': 'wq', 'addr': 'Q0.2'}, {'tag': 'wqw', 'addr': 'QW2'},
        ] + [{'tag': 'w%d' % i, 'addr': 'DB2.DBW%d' % (i * 2), 'access': 'ro'} for i in range(500)]
        writes = {'sp': -4321, 'big': -70000, 'spu': 3000000000, 'spf': 1.5, 'wb8': 200, 'ws8': -2, 'wbit': True,
                  'wname': 'BYE', 'wm': True, 'wmw': 40000, 'wq': True, 'wqw': 258}
        expect = ['online', 'x0', 'x1', 'x2', 'u8', 's8', 's16', 'u16', 's32', 'u32', 'f', 'scaled', 'name', 'm3', 'mw', 'mf',
                  'i0', 'e7', 'iw', 'q1', 'aw', 'nodb', 'w0', 'w230', 'w231', 'w499']
        got, out = run_project(runtime, port, tags, rows, expect, writes)

        rep.check('online', got.get('online'), 'true')
        rep.check('DB1.DBX0.0 -> x0', got.get('x0'), 'true')
        rep.check('DB1.DBX0.1 -> x1', got.get('x1'), 'false')
        rep.check('db1.dbx0.2 -> x2', got.get('x2'), 'true')
        rep.check('DB1.DBB2 -> u8', got.get('u8'), 200, 0)
        rep.check('DB1.DBB3 int8 -> s8', got.get('s8'), -56, 0)
        rep.check('DB1.DBW4 -> s16', got.get('s16'), -1234, 0)
        rep.check('DB1.DBW6 uint16 -> u16', got.get('u16'), 40000, 0)
        rep.check('DB1.DBD8 -> s32', got.get('s32'), -100000, 0)
        rep.check('DB1.DBD12 uint32 -> u32', got.get('u32'), 3000000000, 0)
        rep.check('DB1.DBD16 float -> f', got.get('f'), 12.5, 1e-6)
        rep.check('DB1.DBW20 * 0.1 - 5 -> scaled', got.get('scaled'), 20, 1e-6)
        rep.check('DB1.DBB30 string -> name', got.get('name'), 'HELLO')
        rep.check('M10.3 -> m3', got.get('m3'), 'true')
        rep.check('%MW20 -> mw', got.get('mw'), 0x1234, 0)
        rep.check('MD24 float -> mf', got.get('mf'), 1.5, 1e-6)
        rep.check('I0.0 -> i0', got.get('i0'), 'true')
        rep.check('E0.7 -> e7', got.get('e7'), 'true')
        rep.check('IW2 -> iw', got.get('iw'), 300, 0)
        rep.check('Q1.1 -> q1', got.get('q1'), 'true')
        rep.check('AW6 -> aw', got.get('aw'), 258, 0)
        rep.check('DB9 (not there): the tag stays', got.get('nodb'), 0, 0)
        rep.check('DB2: 500 words, w0', got.get('w0'), 0, 0)
        rep.check('DB2: w230 (the end of the first item)', got.get('w230'), 230, 0)
        rep.check('DB2: w231 (the start of the second)', got.get('w231'), 231, 0)
        rep.check('DB2: w499', got.get('w499'), 499, 0)

        rep.check('sp -> DB1.DBW100', struct.unpack('>h', plc.get('DB1', 100, 2))[0], -4321)
        rep.check('big -> DB1.DBD102', struct.unpack('>i', plc.get('DB1', 102, 4))[0], -70000)
        rep.check('spu -> DB1.DBD106 uint32', struct.unpack('>I', plc.get('DB1', 106, 4))[0], 3000000000)
        rep.check('spf -> DB1.DBD110 float', plc.get('DB1', 110, 4), struct.pack('>f', 1.5))
        rep.check('wb8, ws8 -> DB1.DBB114, DBB115', plc.get('DB1', 114, 2), bytes([200, 0xFE]))
        # A bit write. The server of python-snap7 3.x has no bits: it drops the bit number and stores the data
        # byte (0x01 = true) over the whole byte. So what is seen here is that the write was taken, went to the
        # right byte and said "true" - NOT that only bit 3 changed (a real PLC changes that bit alone).
        rep.check('wbit -> DB1.DBX116.3 (taken; byte and value)', plc.get('DB1', 116, 1), bytes([0x01]))
        rep.check('wname -> DB1.DBB120 (declared length kept)', plc.get('DB1', 120, 13), bytes([10, 3]) + b'BYE' + bytes(7) + b'Z')
        rep.check('wm -> M5.1 (taken; byte and value)', plc.get('M', 5, 1), bytes([0x01]))
        rep.check('wmw -> MW6', struct.unpack('>H', plc.get('M', 6, 2))[0], 40000)
        rep.check('wq -> Q0.2 (taken; byte and value)', plc.get('Q', 0, 1), bytes([0x01]))
        rep.check('wqw -> QW2', plc.get('Q', 2, 2), bytes([0x01, 0x02]))
        rep.check('the inputs were not written', plc.get('I', 0, 1), bytes([0x81]))
        refused = [l for l in out.splitlines() if 'DB9' in l and 'the PLC refused' in l]
        rep.check('DB9: the refusal is in the log, once', len(refused), 1)
        rep.warnings(out, allow=('DB9',))
        if rep.bad:
            print(out[-3000:])
    finally:
        plc.stop()


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--runtime', default=os.path.join(ROOT, 'build', 'bin', 'hml_runtime.exe' if os.name == 'nt' else 'hml_runtime'))
    ap.add_argument('--port', type=int, default=1102, help='the port the Snap7 server listens on (102 needs administrator rights on Linux)')
    a = ap.parse_args()
    if not os.path.exists(a.runtime):
        print('no runtime at %s (build it, or name it with --runtime)' % a.runtime)
        return 2
    rep = Report()
    try:
        check_s7(a.runtime, a.port, rep)
    except Exception as e:
        print('the Snap7 server could not be used (%s: %s)' % (type(e).__name__, e))
        return 2
    print('%d checks, %d failed' % (rep.n, rep.bad))
    return 1 if rep.bad else 0


if __name__ == '__main__':
    sys.exit(main())
