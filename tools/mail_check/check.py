"""HML's mail against a mail server that is not ours (aiosmtpd, started with smtpd.py in WSL): each
case writes a project, runs the runtime for a few seconds and reads what the server took.

  python -I tools/mail_check/check.py <the server's folder in WSL> <ca.crt on this machine>

See the README ("メールで知らせる")."""
import json, os, shutil, subprocess, sys, time
ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
SERVER_DIR = sys.argv[1] if len(sys.argv) > 1 else '/home/user/hml-mailtest'
RT = os.path.join(ROOT, 'build', 'bin', 'hml_runtime.exe')
D = os.path.join(ROOT, 'build', 'scratch', 'mailpc')
CA = os.path.abspath(sys.argv[2] if len(sys.argv) > 2 else os.path.join(ROOT, 'build', 'scratch', 'mail_ca.crt')).replace('\\', '/')

def server_mails():
    r = subprocess.run(['wsl', '--', 'cat', SERVER_DIR + '/mails.jsonl'], capture_output=True, env=dict(os.environ, MSYS_NO_PATHCONV='1'))
    return [json.loads(l) for l in r.stdout.decode('utf-8', 'replace').splitlines() if l.strip()]

def run(mail, local=None, sets=(), click=True, seconds=7):
    shutil.rmtree(D, ignore_errors=True)
    os.makedirs(os.path.join(D, 'screens'))
    project = {'title': 'ライン 1', 'display': {'width': 480, 'height': 320}, 'start_screen': 's',
               'tags': [{'name': 'hot', 'type': 'bool', 'init': False}, {'name': 'level', 'type': 'float', 'init': 87.5}],
               'alarms': {'items': [{'name': 'hot', 'tag': 'hot', 'message': 'タンクの温度が高い', 'severity': 'critical'}]},
               'mail': mail, 'screens': ['screens/s.json']}
    json.dump(project, open(os.path.join(D, 'project.json'), 'w', encoding='utf-8'), ensure_ascii=False)
    json.dump({'name': 's', 'children': [{'type': 'button', 'x': 16, 'y': 16, 'w': 200, 'h': 60, 'text': 'send',
               'on_click': {'action': 'call', 'fn': 'mail.send', 'arg': '液位の報告 {level} % ({project})\n液位は {level} % です。\n.\n2 行目: <b>太字ではない</b> & 記号 "\'\nおわり'}}]},
              open(os.path.join(D, 'screens', 's.json'), 'w', encoding='utf-8'), ensure_ascii=False)
    if local: json.dump({'mail': local}, open(os.path.join(D, 'project.local.json'), 'w', encoding='utf-8'))
    before = len(server_mails())
    args = [RT, os.path.join(D, 'project.json'), '--exit-after', str(seconds * 1000)]
    if click: args += ['--click', '100,40,800']
    for s in sets: args += ['--set', s]
    env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy')
    out = subprocess.run(args, capture_output=True, env=env, timeout=seconds + 30)
    log = (out.stdout + out.stderr).decode('utf-8', 'replace')
    lines = [l.strip() for l in log.splitlines() if 'mail' in l.lower()]
    left = os.path.exists(os.path.join(D, 'logs', 'mail_queue.json'))
    return server_mails()[before:], lines, left

bad = 0
def check(what, ok, detail=''):
    global bad
    if not ok: bad += 1
    print('  %-4s %s%s' % ('ok' if ok else 'FAIL', what, '' if ok else '   ' + str(detail)[:300]))

base = {'host': 'localhost', 'from': 'hmi@plant.example', 'to': ['shift@plant.example'], 'ca_file': CA, 'timeout_ms': 5000, 'retries': 0}
SUBJECT = '液位の報告 87.5 % (ライン 1)'
BODY = '液位は 87.5 % です。\n.\n2 行目: <b>太字ではない</b> & 記号 "\'\nおわり'

