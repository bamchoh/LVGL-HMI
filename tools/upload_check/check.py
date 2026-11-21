"""HML's uploads against servers that are not ours (servers.py in WSL: Python's http.server, pyftpdlib):
each case writes a project, runs the runtime for a few seconds with a click on "send", and looks at
what the server has (received/<port>/) and at what the runtime kept (logs/upload/).

  python -I tools/upload_check/check.py <the servers' folder in WSL> <ca.crt on this machine>

From Git Bash: MSYS_NO_PATHCONV=1 in front (else the WSL path is rewritten). See the README ("ログを外に出す")."""
import json, os, shutil, subprocess, sys, time
ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
SERVER_DIR = sys.argv[1] if len(sys.argv) > 1 else '/home/user/hml-mailtest'
CA = os.path.abspath(sys.argv[2] if len(sys.argv) > 2 else os.path.join(ROOT, 'build', 'scratch', 'mail_ca.crt')).replace('\\', '/')
RT = os.path.join(ROOT, 'build', 'bin', 'hml_runtime.exe' if os.name == 'nt' else 'hml_runtime')
D = os.path.join(ROOT, 'build', 'scratch', 'uploadpc')

def wsl(*args):
    r = subprocess.run(['wsl', '--'] + list(args), capture_output=True)
    return r.stdout

def received(port, sub=''):
    """{name: bytes} of what the server has for a port."""
    d = '%s/received/%s%s' % (SERVER_DIR, port, sub)
    names = [n for n in wsl('ls', '-1', d).decode('utf-8', 'replace').split() if n.endswith('.csv')]
    return {n: wsl('cat', d + '/' + n) for n in names}

def clear_received():
    wsl('sh', '-c', 'rm -rf %s/received/8080 %s/received/8443; find %s/received/2121 -name "*.csv" -delete 2>/dev/null; true' % (SERVER_DIR, SERVER_DIR, SERVER_DIR))

def run(upload, local=None, seconds=8, keep_folder=False):
    if not keep_folder: shutil.rmtree(D, ignore_errors=True)
    os.makedirs(os.path.join(D, 'screens'), exist_ok=True)
    project = {'title': 'Line 1', 'display': {'width': 480, 'height': 320}, 'start_screen': 's',
               'tags': [{'name': 'hot', 'type': 'bool', 'init': False}, {'name': 'temp', 'type': 'float', 'init': 21.5}],
               'drivers': [{'type': 'sim', 'items': [{'tag': 'temp', 'wave': 'sine', 'min': 20, 'max': 30, 'period_s': 4}]}],
               'data_log': {'groups': [{'name': 'line1', 'tags': ['temp'], 'interval_ms': 200}]},
               'alarms': {'items': [{'name': 'hot', 'tag': 'hot', 'message': 'タンクの温度が高い', 'severity': 'critical'}]},
               'uploads': [upload], 'screens': ['screens/s.json']}
    json.dump(project, open(os.path.join(D, 'project.json'), 'w', encoding='utf-8'), ensure_ascii=False)
    json.dump({'name': 's', 'children': [{'type': 'button', 'x': 16, 'y': 16, 'w': 200, 'h': 60, 'text': 'send', 'on_click': {'action': 'call', 'fn': 'upload.run', 'arg': upload['name']}}]},
              open(os.path.join(D, 'screens', 's.json'), 'w', encoding='utf-8'))
    lp = os.path.join(D, 'project.local.json')
    if local: json.dump({'uploads': {upload['name']: local}}, open(lp, 'w', encoding='utf-8'))
    elif os.path.exists(lp): os.remove(lp)
    args = [RT, os.path.join(D, 'project.json'), '--exit-after', str(seconds * 1000), '--click', '100,40,2500', '--set', 'hot=true']
    env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy')
    out = subprocess.run(args, capture_output=True, env=env, timeout=seconds + 30)
    log = [l.strip() for l in (out.stdout + out.stderr).decode('utf-8', 'replace').splitlines() if 'upload' in l.lower()]
    folder = os.path.join(D, 'logs', 'upload', upload['name'])
    kept = sorted(os.listdir(folder)) if os.path.isdir(folder) else []
    return log, kept

bad = 0
def check(what, ok, detail=''):
    global bad
    if not ok: bad += 1
    print('  %-4s %s%s' % ('ok' if ok else 'FAIL', what, '' if ok else '   ' + str(detail)[:400]))

def looks_like(files):
    """the two logs of a run, as CSV: a data log with rows, the alarms with the alarm of the run"""
    data = [v for n, v in files.items() if '_data-line1_' in n]
    alarms = [v for n, v in files.items() if '_alarms_' in n]
    if len(data) != 1 or len(alarms) != 1: return 'files: %s' % sorted(files)
    d, a = data[0].decode('utf-8-sig'), alarms[0].decode('utf-8-sig')
    if not d.startswith('time,temp\r\n') or d.count('\r\n') < 5: return 'data log: %r' % d[:120]
    if 'タンクの温度が高い' not in a or 'raised' not in a: return 'alarms: %r' % a[:200]
    return ''

if not wsl('ls', SERVER_DIR + '/servers.py'):
    print('no servers.py in %s (WSL): see the README' % SERVER_DIR); sys.exit(2)
logs = ['data:line1', 'alarms']

