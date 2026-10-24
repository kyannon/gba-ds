#!/usr/bin/env python3
"""Stand-in for GitHub's latest-release API, for testing GBA DS's updater
offline (docs/TESTING.md, "Updates"):

  python3 tools/update_mock.py --zip RELEASE.zip [--version 9.9.9] [--port 8098]
                               [--bad-digest] [--slow] [--status 200]

then run GBA DS with
  --update-url=http://127.0.0.1:8098/repos/kyannon/gba-ds/releases/latest

GET /repos/kyannon/gba-ds/releases/latest answers like GitHub (tag_name,
body, assets with name / size / digest / browser_download_url); the asset
is the zip given, served from /download/<name>. --slow sends it in pieces
over a few seconds (to see the progress bar), --bad-digest gives a wrong
checksum, --status answers the API with that code instead.
"""
import argparse, hashlib, json, os, time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

ap = argparse.ArgumentParser()
ap.add_argument("--zip", required=True)
ap.add_argument("--version", default="9.9.9")
ap.add_argument("--port", type=int, default=8098)
ap.add_argument("--bad-digest", action="store_true")
ap.add_argument("--slow", action="store_true")
ap.add_argument("--status", type=int, default=200)
args = ap.parse_args()

data = open(args.zip, "rb").read()
name = f"gba-ds-v{args.version}-linux-aarch64.zip"
digest = "sha256:" + ("0" * 64 if args.bad_digest else hashlib.sha256(data).hexdigest())
host = f"http://127.0.0.1:{args.port}"
NOTES = f"""## What's new

- **Updates from the game list:** a test release, v{args.version}.
  - Nothing really changed.
- A [link](https://example.com) and `code` in the notes.

## Fixes

- **Everything:** fixed.

## Install / upgrade

Not shown in the app.
"""


class H(BaseHTTPRequestHandler):
    def log_message(self, fmt, *a):
        print(self.command, self.path, self.headers.get("User-Agent"), flush=True)

    def reply(self, code, body, ctype="application/json"):
        if isinstance(body, str):
            body = body.encode()
        self.send_response(code)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self):
        if self.path == "/repos/kyannon/gba-ds/releases/latest":
            if args.status != 200:
                return self.reply(args.status, json.dumps({"message": "Not Found"}))
            rel = {
                "tag_name": f"v{args.version}", "name": f"v{args.version}", "draft": False, "prerelease": False,
                "body": NOTES,
                "author": {"login": "kyannon", "name": "not an asset"},
                "assets": [
                    {"name": "gba-ds-cocoon-player.json", "size": 10, "digest": "sha256:" + "1" * 64,
                     "browser_download_url": host + "/download/gba-ds-cocoon-player.json",
                     "uploader": {"login": "kyannon"}},
                    {"name": name, "size": len(data), "digest": digest, "content_type": "application/zip",
                     "browser_download_url": host + "/download/" + name, "uploader": {"login": "kyannon", "name": "x"}},
                    {"name": f"gba-ds-v{args.version}-android.apk", "size": 5, "digest": "sha256:" + "2" * 64,
                     "browser_download_url": host + "/download/apk"},
                ],
            }
            return self.reply(200, json.dumps(rel, indent=1))
        if self.path == "/download/" + name:
            self.send_response(200)
            self.send_header("Content-Type", "application/octet-stream")
            self.send_header("Content-Length", str(len(data)))
            self.end_headers()
            step = max(1, len(data) // 20)
            for i in range(0, len(data), step):
                self.wfile.write(data[i:i + step])
                self.wfile.flush()
                if args.slow:
                    time.sleep(0.25)
            return
        self.reply(404, "{}")


print(f"serving {name} ({len(data)} bytes) on {host}", flush=True)
ThreadingHTTPServer(("127.0.0.1", args.port), H).serve_forever()
