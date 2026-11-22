"""Checks the MQTT driver against a real broker, Eclipse Mosquitto (the unit tests talk to a fake
broker we wrote ourselves): plaintext on a broker that already runs (default localhost:1883, e.g.
the Windows service the installer sets up) and TLS on a broker this script starts on 18883 with a
throw-away CA (needs openssl). For each: the online / offline status (last will), a value per
topic, a JSON message of several tags (fields, const, ts; again on a change), commands in (one
value, fields of a JSON message, a value outside min / max refused).

    python -I tools/mosquitto_check.py [--mosquitto "C:\Program Files\mosquitto"] [--runtime build/bin/hml_runtime.exe] [--port 1883]

Not part of ctest: it needs Mosquitto. Exit code 0 = all passed.
"""
import argparse, json, os, shutil, subprocess, sys, tempfile, time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ap = argparse.ArgumentParser()
ap.add_argument('--mosquitto', default=r'C:\Program Files\mosquitto' if os.name == 'nt' else '/usr/sbin')
ap.add_argument('--runtime', default=os.path.join(ROOT, 'build', 'bin', 'hml_runtime.exe' if os.name == 'nt' else 'hml_runtime'))
ap.add_argument('--port', type=int, default=1883)
opt = ap.parse_args()
MQ = opt.mosquitto
RT = opt.runtime
W = os.path.join(tempfile.gettempdir(), 'hml-mosquitto-check')
EXE = '.exe' if os.name == 'nt' else ''
OPENSSL = r'C:\Program Files\Git\mingw64in\openssl.exe'
shutil.rmtree(W, ignore_errors=True)
os.makedirs(os.path.join(W, 'certs'))
results = []


def check(name, got, want):
    ok = got == want
    results.append(ok)
    print(('ok    ' if ok else 'FAIL  ') + name + ('' if ok else '   got %r, want %r' % (got, want)))


def run(*a, **kw):
    return subprocess.run(list(a), capture_output=True, text=True, **kw)


