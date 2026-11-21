"""Servers for the check of HML's uploads (check.py) - none of them our code:
   8080  HTTP   Python's http.server: PUT stores a file, POST stores the body under the name of Content-Disposition
   8443  HTTPS  the same behind TLS (server.crt / server.key of this folder), login hmi / s3cret-pw required
   2121  FTP    pyftpdlib (get.py fetches it into pkgs/), login hmi / s3cret-pw, passive ports 60000-60010
What arrives is kept in ./received/<port>/. They listen on this machine only (127.0.0.1).
Run it in a folder that has server.crt and server.key (see the README, "ログを外に出す")."""
import base64, http.server, os, re, ssl, sys, threading, time
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, 'pkgs'))
HOST = sys.argv[1] if len(sys.argv) > 1 else '127.0.0.1'
OUT = os.path.join(HERE, 'received')

def safe(name): return re.sub(r'[^A-Za-z0-9._-]', '_', os.path.basename(name))[:150] or 'file'

def handler(port, need_login):
    class H(http.server.BaseHTTPRequestHandler):
        protocol_version = 'HTTP/1.1'
        def log_message(self, *a): pass
        def answer(self, code, text=''):
            body = text.encode()
            self.send_response(code)
            self.send_header('Content-Length', str(len(body)))
            self.send_header('Connection', 'close')
            if code == 401: self.send_header('WWW-Authenticate', 'Basic realm="hml"')
            self.end_headers()
            self.wfile.write(body)
        def take(self, name):
            if need_login and self.headers.get('Authorization') != 'Basic ' + base64.b64encode(b'hmi:s3cret-pw').decode(): return self.answer(401, 'login required')
            if self.path.startswith('/full/'): return self.answer(507, 'no room')
            if self.path.startswith('/moved/'):
                self.send_response(301); self.send_header('Location', 'http://127.0.0.1:1/'); self.send_header('Content-Length', '0'); self.end_headers(); return
            n = int(self.headers.get('Content-Length', '0'))
            data = self.rfile.read(n)
            d = os.path.join(OUT, str(port))
            os.makedirs(d, exist_ok=True)
            with open(os.path.join(d, safe(name)), 'wb') as f: f.write(data)
            self.answer(201, 'stored')
        def do_PUT(self): self.take(self.path.rsplit('/', 1)[-1])
        def do_POST(self):
            m = re.search(r'filename="([^"]*)"', self.headers.get('Content-Disposition', ''))
            self.take(m.group(1) if m else 'posted')
    return H

servers = []
plain = http.server.ThreadingHTTPServer((HOST, 8080), handler(8080, False))
servers.append(plain)
tls = http.server.ThreadingHTTPServer((HOST, 8443), handler(8443, True))
ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
ctx.load_cert_chain(os.path.join(HERE, 'server.crt'), os.path.join(HERE, 'server.key'))
tls.socket = ctx.wrap_socket(tls.socket, server_side=True)
servers.append(tls)
for s in servers: threading.Thread(target=s.serve_forever, daemon=True).start()

ftp = None
try:
    from pyftpdlib.authorizers import DummyAuthorizer
    from pyftpdlib.handlers import FTPHandler
    from pyftpdlib.servers import FTPServer
    home = os.path.join(OUT, '2121')
    os.makedirs(os.path.join(home, 'incoming'), exist_ok=True)
    os.makedirs(os.path.join(home, 'readonly'), exist_ok=True)
    auth = DummyAuthorizer()
    auth.add_user('hmi', 's3cret-pw', home, perm='elw')              # list, change directory, store
    auth.override_perm('hmi', os.path.join(home, 'readonly'), 'el')
    class Handler(FTPHandler): pass
    Handler.authorizer = auth
    Handler.passive_ports = range(60000, 60011)
    Handler.banner = 'pyftpdlib for the HML upload check'
    ftp = FTPServer((HOST, 2121), Handler)
    threading.Thread(target=ftp.serve_forever, daemon=True).start()
except ImportError:
    print('no pyftpdlib (python3 get.py pyftpdlib): the FTP server is left out', flush=True)

print('ready', flush=True)
try:
    while True: time.sleep(1)
except KeyboardInterrupt:
    pass
