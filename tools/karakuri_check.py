"""Checks the drivers of the runtime against Karakuri (https://github.com/bamchoh/karakuri), a PLC
simulator with a REST API: an implementation of the protocols that was not written with this
runtime, so a misunderstanding on our side does not go unnoticed (the unit tests talk to
fakes that we wrote ourselves).

    1. start Karakuri (its window shows the address of the API, for example http://localhost:8765/api)
    2. python -I tools/karakuri_check.py [--api http://localhost:8765/api] [--runtime build/bin/hml_runtime.exe] [slmp modbus ...]

For each protocol: the server of Karakuri is started, values are put into its memory over the
API, the runtime runs a small project for a few seconds (headless) whose script prints what
the tags hold and then writes other values, and the memory of Karakuri is read back.
Not part of ctest: it needs Karakuri running.
"""
import argparse, json, os, shutil, struct, subprocess, sys, tempfile, urllib.request, urllib.error

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)


class Api:
    def __init__(self, base):
        self.base = base.rstrip('/')

    def call(self, method, path, body=None):
        data = None if body is None else json.dumps(body).encode()
        req = urllib.request.Request(self.base + path, data=data, method=method, headers={'Content-Type': 'application/json'})
        try:
            with urllib.request.urlopen(req, timeout=10) as r:
                t = r.read().decode('utf-8')
                return json.loads(t) if t.strip() else None
        except urllib.error.HTTPError as e:
            raise RuntimeError('%s %s: %s %s' % (method, path, e.code, e.read().decode('utf-8', 'replace')))

    def ensure_server(self, proto, settings=None):
        if not any(s['protocolType'] == proto for s in (self.call('GET', '/servers') or [])):
            self.call('POST', '/servers', {'protocolType': proto})
        if settings:
            cfg = self.call('GET', '/servers/%s/config' % proto)
            if any(cfg['settings'].get(k) != v for k, v in settings.items()):
                self.call('POST', '/servers/%s/stop' % proto)
                cfg['settings'].update(settings)
                self.call('PUT', '/servers/%s/config' % proto, cfg)
        st = self.call('GET', '/servers/%s/status' % proto)
        if 'Running' not in json.dumps(st):
            self.call('POST', '/servers/%s/start' % proto)
        return self.call('GET', '/servers/%s/config' % proto)['settings']

    def put_words(self, proto, area, addr, words):
        for i, w in enumerate(words):
            self.call('PUT', '/memory/%s/%s/words/%d' % (proto, area, addr + i), {'value': w & 0xFFFF})

    def get_words(self, proto, area, addr, count):
        r = self.call('GET', '/memory/%s/%s/words?address=%d&count=%d' % (proto, area, addr, count))
        return [int(v) & 0xFFFF for v in (r['values'] if isinstance(r, dict) else r)]

    def put_bit(self, proto, area, addr, on):
        self.call('PUT', '/memory/%s/%s/bits/%d' % (proto, area, addr), {'value': bool(on)})

    def get_bit(self, proto, area, addr):
        r = self.call('GET', '/memory/%s/%s/bits?address=%d&count=1' % (proto, area, addr))
        return bool((r['values'] if isinstance(r, dict) else r)[0])


def u32_words(v, low_first=True):
    v &= 0xFFFFFFFF
    lo, hi = v & 0xFFFF, v >> 16
    return [lo, hi] if low_first else [hi, lo]


def float_words(f, low_first=True):
    return u32_words(struct.unpack('<I', struct.pack('<f', f))[0], low_first)


def words_float(w, low_first=True):
    lo, hi = (w[0], w[1]) if low_first else (w[1], w[0])
    return struct.unpack('<f', struct.pack('<I', (hi << 16) | lo))[0]


def words_i32(w, low_first=True):
    lo, hi = (w[0], w[1]) if low_first else (w[1], w[0])
    return struct.unpack('<i', struct.pack('<I', (hi << 16) | lo))[0]