print('STARTTLS (2587), the login from project.local.json')
got, log, left = run(dict(base, port=2587, security='starttls', username='hmi'), local={'password': 's3cret-pw'})
check('one mail arrives', len(got) == 1, log)
if got:
    m = got[0]
    check('through TLS (the server takes mail on this port only after a login: see the cases below)', m['tls'] is True, {k: m[k] for k in ('port', 'tls')})
    check('the subject as it was written (Japanese, a tag value)', m['subject'] == SUBJECT, m['subject'])
    check('the body as it was written (a lone ".", markup, quotes)', m['body'].replace('\r\n', '\n').rstrip('\n') == BODY, repr(m['body']))
    check('sender and recipient', m['from'] == 'hmi@plant.example' and m['to'] == ['shift@plant.example'], m)
    check('the headers of a mail, each once', sorted(h.lower() for h in m['headers']) == sorted(['date', 'from', 'to', 'subject', 'message-id', 'mime-version', 'content-type', 'content-transfer-encoding']), m['headers'])
check('nothing is left waiting', not left)
check('the password is not in the output', not any('s3cret-pw' in l for l in log), log)

print('TLS from the first byte (2465)')
got, log, left = run(dict(base, port=2465, security='tls', username='hmi'), local={'password': 's3cret-pw'})
check('one mail arrives (this port speaks TLS only)', len(got) == 1 and got[0]['port'] == 2465 and got[0]['subject'] == SUBJECT, log)

print('plain, no login (2525)')
got, log, left = run(dict(base, port=2525, security='none'))
check('one mail arrives, not through TLS', len(got) == 1 and not got[0]['tls'], log)
check('said in the log: not secured', any('not secured' in l for l in log), log)

print('an alarm (2587)')
got, log, left = run(dict(base, port=2587, security='starttls', username='hmi', alarms={'body': '{alarm.message} / 液位 {level} % / {alarm.event}'}), local={'password': 's3cret-pw'}, sets=['hot=true'], click=False)
check('the alarm is mailed', len(got) == 1, log)
if got:
    check('subject: project, severity, message', got[0]['subject'] == '[ライン 1] critical: タンクの温度が高い', got[0]['subject'])
    check('body: the alarm and a tag value', got[0]['body'].strip() == 'タンクの温度が高い / 液位 87.5 % / raised', repr(got[0]['body']))

print('a wrong password (2587)')
got, log, left = run(dict(base, port=2587, security='starttls', username='hmi'), local={'password': 'wrong'})
check('nothing arrives', len(got) == 0, got)
check('said: the user name or the password was not accepted', any('user name or the password was not accepted' in l for l in log), log)
check('not tried again, nothing left waiting', not left)

print('no login where one is required (2587)')
got, log, left = run(dict(base, port=2587, security='starttls'))
check('nothing arrives, and the reason is said', len(got) == 0 and any('not sent' in l for l in log), log)

print('a certificate that is not trusted (2587 without the CA file: the system does not know the test CA)')
got, log, left = run(dict(base, port=2587, security='starttls', username='hmi', ca_file=''), local={'password': 's3cret-pw'})
check('nothing arrives', len(got) == 0, got)
check('said: the TLS handshake failed', any('TLS' in l for l in log), log)

print('STARTTLS asked of the plain port (2525): the login must not leave')
got, log, left = run(dict(base, port=2525, security='starttls', username='hmi'), local={'password': 's3cret-pw'})
check('nothing arrives, said why', len(got) == 0 and any('STARTTLS' in l for l in log), log)

print('recipients: one unknown, one busy, one good (2587)')
got, log, left = run(dict(base, port=2587, security='starttls', username='hmi', to=['nobody@plant.example', 'busy@plant.example', 'shift@plant.example']), local={'password': 's3cret-pw'})
check('the mail arrives for the one the server takes', len(got) == 1 and got[0]['to'] == ['shift@plant.example'], got or log)
check('the two refused are named in the log', sum('did not take the recipient' in l for l in log) == 2, log)

print('the server is not there (port 2599): the mail waits')
got, log, left = run(dict(base, port=2599, security='starttls', retries=3, retry_s=60), seconds=9)
check('nothing arrives; the mail is kept for later', len(got) == 0 and left, log)
check('said: tried again', any('tried again' in l for l in log), log)

print('%d failed' % bad)
sys.exit(1 if bad else 0)