# ---- the certificates (CA, broker for "localhost")
C = os.path.join(W, 'certs')
os.environ['MSYS_NO_PATHCONV'] = '1'
openssl = OPENSSL if os.path.exists(OPENSSL) else 'openssl'
run(openssl, 'req', '-x509', '-newkey', 'rsa:2048', '-nodes', '-days', '2', '-subj', '/CN=hml test CA', '-keyout', os.path.join(C, r'ca.key'), '-out', os.path.join(C, r'ca.pem'))
run(openssl, 'req', '-newkey', 'rsa:2048', '-nodes', '-subj', '/CN=localhost', '-keyout', os.path.join(C, r'server.key'), '-out', os.path.join(C, r'server.csr'))
open(os.path.join(C, r'san.ext'), 'w').write('subjectAltName=DNS:localhost,IP:127.0.0.1\n')
r = run(openssl, 'x509', '-req', '-in', os.path.join(C, r'server.csr'), '-CA', os.path.join(C, r'ca.pem'), '-CAkey', os.path.join(C, r'ca.key'), '-CAcreateserial', '-days', '2', '-out', os.path.join(C, r'server.pem'), '-extfile', os.path.join(C, r'san.ext'))
assert os.path.exists(os.path.join(C, r'server.pem')), r.stderr
conf = os.path.join(W, 'tls.conf')
open(conf, 'w').write('listener 18883 127.0.0.1\nallow_anonymous true\ncafile %s\ncertfile %s\nkeyfile %s\n' % (os.path.join(C, r'ca.pem'), os.path.join(C, r'server.pem'), os.path.join(C, r'server.key')))
tls_broker = subprocess.Popen([os.path.join(MQ, 'mosquitto' + EXE), '-c', conf], stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
time.sleep(1.0)


def scenario(label, port, tls):
    proj = os.path.join(W, label)
    os.makedirs(os.path.join(proj, 'screens'))
    open(os.path.join(proj, 'screens', 's.json'), 'w').write('{ "name": "s", "children": [] }')
    drv = {'type': 'mqtt', 'host': 'localhost', 'port': port, 'tls': tls, 'client_id': 'hml-real-' + label, 'topic_prefix': 'hmltest/%s/' % label,
           'status_topic': 'status', 'status_tag': 'online',
           'publish': ['temp',
                       {'topic': 'line1/status', 'fields': {'/oven/temp': 'temp', '/count': 'count', '/run': 'run', '/product': 'product'},
                        'const': {'/device': 'line1'}, 'ts': '/ts', 'qos': 1}],
           'subscribe': [{'tag': 'sp', 'topic': 'cmd/sp'},
                         {'topic': 'line1/cmd', 'fields': {'/oven/setpoint': 'oven_sp', '/mixer/rpm': 'rpm_sp', '/mode': 'mode'}}]}
    if tls:
        drv['ca_file'] = os.path.join(C, r'ca.pem')
    p = {'start_screen': 's', 'screens': ['screens/s.json'],
         'tags': [{'name': 'temp', 'type': 'float', 'init': 211.5}, {'name': 'count', 'type': 'int', 'init': 3420},
                  {'name': 'run', 'type': 'bool', 'init': True}, {'name': 'product', 'type': 'string', 'init': 'White bread'},
                  {'name': 'sp', 'type': 'int', 'init': 10, 'min': 0, 'max': 100}, {'name': 'oven_sp', 'type': 'int', 'init': 200, 'min': 150, 'max': 260},
                  {'name': 'rpm_sp', 'type': 'float', 'init': 100}, {'name': 'mode', 'type': 'string', 'init': 'auto'},
                  {'name': 'online', 'type': 'bool', 'init': False}],
         'drivers': [drv],
         'scripts': [{'name': 't', 'trigger': 'interval', 'interval_ms': 250, 'code': [
             'if millis() > 3000 and not A then A = true; tag.temp = 212.25 end',
             'if millis() > 10000 and not B then B = true; log("REAL", tag.online, tag.sp, tag.oven_sp, tag.rpm_sp, tag.mode) end']}]}
    json.dump(p, open(os.path.join(proj, 'project.json'), 'w'))
    tls_args = ['--cafile', os.path.join(C, r'ca.pem')] if tls else []
    sub = subprocess.Popen([os.path.join(MQ, 'mosquitto_sub' + EXE), '-h', 'localhost', '-p', str(port), *tls_args, '-v', '-t', 'hmltest/%s/#' % label, '-W', '13'],
                           stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    time.sleep(0.5)
    rt = subprocess.Popen([RT, os.path.join(proj, 'project.json'), '--exit-after', '11500'], stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, encoding='utf-8', errors='replace')
    time.sleep(2.5)
    pub = lambda t, m: run(os.path.join(MQ, 'mosquitto_pub' + EXE), '-h', 'localhost', '-p', str(port), *tls_args, '-t', 'hmltest/%s/%s' % (label, t), '-m', m)
    pub('cmd/sp', '55')
    pub('line1/cmd', json.dumps({'oven': {'setpoint': 220}, 'mixer': {'rpm': 135.5}}))
    pub('line1/cmd', json.dumps({'oven': {'setpoint': 999}, 'mode': 'manual'}))      # 999: outside 150..260
    log, _ = rt.communicate(timeout=30)
    got, _ = sub.communicate(timeout=30)
    lines = [l for l in got.splitlines() if l.startswith('hmltest/')]
    msgs = {}
    for l in lines:
        t, _, m = l.partition(' ')
        msgs.setdefault(t[len('hmltest/%s/' % label):], []).append(m)
    print('--', label, 'broker port', port, 'tls' if tls else 'plaintext')
    status = msgs.get('status', [])
    # (the first one may be the retained "offline" of an earlier run)
    check(label + ': status online, then offline (last will / on stop)', ('online' in status, status[-1:]), (True, ['offline']))
    check(label + ': one value per topic', msgs.get('temp', [None])[0], '211.5')
    st = [json.loads(m) for m in msgs.get('line1/status', [])]
    check(label + ': message: fields, const, int as integer, bool, text',
          bool(st) and (st[0]['oven']['temp'], st[0]['count'], st[0]['run'], st[0]['product'], st[0]['device'], isinstance(st[0]['ts'], int)),
          (211.5, 3420, True, 'White bread', 'line1', True))
    check(label + ': message again when a tag changes', any(m['oven']['temp'] == 212.25 for m in st), True)
    real = [l for l in log.splitlines() if '[script] REAL' in l]
    vals = real[0].split('REAL', 1)[1].split() if real else []
    check(label + ': commands in (online, sp 55, oven_sp 220 (999 refused), rpm 135.5, mode manual)', vals, ['true', '55', '220', '135.5', 'manual'])
    check(label + ': 999 refused in the log', '999 is outside 150..260' in log, True)
    if not real:
        print(log[-2500:])


scenario('plain', opt.port, False)
scenario('tls', 18883, True)
tls_broker.terminate()
print('%d / %d passed' % (sum(results), len(results)))
shutil.rmtree(W, ignore_errors=True)
sys.exit(0 if all(results) else 1)
