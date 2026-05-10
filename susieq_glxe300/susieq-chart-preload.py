#!/usr/bin/env python3
# susieq-chart-preload.py — lataa Pirkanmaan alueen karttatilet offline-käyttöön
# Asennus: /usr/bin/susieq-chart-preload.py
# Ajo:     python3 /usr/bin/susieq-chart-preload.py
#          (tarvitsee internet-yhteyden, aja kotona ennen reissua)

import math
import sys
import time
import urllib.request
from pathlib import Path

TILE_CACHE = Path("/mnt/tfcard/susieq-tiles")
UA = {"User-Agent": "SusieQ-ChartProxy/1.0 (+github.com/jussinippala-cmd/susieq-remote)"}
TILE_SOURCES = {
    "osm":     "https://tile.openstreetmap.org/{z}/{x}/{y}.png",
    "seamark": "https://tiles.openseamap.org/seamark/{z}/{x}/{y}.png",
}

# Pirkanmaa — Näsijärvi, Pyhäjärvi ja ympäristö
LAT_MIN, LAT_MAX = 61.2, 62.0
LON_MIN, LON_MAX = 23.0, 24.5
ZOOM_MIN, ZOOM_MAX = 8, 14
SLEEP = 0.35   # sekuntia per tile (rate-limiting)


def deg2tile(lat: float, lon: float, z: int):
    n = 1 << z
    x = int((lon + 180) / 360 * n)
    lr = math.radians(lat)
    y = int((1 - math.log(math.tan(lr) + 1 / math.cos(lr)) / math.pi) / 2 * n)
    return x, y


def estimate_total() -> int:
    total = 0
    for z in range(ZOOM_MIN, ZOOM_MAX + 1):
        x0, y1 = deg2tile(LAT_MAX, LON_MIN, z)
        x1, y0 = deg2tile(LAT_MIN, LON_MAX, z)
        total += (abs(x1 - x0) + 1) * (abs(y1 - y0) + 1)
    return total * len(TILE_SOURCES)


def download_tiles() -> None:
    fetched = skipped = errors = 0
    est = estimate_total()
    print(f"Arvioitu tileiden määrä: {est} (zoom {ZOOM_MIN}–{ZOOM_MAX}, "
          f"{len(TILE_SOURCES)} lähde{'ttä' if len(TILE_SOURCES) > 1 else ''})")
    print(f"Kohde: {TILE_CACHE}\n")

    try:
        for z in range(ZOOM_MIN, ZOOM_MAX + 1):
            x0, y1 = deg2tile(LAT_MAX, LON_MIN, z)
            x1, y0 = deg2tile(LAT_MIN, LON_MAX, z)
            x0, x1 = min(x0, x1), max(x0, x1)
            y0, y1 = min(y0, y1), max(y0, y1)
            count = (x1 - x0 + 1) * (y1 - y0 + 1)
            print(f"  Zoom {z:2d}: {count:5d} tilet/lähde")

            for source, url_tpl in TILE_SOURCES.items():
                for x in range(x0, x1 + 1):
                    for y in range(y0, y1 + 1):
                        path = TILE_CACHE / source / str(z) / str(x) / f"{y}.png"
                        if path.exists():
                            skipped += 1
                            continue

                        url = url_tpl.format(z=z, x=x, y=y)
                        try:
                            req = urllib.request.Request(url, headers=UA)
                            with urllib.request.urlopen(req, timeout=10) as r:
                                data = r.read()
                            path.parent.mkdir(parents=True, exist_ok=True)
                            path.write_bytes(data)
                            fetched += 1
                            done = fetched + skipped + errors
                            pct = done * 100 // est if est else 0
                            print(f"\r  [{pct:3d}%] ladattu {fetched}, "
                                  f"ohitettu {skipped}, virheitä {errors}  ",
                                  end="", flush=True)
                        except Exception as e:
                            errors += 1
                            print(f"\r  VIRHE {source}/{z}/{x}/{y}: {e}  ")

                        time.sleep(SLEEP)

    except KeyboardInterrupt:
        print("\n\nKeskeytys.")

    print(f"\nValmis. Ladattu: {fetched}, ohitettu: {skipped}, virheitä: {errors}")
    if errors:
        print(f"Voit ajaa skriptin uudelleen — ohitetaan jo ladatut tilet.")


if __name__ == "__main__":
    download_tiles()
