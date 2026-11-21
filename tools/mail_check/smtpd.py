"""A mail server for the check of HML's mail (check.py): aiosmtpd - not our code - on three ports.
   Run it in a folder that has pkgs/ (get.py), server.crt and server.key (see the README, "メールで知らせる").
   It listens on this machine only (127.0.0.1); another address as the first argument.
   2525  plain, no login                      ("security": "none")
   2587  STARTTLS, login required             ("security": "starttls")
   2465  TLS from the first byte, login       ("security": "tls")
Every mail taken is appended to mails.jsonl (decoded: subject, body, recipients, port)."""
import asyncio, email, email.policy, json, os, ssl, sys, time
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), 'pkgs'))
from aiosmtpd.controller import Controller
from aiosmtpd.smtp import AuthResult, LoginPassword

HERE = os.path.dirname(os.path.abspath(__file__))
HOST = sys.argv[1] if len(sys.argv) > 1 else '127.0.0.1'
LOG = os.path.join(HERE, 'mails.jsonl')

class Handler:
    def __init__(self, port): self.port = port
    async def handle_RCPT(self, server, session, envelope, address, rcpt_options):
        if address.startswith('nobody@'): return '550 5.1.1 no such user here'
        if address.startswith('busy@'): return '451 4.3.0 try again later'
        envelope.rcpt_tos.append(address)
        return '250 OK'
    async def handle_DATA(self, server, session, envelope):
        msg = email.message_from_bytes(envelope.content, policy=email.policy.default)
        body = msg.get_content() if not msg.is_multipart() else ''
        rec = {'port': self.port, 'from': envelope.mail_from, 'to': envelope.rcpt_tos, 'subject': str(msg['subject']), 'body': body,
               'headers': [k for k in msg.keys()], 'tls': self.port == 2465 or session.ssl is not None}
        with open(LOG, 'a', encoding='utf-8') as f: f.write(json.dumps(rec, ensure_ascii=False) + '\n')
        return '250 Message accepted'

def authenticator(server, session, envelope, mechanism, auth_data):
    ok = isinstance(auth_data, LoginPassword) and auth_data.login == b'hmi' and auth_data.password == b's3cret-pw'
    return AuthResult(success=ok, handled=True) if ok else AuthResult(success=False, handled=False)

ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
ctx.load_cert_chain(os.path.join(HERE, 'server.crt'), os.path.join(HERE, 'server.key'))
servers = [
    Controller(Handler(2525), hostname=HOST, port=2525),
    Controller(Handler(2587), hostname=HOST, port=2587, tls_context=ctx, require_starttls=True, authenticator=authenticator, auth_required=True),
    Controller(Handler(2465), hostname=HOST, port=2465, ssl_context=ctx, authenticator=authenticator, auth_required=True, auth_require_tls=False),
]
for s in servers: s.start()
print('ready', flush=True)
try:
    while True: time.sleep(1)
except KeyboardInterrupt:
    pass
for s in servers: s.stop()