print('HTTP PUT (8080, no login)')
clear_received()
log, kept = run({'name': 'plain', 'url': 'http://localhost:8080/hml/', 'logs': logs, 'retries': 0})
got = received(8080)
check('both files arrive, as they were written', looks_like(got) == '', looks_like(got) or log)
check('names: <project>_<log>_<time>.csv', all(n.startswith('Line_1_') and n.endswith('.csv') for n in got), sorted(got))
check('nothing is left waiting', kept == [], kept)
check('said in the log: not secured', any('not secured' in l for l in log), log)

print('HTTPS PUT (8443), the login from project.local.json')
clear_received()
log, kept = run({'name': 'secure', 'url': 'https://localhost:8443/hml/', 'username': 'hmi', 'ca_file': CA, 'logs': logs, 'retries': 0}, local={'password': 's3cret-pw'})
got = received(8443)
check('both files arrive', looks_like(got) == '', looks_like(got) or log)
check('nothing is left waiting; the password is not in the output', kept == [] and not any('s3cret-pw' in l for l in log), (kept, log))

print('HTTPS POST (8443)')
clear_received()
log, kept = run({'name': 'api', 'url': 'https://localhost:8443/api/upload', 'method': 'post', 'username': 'hmi', 'ca_file': CA, 'logs': logs, 'retries': 0}, local={'password': 's3cret-pw'})
got = received(8443)
check('both files arrive under their names', looks_like(got) == '', looks_like(got) or log)

print('HTTPS: a wrong password')
clear_received()
log, kept = run({'name': 'secure', 'url': 'https://localhost:8443/hml/', 'username': 'hmi', 'ca_file': CA, 'logs': logs, 'retries': 0}, local={'password': 'wrong'})
check('nothing arrives', received(8443) == {}, sorted(received(8443)))
check('said: the server does not let us in', any('does not let us in' in l for l in log), log)
check('the files are kept for the next run', len(kept) == 2, kept)
print('  ... the right password, the same folder: what waited is sent after the restart, and the new run too')
log, kept = run({'name': 'secure', 'url': 'https://localhost:8443/hml/', 'username': 'hmi', 'ca_file': CA, 'logs': logs, 'retries': 0}, local={'password': 's3cret-pw'}, keep_folder=True)
got = received(8443)
check('four files at the server (two from before, two new)', len(got) == 4, sorted(got))
check('nothing is left waiting', kept == [], kept)

print('HTTPS: a certificate that is not trusted (no CA file: the system does not know the test CA)')
clear_received()
log, kept = run({'name': 'secure', 'url': 'https://localhost:8443/hml/', 'username': 'hmi', 'logs': logs, 'retries': 0}, local={'password': 's3cret-pw'})
check('nothing arrives, the files are kept', received(8443) == {} and len(kept) == 2, (sorted(received(8443)), kept))
check('said: TLS', any('TLS' in l for l in log), log)

print('HTTP: the server has no room (507), the server sends us elsewhere (301)')
clear_received()
log, kept = run({'name': 'full', 'url': 'http://localhost:8080/full/', 'logs': logs, 'retries': 0})
check('507: said, kept', any('cannot take the file now' in l for l in log) and len(kept) == 2, (log, kept))
log, kept = run({'name': 'moved', 'url': 'http://localhost:8080/moved/', 'logs': logs, 'retries': 0})
check('301: not followed, said, kept', any('sends us elsewhere' in l for l in log) and len(kept) == 2, (log, kept))

print('FTP (2121, passive), the login from project.local.json')
clear_received()
log, kept = run({'name': 'nas', 'url': 'ftp://localhost:2121/incoming/', 'username': 'hmi', 'logs': logs, 'retries': 0}, local={'password': 's3cret-pw'})
got = received(2121, '/incoming')
check('both files arrive in the directory, byte for byte a CSV', looks_like(got) == '', looks_like(got) or log)
check('nothing is left waiting', kept == [], kept)
check('said in the log: not secured', any('not secured' in l for l in log), log)

print('FTP: a wrong password, a directory that is not there, one we may not write to')
clear_received()
log, kept = run({'name': 'nas', 'url': 'ftp://localhost:2121/incoming/', 'username': 'hmi', 'logs': logs, 'retries': 0}, local={'password': 'wrong'})
check('wrong password: said, nothing arrives, kept', any('password was not accepted' in l for l in log) and received(2121, '/incoming') == {} and len(kept) == 2, (log, kept))
log, kept = run({'name': 'nas', 'url': 'ftp://localhost:2121/nowhere/', 'username': 'hmi', 'logs': logs, 'retries': 0}, local={'password': 's3cret-pw'})
check('no such directory: said, kept', any('directory is not there' in l for l in log) and len(kept) == 2, (log, kept))
log, kept = run({'name': 'nas', 'url': 'ftp://localhost:2121/readonly/', 'username': 'hmi', 'logs': logs, 'retries': 0}, local={'password': 's3cret-pw'})
check('no permission: said, kept', any('does not take the file' in l for l in log) and len(kept) == 2 and received(2121, '/readonly') == {}, (log, kept))

print('nobody there (port 8099): the files wait and are tried again')
log, kept = run({'name': 'gone', 'url': 'http://localhost:8099/hml/', 'logs': logs, 'retries': 3, 'retry_s': 60})
check('kept; said: tried again', len(kept) == 2 and any('tried again' in l for l in log), (log, kept))

print('%d failed' % bad)
sys.exit(1 if bad else 0)