def run_project(runtime, tags, driver, expect, writes, seconds=7):
    """Runs the runtime with one driver. Its script prints the tags named in `expect` after 3 s
    and then writes `writes`. Returns ({tag: text printed}, the output of the runtime)."""
    d = tempfile.mkdtemp(prefix='hml_karakuri_')
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
        project = {'title': 'karakuri check', 'display': {'width': 320, 'height': 240}, 'start_screen': 's',
                   'tags': tags + [{'name': 'online', 'type': 'bool', 'init': False}],
                   'drivers': [dict(driver, status_tag='online', poll_ms=100, timeout_ms=1000)],
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
        ok = (abs(float(got) - float(want)) <= tol) if tol is not None and got not in (None, '') else (got == want)
        if not ok:
            self.bad += 1
        print('  %-4s %-34s %s%s' % ('ok' if ok else 'FAIL', what, got, '' if ok else '   (expected %s)' % (want,)))

    def warnings(self, out, allow=()):
        for line in out.splitlines():
            if ('WARN' in line or 'ERROR' in line) and not any(a in line for a in allow):
                self.n += 1
                self.bad += 1
                print('  FAIL the runtime said: ' + line.strip())


def T(name, typ): return {'name': name, 'type': typ, 'init': '' if typ == 'string' else (False if typ == 'bool' else 0)}


# ---------------------------------------------------------------- SLMP (MELSEC)
def check_slmp(api, runtime, rep):
    s = api.ensure_server('slmp')
    for frame in ('3e', '4e'):
        print('slmp, frame %s (port %s)' % (frame, s['port']))
        P = 'slmp'
        api.put_words(P, 'D', 100, [(-1234) & 0xFFFF])
        api.put_words(P, 'D', 102, u32_words(100000))
        api.put_words(P, 'D', 110, float_words(12.5))
        api.put_words(P, 'D', 120, [0x0008])
        api.put_words(P, 'D', 200, [0x4548, 0x4C4C, 0x004F, 0])            # "HELLO", the low byte first
        api.put_words(P, 'W', 0x1A, [40000])
        api.put_words(P, 'D', 300, [0, 0, 0, 0])
        api.put_bit(P, 'M', 10, True)
        api.put_bit(P, 'M', 11, True)
        api.put_bit(P, 'X', 0x1F, True)
        api.put_bit(P, 'Y', 0x20, False)
        tags = [T('speed', 'int'), T('total', 'int'), T('temp', 'float'), T('ready', 'bool'), T('name', 'string'), T('w', 'int'),
                T('run', 'bool'), T('stop', 'bool'), T('in3', 'bool'), T('out', 'bool'), T('sp', 'int'), T('big', 'int')]
        driver = {'type': 'slmp', 'host': '127.0.0.1', 'port': s['port'], 'frame': frame, 'map': [
            {'tag': 'speed', 'addr': 'D100'}, {'tag': 'total', 'addr': 'D102', 'dtype': 'int32'},
            {'tag': 'temp', 'addr': 'D110', 'dtype': 'float'}, {'tag': 'ready', 'addr': 'D120.3'},
            {'tag': 'name', 'addr': 'D200', 'dtype': 'string', 'length': 8}, {'tag': 'w', 'addr': 'W1A', 'dtype': 'uint16'},
            {'tag': 'run', 'addr': 'M10'}, {'tag': 'stop', 'addr': 'M11'}, {'tag': 'in3', 'addr': 'X1F', 'access': 'ro'},
            {'tag': 'out', 'addr': 'Y20'}, {'tag': 'sp', 'addr': 'D300'}, {'tag': 'big', 'addr': 'D302', 'dtype': 'int32'}]}
        writes = {'sp': 4321, 'big': -70000, 'temp': 1.5, 'run': False, 'out': True, 'name': 'BYE', 'w': 65535}
        got, out = run_project(runtime, tags, driver, ['online', 'speed', 'total', 'temp', 'ready', 'name', 'w', 'run', 'stop', 'in3', 'out'], writes)
        rep.check('online', got.get('online'), 'true')
        rep.check('D100 int16 -> speed', got.get('speed'), '-1234')
        rep.check('D102 int32 -> total', got.get('total'), '100000')
        rep.check('D110 float -> temp', got.get('temp'), 12.5, 1e-6)
        rep.check('D120.3 -> ready', got.get('ready'), 'true')
        rep.check('D200 string -> name', got.get('name'), 'HELLO')
        rep.check('W1A uint16 -> w', got.get('w'), '40000')
        rep.check('M10 -> run', got.get('run'), 'true')
        rep.check('M11 -> stop', got.get('stop'), 'true')
        rep.check('X1F -> in3', got.get('in3'), 'true')
        rep.check('Y20 -> out', got.get('out'), 'false')
        rep.check('sp -> D300', api.get_words(P, 'D', 300, 1)[0], 4321)
        rep.check('big -> D302 int32', words_i32(api.get_words(P, 'D', 302, 2)), -70000)
        rep.check('temp -> D110 float', words_float(api.get_words(P, 'D', 110, 2)), 1.5, 1e-6)
        rep.check('run -> M10', api.get_bit(P, 'M', 10), False)
        rep.check('M11 untouched', api.get_bit(P, 'M', 11), True)
        rep.check('out -> Y20', api.get_bit(P, 'Y', 0x20), True)
        rep.check('name -> D200 string', api.get_words(P, 'D', 200, 2), [0x5942, 0x0045])
        rep.check('w -> W1A', api.get_words(P, 'W', 0x1A, 1)[0], 65535)
        rep.warnings(out)


# ---------------------------------------------------------------- Modbus TCP
def check_modbus(api, runtime, rep):
    s = api.ensure_server('modbus-tcp')
    s = {'port': s.get('port') or s.get('tcpPort')}
    print('modbus-tcp (port %s)' % s['port'])
    P = 'modbus-tcp'
    api.put_words(P, 'holdingRegisters', 0, [1234, (-2) & 0xFFFF])
    api.put_words(P, 'holdingRegisters', 10, u32_words(100000, low_first=False))        # big-endian words by default
    api.put_words(P, 'holdingRegisters', 12, float_words(12.5, low_first=False))
    api.put_words(P, 'holdingRegisters', 20, [0, 0, 0, 0, 0])
    api.put_words(P, 'inputRegisters', 5, [777])
    api.put_bit(P, 'coils', 3, True)
    api.put_bit(P, 'coils', 4, False)
    api.put_bit(P, 'discreteInputs', 7, True)
    tags = [T('h', 'int'), T('hs', 'int'), T('h32', 'int'), T('hf', 'float'), T('ir', 'int'), T('c', 'bool'), T('c2', 'bool'), T('di', 'bool'),
            T('sp', 'int'), T('sp32', 'int'), T('spf', 'float')]
    driver = {'type': 'modbus_tcp', 'host': '127.0.0.1', 'port': s['port'], 'unit_id': 1, 'map': [
        {'tag': 'h', 'area': 'holding', 'addr': 0}, {'tag': 'hs', 'area': 'holding', 'addr': 1, 'dtype': 's16'},
        {'tag': 'h32', 'area': 'holding', 'addr': 10, 'dtype': 'u32'}, {'tag': 'hf', 'area': 'holding', 'addr': 12, 'dtype': 'float'},
        {'tag': 'ir', 'area': 'input', 'addr': 5}, {'tag': 'c', 'area': 'coil', 'addr': 3}, {'tag': 'c2', 'area': 'coil', 'addr': 4},
        {'tag': 'di', 'area': 'discrete', 'addr': 7},
        {'tag': 'sp', 'area': 'holding', 'addr': 20}, {'tag': 'sp32', 'area': 'holding', 'addr': 21, 'dtype': 's32'},
        {'tag': 'spf', 'area': 'holding', 'addr': 23, 'dtype': 'float'}]}
    writes = {'sp': 4321, 'sp32': -70000, 'spf': 1.5, 'c': False, 'c2': True}
    got, out = run_project(runtime, tags, driver, ['online', 'h', 'hs', 'h32', 'hf', 'ir', 'c', 'c2', 'di'], writes)
    rep.check('online', got.get('online'), 'true')
    rep.check('holding 0 -> h', got.get('h'), '1234')
    rep.check('holding 1 s16 -> hs', got.get('hs'), '-2')
    rep.check('holding 10 u32 -> h32', got.get('h32'), '100000')
    rep.check('holding 12 float -> hf', got.get('hf'), 12.5, 1e-6)
    rep.check('input 5 -> ir', got.get('ir'), '777')
    rep.check('coil 3 -> c', got.get('c'), 'true')
    rep.check('coil 4 -> c2', got.get('c2'), 'false')
    rep.check('discrete 7 -> di', got.get('di'), 'true')
    rep.check('sp -> holding 20', api.get_words(P, 'holdingRegisters', 20, 1)[0], 4321)
    rep.check('sp32 -> holding 21 s32', words_i32(api.get_words(P, 'holdingRegisters', 21, 2), low_first=False), -70000)
    rep.check('spf -> holding 23 float', words_float(api.get_words(P, 'holdingRegisters', 23, 2), low_first=False), 1.5, 1e-6)
    rep.check('c -> coil 3', api.get_bit(P, 'coils', 3), False)
    rep.check('c2 -> coil 4', api.get_bit(P, 'coils', 4), True)
    rep.warnings(out)


# ---------------------------------------------------------------- FINS (OMRON)
def check_fins(api, runtime, rep):
    s = api.ensure_server('fins')
    print('fins, FINS/TCP (port %s)' % s['port'])
    P = 'fins'
    api.put_words(P, 'DM', 100, [(-1234) & 0xFFFF])
    api.put_words(P, 'DM', 102, u32_words(100000))                      # the low word first
    api.put_words(P, 'DM', 110, float_words(12.5))
    api.put_words(P, 'DM', 120, [0x1234, 0x0008])
    api.put_words(P, 'DM', 200, [0x4845, 0x4C4C, 0x4F00, 0])            # "HELLO", the high byte first
    api.put_words(P, 'DM', 300, [0, 0, 0, 0, 0, 0, 0, 0])
    api.put_words(P, 'CIO', 10, [0x8001])
    api.put_words(P, 'WR', 5, [0x0002])
    api.put_words(P, 'HR', 7, [40000])
    api.put_words(P, 'EM1', 50, [321])
    api.put_words(P, 'T', 3, [55])
    api.put_words(P, 'C', 4, [66])
    tags = [T('speed', 'int'), T('total', 'int'), T('temp', 'float'), T('bcd', 'int'), T('ready', 'bool'), T('name', 'string'),
            T('c0', 'bool'), T('c15', 'bool'), T('c1', 'bool'), T('w1', 'bool'), T('w0', 'bool'), T('h', 'int'), T('e', 'int'),
            T('t', 'int'), T('c', 'int'), T('sp', 'int'), T('big', 'int'), T('spf', 'float'), T('spb', 'int')]
    driver = {'type': 'fins', 'host': '127.0.0.1', 'port': s['port'], 'map': [
        {'tag': 'speed', 'addr': 'D100'}, {'tag': 'total', 'addr': 'D102', 'dtype': 'int32'},
        {'tag': 'temp', 'addr': 'D110', 'dtype': 'float'}, {'tag': 'bcd', 'addr': 'D120', 'dtype': 'bcd16'},
        {'tag': 'ready', 'addr': 'D121.03'}, {'tag': 'name', 'addr': 'D200', 'dtype': 'string', 'length': 8},
        {'tag': 'c0', 'addr': 'CIO10.00'}, {'tag': 'c15', 'addr': '10.15'}, {'tag': 'c1', 'addr': 'CIO10.01'},
        {'tag': 'w1', 'addr': 'W5.01'}, {'tag': 'w0', 'addr': 'W5.00'},
        {'tag': 'h', 'addr': 'H7', 'dtype': 'uint16'}, {'tag': 'e', 'addr': 'E1_50'},
        {'tag': 't', 'addr': 'T3'}, {'tag': 'c', 'addr': 'C4'},
        {'tag': 'sp', 'addr': 'D300'}, {'tag': 'big', 'addr': 'D302', 'dtype': 'int32'},
        {'tag': 'spf', 'addr': 'D304', 'dtype': 'float'}, {'tag': 'spb', 'addr': 'D306', 'dtype': 'bcd16'}]}
    writes = {'sp': 4321, 'big': -70000, 'spf': 1.5, 'spb': 987, 'c1': True, 'w1': False, 'w0': True, 'name': 'BYE', 'h': 65535, 'e': -7}
    got, out = run_project(runtime, tags, driver, ['online', 'speed', 'total', 'temp', 'bcd', 'ready', 'name', 'c0', 'c15', 'c1', 'w1', 'w0', 'h', 'e', 't', 'c'], writes)
    rep.check('online', got.get('online'), 'true')
    rep.check('D100 int16 -> speed', got.get('speed'), '-1234')
    rep.check('D102 int32 -> total', got.get('total'), '100000')
    rep.check('D110 float -> temp', got.get('temp'), 12.5, 1e-6)
    rep.check('D120 bcd16 -> bcd', got.get('bcd'), '1234')
    rep.check('D121.03 -> ready', got.get('ready'), 'true')
    rep.check('D200 string -> name', got.get('name'), 'HELLO')
    rep.check('CIO10.00 -> c0', got.get('c0'), 'true')
    rep.check('10.15 -> c15', got.get('c15'), 'true')
    rep.check('CIO10.01 -> c1', got.get('c1'), 'false')
    rep.check('W5.01 -> w1', got.get('w1'), 'true')
    rep.check('W5.00 -> w0', got.get('w0'), 'false')
    rep.check('H7 uint16 -> h', got.get('h'), '40000')
    rep.check('E1_50 -> e', got.get('e'), '321')
    rep.check('T3 -> t', got.get('t'), '55')
    rep.check('C4 -> c', got.get('c'), '66')
    rep.check('sp -> D300', api.get_words(P, 'DM', 300, 1)[0], 4321)
    rep.check('big -> D302 int32', words_i32(api.get_words(P, 'DM', 302, 2)), -70000)
    rep.check('spf -> D304 float', words_float(api.get_words(P, 'DM', 304, 2)), 1.5, 1e-6)
    rep.check('spb -> D306 bcd16', api.get_words(P, 'DM', 306, 1)[0], 0x0987)
    rep.check('c1 -> CIO10 (bits 0, 1, 15)', api.get_words(P, 'CIO', 10, 1)[0], 0x8003)
    rep.check('w1, w0 -> W5', api.get_words(P, 'WR', 5, 1)[0], 0x0001)
    rep.check('name -> D200 string', api.get_words(P, 'DM', 200, 2), [0x4259, 0x4500])
    rep.check('h -> H7', api.get_words(P, 'HR', 7, 1)[0], 65535)
    rep.check('e -> E1_50', api.get_words(P, 'EM1', 50, 1)[0], (-7) & 0xFFFF)
    rep.warnings(out)


# ---------------------------------------------------------------- OPC UA (the runtime as a client)
def check_opcua(api, runtime, rep, security='none'):
    want = None
    if security != 'none':       # an encrypted endpoint next to the plain one
        want = {'securityEndpoints': [{'policy': 'None', 'mode': 'SignAndEncrypt'}, {'policy': 'Basic256Sha256', 'mode': 'Both'}]}
    s = api.ensure_server('opcua', want)
    print('opcua client, security %s (port %s)' % (security, s['port']))
    have = {v['name']: v for v in (api.call('GET', '/variables') or [])}
    def var(name, typ, value):
        if name not in have:
            api.call('POST', '/variables', {'name': name, 'dataType': typ, 'value': value})
        api.call('PUT', '/variables/%s/node-publishings/opcua' % name, {'enabled': True, 'accessMode': 'readwrite'})
        api.call('PUT', '/variables/%s/value' % name, {'value': value})
    def value(name):
        r = api.call('GET', '/variables/%s/value' % name)
        return r['value'] if isinstance(r, dict) and 'value' in r else r
    var('hml_i', 'INT', -1234); var('hml_d', 'DINT', 100000); var('hml_u', 'UINT', 40000); var('hml_r', 'REAL', 12.5)
    var('hml_l', 'LREAL', 0.25); var('hml_b', 'BOOL', True); var('hml_s', 'STRING[16]', 'HELLO')
    var('hml_wi', 'INT', 0); var('hml_wd', 'DINT', 0); var('hml_wr', 'REAL', 0.0); var('hml_wb', 'BOOL', False); var('hml_ws', 'STRING[16]', '')
    ns = s.get('namespace', 'urn:karakuri:plc')
    def row(tag, name, rw=False):
        r = {'tag': tag, 'node': 'nsu=%s;s=%s' % (ns, name)}
        if rw: r['access'] = 'rw'
        return r
    tags = [T('i', 'int'), T('d', 'int'), T('u', 'int'), T('r', 'float'), T('l', 'float'), T('b', 'bool'), T('s', 'string'),
            T('wi', 'int'), T('wd', 'int'), T('wr', 'float'), T('wb', 'bool'), T('ws', 'string')]
    driver = {'type': 'opcua_client', 'endpoint': 'opc.tcp://127.0.0.1:%s' % s['port'], 'security': security, 'publish_ms': 100, 'map': [
        row('i', 'hml_i'), row('d', 'hml_d'), row('u', 'hml_u'), row('r', 'hml_r'), row('l', 'hml_l'), row('b', 'hml_b'), row('s', 'hml_s'),
        row('wi', 'hml_wi', True), row('wd', 'hml_wd', True), row('wr', 'hml_wr', True), row('wb', 'hml_wb', True), row('ws', 'hml_ws', True)]}
    if security != 'none':
        driver['verify'] = False                     # (the certificate of the simulator is made at its first start)
    writes = {'wi': -4321, 'wd': -70000, 'wr': 1.5, 'wb': True, 'ws': 'BYE'}
    got, out = run_project(runtime, tags, driver, ['online', 'i', 'd', 'u', 'r', 'l', 'b', 's'], writes, seconds=8)
    rep.check('online', got.get('online'), 'true')
    rep.check('INT -> i', got.get('i'), '-1234')
    rep.check('DINT -> d', got.get('d'), '100000')
    rep.check('UINT -> u', got.get('u'), '40000')
    rep.check('REAL -> r', got.get('r'), 12.5, 1e-6)
    rep.check('LREAL -> l', got.get('l'), 0.25, 1e-9)
    rep.check('BOOL -> b', got.get('b'), 'true')
    rep.check('STRING -> s', got.get('s'), 'HELLO')
    rep.check('wi -> INT', value('hml_wi'), -4321)
    rep.check('wd -> DINT', value('hml_wd'), -70000)
    rep.check('wr -> REAL', value('hml_wr'), 1.5, 1e-6)
    rep.check('wb -> BOOL', value('hml_wb'), True)
    rep.check('ws -> STRING', value('hml_ws'), 'BYE')
    # (the runtime warns that the connection is not encrypted / that the certificate is not checked: meant)
    rep.warnings(out, allow=('security', 'encrypt', 'verify', 'certificate', 'plain'))


def check_opcua_secure(api, runtime, rep):
    check_opcua(api, runtime, rep, security='sign_encrypt')


CHECKS = {'slmp': check_slmp, 'fins': check_fins, 'modbus': check_modbus, 'opcua': check_opcua, 'opcua-secure': check_opcua_secure}


# ---------------------------------------------------------------- addresses without a tag (System > Device)
def check_device(api, runtime, rep):
    tests = os.path.join(os.path.dirname(runtime), 'hml_tests.exe' if os.name == 'nt' else 'hml_tests')
    if not os.path.exists(tests):
        print('device: %s is not built, skipped' % tests)
        return
    slmp = api.ensure_server('slmp')
    fins = api.ensure_server('fins')
    mb = api.ensure_server('modbus-tcp')
    mb_port = mb.get('port') or mb.get('tcpPort')
    print('device access: slmp %s, fins %s, modbus-tcp %s' % (slmp['port'], fins['port'], mb_port))
    api.put_words('slmp', 'D', 400, [(-1234) & 0xFFFF, 0] + u32_words(100000) + float_words(12.5))
    api.put_words('slmp', 'D', 410, [0, 0, 0, 0])
    for n, on in ((40, True), (41, False), (42, True), (43, False), (45, False)): api.put_bit('slmp', 'M', n, on)
    api.put_words('fins', 'DM', 400, [(-1234) & 0xFFFF, 0] + u32_words(100000) + float_words(12.5))
    api.put_words('fins', 'DM', 410, [0, 0, 0, 0])
    api.put_words('fins', 'CIO', 40, [0x0005, 0])
    api.put_words('modbus-tcp', 'holdingRegisters', 30, [1234] + u32_words(100000, low_first=False) + float_words(12.5, low_first=False))
    api.put_words('modbus-tcp', 'holdingRegisters', 40, [0, 0, 0])
    api.put_words('modbus-tcp', 'inputRegisters', 8, [777])
    for n, on in ((30, True), (31, False), (32, True), (35, False)): api.put_bit('modbus-tcp', 'coils', n, on)
    api.put_bit('modbus-tcp', 'discreteInputs', 9, True)
    env = dict(os.environ, HML_KARAKURI_SLMP=str(slmp['port']), HML_KARAKURI_FINS=str(fins['port']), HML_KARAKURI_MODBUS=str(mb_port))
    p = subprocess.run([tests, '-tc=karakuri*'], env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=120)
    out = p.stdout.decode('utf-8', 'replace')
    rep.check('reads and writes through the drivers (hml_tests -tc=karakuri*)', p.returncode, 0)
    if p.returncode != 0:
        print('\n'.join(l for l in out.splitlines() if not l.startswith('[hml')))
    rep.check('slmp: D410 written', api.get_words('slmp', 'D', 410, 1)[0], 4321)
    rep.check('slmp: D412 int32 written', words_i32(api.get_words('slmp', 'D', 412, 2)), -70000)
    rep.check('slmp: M45 set', api.get_bit('slmp', 'M', 45), True)
    rep.check('slmp: M40 reset', api.get_bit('slmp', 'M', 40), False)
    rep.check('slmp: M42 untouched', api.get_bit('slmp', 'M', 42), True)
    rep.check('fins: D410 written', api.get_words('fins', 'DM', 410, 1)[0], 4321)
    rep.check('fins: D412 int32 written', words_i32(api.get_words('fins', 'DM', 412, 2)), -70000)
    rep.check('fins: CIO41.02 set, the rest of the word untouched', api.get_words('fins', 'CIO', 41, 1)[0], 4)
    rep.check('modbus: holding 40 written', api.get_words('modbus-tcp', 'holdingRegisters', 40, 1)[0], 4321)
    rep.check('modbus: holding 41 int32 written', words_i32(api.get_words('modbus-tcp', 'holdingRegisters', 41, 2), low_first=False), -70000)
    rep.check('modbus: coil 35 set', api.get_bit('modbus-tcp', 'coils', 35), True)
    rep.check('modbus: coil 30 reset', api.get_bit('modbus-tcp', 'coils', 30), False)
    rep.check('modbus: coil 32 untouched', api.get_bit('modbus-tcp', 'coils', 32), True)


CHECKS['device'] = check_device


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--api', default='http://localhost:8765/api')
    ap.add_argument('--runtime', default=os.path.join(ROOT, 'build', 'bin', 'hml_runtime.exe' if os.name == 'nt' else 'hml_runtime'))
    ap.add_argument('protocols', nargs='*', default=[], help=' '.join(CHECKS))
    a = ap.parse_args()
    api = Api(a.api)
    try:
        api.call('GET', '/servers')
    except Exception as e:
        print('Karakuri does not answer at %s (%s). Start it; its window shows the address of the API.' % (a.api, e))
        return 2
    rep = Report()
    for name in (a.protocols or list(CHECKS)):
        if name not in CHECKS:
            print('unknown protocol %s (%s)' % (name, ' '.join(CHECKS)))
            return 2
        CHECKS[name](api, a.runtime, rep)
    print('%d checks, %d failed' % (rep.n, rep.bad))
    return 1 if rep.bad else 0


if __name__ == '__main__':
    sys.exit(main())
