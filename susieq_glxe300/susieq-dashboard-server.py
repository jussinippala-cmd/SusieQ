#!/usr/bin/env python3
# susieq-dashboard-server.py — dashboard-palvelin GL-XE300:lle (OpenWrt)
#
# Pollaa cockpitin (192.168.8.100) /data-endpointin 1s välein ja tarjoaa
# dashboardin HTML-sivun + viimeisimmän sensoridatan samasta originista,
# jotta selaimen ei tarvitse pitää yhteyttä auki ESP32:een asti.
#
# Asennus: /usr/bin/susieq-dashboard-server.py
# Käynnistys: /etc/init.d/susieq-dashboard start

import json
import socketserver
import threading
import time
import urllib.request
from http.server import BaseHTTPRequestHandler, HTTPServer
from pathlib import Path
from urllib.parse import urlparse

PORT = 8081
STATIC_DIR = Path("/usr/share/susieq-dashboard")
ESP32_URL = "http://192.168.8.100/data"
POLL_INTERVAL_S = 1.0
UA = {"User-Agent": "SusieQ-DashboardProxy/1.0"}

_cached_json: bytes = b"{}"
# Ei PEP 604 -annotaatiota (`float | None`): se vaatii Python 3.10+, eikä
# modeemin python3-versiota ole varmennettu. Annotaatio ei toisi tässä mitään.
_cached_at = None
_cached_lock = threading.Lock()


def _poll_loop() -> None:
    global _cached_json, _cached_at
    while True:
        try:
            req = urllib.request.Request(ESP32_URL, headers=UA)
            with urllib.request.urlopen(req, timeout=3) as resp:
                data = resp.read()
            json.loads(data)  # validoi ennen julkaisua — hylkää rikkinäinen JSON
            with _cached_lock:
                _cached_json = data
                _cached_at = time.time()
        except Exception:
            pass
        time.sleep(POLL_INTERVAL_S)


class DashboardHandler(BaseHTTPRequestHandler):
    def log_message(self, fmt, *args):
        pass

    def do_GET(self):
        path = urlparse(self.path).path
        if path in ("/", "/index.html", "/dashboard"):
            self._static("index.html", "text/html; charset=utf-8")
        elif path == "/data":
            with _cached_lock:
                raw = _cached_json
                cached_at = _cached_at
            # Ikä mukaan, jotta selain voi kertoa käyttäjälle datan olevan
            # vanhentunutta. Ilman tätä ESP32:n yölepotila näyttäisi
            # dashboardilla jäätyneenä klo 22 datana ilman mitään varoitusta.
            try:
                payload = json.loads(raw)
                payload["proxy_age_s"] = (
                    None if cached_at is None else int(time.time() - cached_at)
                )
                body = json.dumps(payload).encode()
            except Exception:
                body = raw
            self.send_response(200)
            self.send_header("Content-Type", "application/json")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)
        else:
            self.send_error(404)

    def _static(self, filename: str, ctype: str) -> None:
        fpath = STATIC_DIR / filename
        if not fpath.exists():
            self.send_error(404)
            return
        data = fpath.read_bytes()
        self.send_response(200)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(data)))
        self.end_headers()
        self.wfile.write(data)


class _ThreadedHTTPServer(socketserver.ThreadingMixIn, HTTPServer):
    daemon_threads = True

    def server_bind(self):
        socketserver.TCPServer.server_bind(self)
        host, port = self.server_address[:2]
        self.server_name = host or "localhost"
        self.server_port = port


if __name__ == "__main__":
    print(f"SusieQ dashboard server: http://0.0.0.0:{PORT}/")
    print(f"Staattiset tiedostot: {STATIC_DIR}")
    threading.Thread(target=_poll_loop, daemon=True).start()
    _ThreadedHTTPServer(("0.0.0.0", PORT), DashboardHandler).serve_forever()
