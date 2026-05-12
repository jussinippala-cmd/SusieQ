#!/usr/bin/env python3
# susieq-chart-server.py — karttaplotteri-palvelin GL-XE300:lle (OpenWrt)
# Asennus: /usr/bin/susieq-chart-server.py
# Käynnistys: /etc/init.d/susieq-chart start

import collections
import json
import socketserver
import threading
import time
import urllib.request
from http.server import BaseHTTPRequestHandler, HTTPServer
from pathlib import Path
from urllib.parse import urlparse

PORT = 8080
STATIC_DIR = Path("/usr/share/susieq-chart")


TILE_CACHE = Path("/mnt/tfcard/susieq-tiles")
ESP32_URL = "http://192.168.8.100/data"
TILE_SOURCES = {
    "osm":     "https://tile.openstreetmap.org/{z}/{x}/{y}.png",
    "seamark": "https://tiles.openseamap.org/seamark/{z}/{x}/{y}.png",
    "merik":   "https://julkinen.traficom.fi/rasteripalvelu/wmts?request=GetTile&version=1.0.0&service=wmts&layer=Traficom:Merikarttasarja%20N&TILEMATRIXSET=WGS84_Pseudo-Mercator&TileMatrix=WGS84_Pseudo-Mercator:{z}&tilerow={y}&tilecol={x}&format=image/png&style=default",
}
UA = {"User-Agent": "SusieQ-ChartProxy/1.0 (+github.com/jussinippala-cmd/susieq-remote)"}

_gps: dict = {}
_gps_lock = threading.Lock()

_tile_mem: collections.OrderedDict = collections.OrderedDict()
_tile_mem_lock = threading.Lock()
TILE_MEM_MAX = 1000

def _tile_mem_get(key: str) -> "bytes | None":
    with _tile_mem_lock:
        if key in _tile_mem:
            _tile_mem.move_to_end(key)
            return _tile_mem[key]
    return None

def _tile_mem_put(key: str, data: bytes) -> None:
    with _tile_mem_lock:
        _tile_mem[key] = data
        _tile_mem.move_to_end(key)
        if len(_tile_mem) > TILE_MEM_MAX:
            _tile_mem.popitem(last=False)


def _poll_gps() -> None:
    global _gps
    while True:
        try:
            req = urllib.request.Request(ESP32_URL, headers=UA)
            with urllib.request.urlopen(req, timeout=3) as resp:
                data = json.loads(resp.read())
                with _gps_lock:
                    _gps = data.get("gps", {})
        except Exception:
            pass
        time.sleep(2)


class ChartHandler(BaseHTTPRequestHandler):
    def log_message(self, fmt, *args):
        pass

    def do_GET(self):
        path = urlparse(self.path).path

        if path in ("/", "/index.html"):
            self._static("index.html", "text/html; charset=utf-8")
        elif path.startswith("/static/"):
            name = path[8:]
            self._static(name, _mime(name))
        elif path == "/gps":
            with _gps_lock:
                gps = dict(_gps)
            gps["ts"] = int(time.time())
            self._json(gps)
        elif path.startswith("/tiles/"):
            self._tile(path[7:])
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

    def _tile(self, tile_path: str) -> None:
        # tile_path: "osm/13/4605/2371.png"
        parts = tile_path.split("/")
        if len(parts) != 4:
            self.send_error(400)
            return
        source, z, x, y_ext = parts
        if source not in TILE_SOURCES or not y_ext.endswith(".png"):
            self.send_error(400)
            return
        y = y_ext[:-4]
        if not (z.isdigit() and x.isdigit() and y.isdigit()):
            self.send_error(400)
            return

        cache_file = TILE_CACHE / source / z / x / f"{y}.png"
        mem_key = f"{source}/{z}/{x}/{y}"

        data = _tile_mem_get(mem_key)
        if data is None:
            if cache_file.exists():
                data = cache_file.read_bytes()
            else:
                url = TILE_SOURCES[source].format(z=z, x=x, y=y)
                try:
                    req = urllib.request.Request(url, headers=UA)
                    with urllib.request.urlopen(req, timeout=8) as resp:
                        data = resp.read()
                    cache_file.parent.mkdir(parents=True, exist_ok=True)
                    cache_file.write_bytes(data)
                except Exception:
                    self.send_error(503)
                    return
            _tile_mem_put(mem_key, data)

        self.send_response(200)
        self.send_header("Content-Type", "image/png")
        self.send_header("Content-Length", str(len(data)))
        self.send_header("Cache-Control", "public, max-age=604800")
        self.end_headers()
        self.wfile.write(data)

    def _json(self, obj: dict) -> None:
        body = json.dumps(obj).encode()
        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Access-Control-Allow-Origin", "*")
        self.end_headers()
        self.wfile.write(body)


def _mime(filename: str) -> str:
    ext = filename.rsplit(".", 1)[-1].lower() if "." in filename else ""
    return {
        "html": "text/html; charset=utf-8",
        "js":   "application/javascript",
        "css":  "text/css",
        "png":  "image/png",
        "ico":  "image/x-icon",
    }.get(ext, "application/octet-stream")


class _ThreadedHTTPServer(socketserver.ThreadingMixIn, HTTPServer):
    daemon_threads = True

    def server_bind(self):
        socketserver.TCPServer.server_bind(self)
        host, port = self.server_address[:2]
        self.server_name = host or "localhost"
        self.server_port = port


if __name__ == "__main__":
    print(f"SusieQ chart server: http://0.0.0.0:{PORT}/")
    print(f"Tile-välimuisti: {TILE_CACHE}")
    print(f"Staattiset tiedostot: {STATIC_DIR}")
    threading.Thread(target=_poll_gps, daemon=True).start()
    _ThreadedHTTPServer(("0.0.0.0", PORT), ChartHandler).serve_forever()
