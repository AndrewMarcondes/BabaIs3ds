#!/usr/bin/env python3
"""Receives benchmark results from the 3DS (pause menu > Benchmark posts them when sdmc:/3ds/baba/upload.txt holds
this receiver's URL, e.g. http://192.168.1.20:8001/bench) and appends them to a file, default build-host/bench_results.txt.
Only for your own network, only while it runs (Ctrl+C stops it).

    tools/bench_receiver.py [file] [port]
"""
import datetime
import http.server
import os
import sys

OUT = sys.argv[1] if len(sys.argv) > 1 else "build-host/bench_results.txt"
PORT = int(sys.argv[2]) if len(sys.argv) > 2 else 8001
MAX = 64 * 1024  # a benchmark report is ~1KB


class Handler(http.server.BaseHTTPRequestHandler):
    def do_POST(self):
        n = int(self.headers.get("Content-Length") or 0)
        if self.path != "/bench" or n <= 0 or n > MAX:
            self.send_error(400)
            return
        body = self.rfile.read(n).decode("utf-8", "replace")
        stamp = datetime.datetime.now().isoformat(timespec="seconds")
        os.makedirs(os.path.dirname(OUT) or ".", exist_ok=True)
        with open(OUT, "a") as f:
            f.write(f"# received {stamp} from {self.client_address[0]}\n{body.rstrip()}\n")
        print(f"{stamp} {self.client_address[0]}: {len(body)} bytes -> {OUT}", flush=True)
        self.send_response(200)
        self.send_header("Content-Type", "text/plain")
        self.end_headers()
        self.wfile.write(b"ok\n")

    def log_message(self, *args):
        pass  # one line per report above is enough


if __name__ == "__main__":
    print(f"waiting for benchmark reports on port {PORT}, appending to {OUT}", flush=True)
    http.server.ThreadingHTTPServer(("", PORT), Handler).serve_forever()
