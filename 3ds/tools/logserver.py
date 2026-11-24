#!/usr/bin/env python3
"""Takes the logs a diagnosis build of GBA DS for the 3DS sends
(make -C 3ds 3ds LOG_URL=http://<this machine>:8377/log): each POST to
/log?name=NAME is written to OUT/NAME-<time>.txt and its last lines shown.

    logserver.py [PORT] [OUT]     (8377, build/logs)
"""
import os
import re
import sys
import time
from http.server import BaseHTTPRequestHandler, HTTPServer
from urllib.parse import parse_qs, urlparse

PORT = int(sys.argv[1]) if len(sys.argv) > 1 else 8377
OUT = sys.argv[2] if len(sys.argv) > 2 else os.path.join(os.path.dirname(__file__), '..', '..', 'build', 'logs')


class Handler(BaseHTTPRequestHandler):
    def do_POST(self):
        url = urlparse(self.path)
        name = re.sub(r'[^A-Za-z0-9_-]', '', parse_qs(url.query).get('name', ['log'])[0]) or 'log'
        body = self.rfile.read(int(self.headers.get('Content-Length', 0)))
        os.makedirs(OUT, exist_ok=True)
        path = os.path.join(OUT, '%s-%s.txt' % (name, time.strftime('%Y%m%d-%H%M%S')))
        with open(path, 'wb') as f:
            f.write(body)
        self.send_response(200)
        self.end_headers()
        self.wfile.write(b'ok\n')
        print('%s: %d bytes from %s' % (os.path.normpath(path), len(body), self.client_address[0]), flush=True)
        for line in body.decode('utf-8', 'replace').splitlines()[-12:]:
            print('    ' + line, flush=True)

    def log_message(self, *args):
        pass


print('listening on port %d, writing to %s' % (PORT, os.path.normpath(OUT)), flush=True)
HTTPServer(('0.0.0.0', PORT), Handler).serve_forever()
