# Dashboard-hosting modeemille Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Siirrä cockpit-dashboardin sivunjako ja reaaliaikadata GL-XE300-modeemille (kuten karttaplotteri toimii jo nyt), jotta ESP32 ei enää tarvitse pitää yllä WebSocket-yhteyksiä tai tarjota raskasta HTML/JS-sivua — ESP32:n web-pinta-ala kapenee käytännössä sensoridataan (`/data`) ja kalibrointitoimintoihin.

**Architecture:** GL-XE300:lle lisätään uusi Python-palvelu (`susieq-dashboard-server.py`, malli `susieq-chart-server.py`:stä), joka pollaa ESP32:n `/data`-endpointia 1s välein samassa LAN:issa, välimuistittaa viimeisimmän JSON:in, ja tarjoaa sekä dashboard-HTML:n että `/data`-proxyn samasta originista (`http://192.168.8.1:8081/`). ESP32:lta poistetaan AsyncWebSocket-palvelin ja `"/"`-sivunjako kokonaan; jäljelle jäävät kalibrointi-/debug-endpointit saavat CORS-headerit, jotta modeemilta tarjottava dashboard voi kutsua niitä suoraan ESP32:lle.

**Tech Stack:** Python 3 stdlib (`http.server`, `socketserver`, `urllib.request`) GL-XE300:lla (OpenWrt, procd-palvelu) — sama tyyli kuin `susieq-chart-server.py`. ESP32-puolella ESPAsyncWebServer (`DefaultHeaders`). Selainpuolen JS on vanilla ES5 (sama tyyli kuin `data/index.html`:ssä nyt).

## Global Constraints

- ESP32 cockpit on osoitteessa `192.168.8.100`, GL-XE300 osoitteessa `192.168.8.1`.
- Modeemin uusi dashboard-palvelu käyttää porttia **8081** (plotteri on jo portissa 8080, ei saa törmätä).
- Dashboardin tavoite-reaaliaikaisuus: **1000 ms** pollausväli päästä päähän (modeemi→ESP32 ja selain→modeemi).
- ESP32:n web-pinta-ala kalibroinnin/debugin osalta säilyy (`/tare`, `/tare_water`, `/tare_rum`, `/calibrate_rum`, `/rum_raw`, `/records`, `/victron-debug`) — vain `"/"`-sivunjako ja WebSocket poistuvat.
- Ei idle-/on-demand-skaalausta tässä suunnitelmassa (päätetty erikseen aiemmin) — palvelu pyörii aina päällä, kuten plotterikin.
- Sovellettava vain kun ollaan SusieQ-Net-verkossa tai etänä GoodCloudin kautta (ks. projektin CLAUDE.md, kysy käyttäjältä työtapa ennen deployta).

---

## File Structure

- `susieq_glxe300/susieq-dashboard-server.py` — uusi Python-palvelu: pollaa ESP32:n `/data`:n 1s välein, tarjoaa dashboard-HTML:n ja `/data`-proxyn
- `susieq_glxe300/susieq-dashboard-init.d` — procd-palvelumääritys (malli `susieq-chart-init.d`:stä)
- `susieq_glxe300/susieq-dashboard-setup.sh` — ensiasennusskripti (malli `susieq-chart-setup.sh`:stä)
- `susieq_glxe300/dashboard/index.html` — siirretty `susieq_dashboard/data/index.html`:stä, WS-logiikka korvattu 1s-pollauksella, kalibrointikutsut osoittavat suoraan ESP32:een
- `susieq_dashboard/src/main.cpp` — poistetaan `AsyncWebSocket`, `"/"`-sivunjako, `serveStatic`; lisätään CORS-headerit
- `susieq_dashboard/include/config.h` — `SENSOR_INTERVAL_MS` 2000 → 1000

---

### Task 1: GL-XE300 dashboard-palvelin

**Files:**
- Create: `susieq_glxe300/susieq-dashboard-server.py`
- Create: `susieq_glxe300/susieq-dashboard-init.d`
- Create: `susieq_glxe300/susieq-dashboard-setup.sh`

**Interfaces:**
- Produces: `GET http://<host>:8081/` ja `/dashboard` → dashboard-HTML `STATIC_DIR / "index.html"`:stä
- Produces: `GET http://<host>:8081/data` → uusin `_cached_json` (bytes), `Content-Type: application/json`
- Consumes (Task 3:ssa): ESP32:n `GET http://192.168.8.100/data` (muuttumaton sopimus — sama JSON-muoto kuin `build_json()` tuottaa)

- [ ] **Step 1: Kirjoita `susieq_glxe300/susieq-dashboard-server.py`**

```python
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
_cached_lock = threading.Lock()


def _poll_loop() -> None:
    global _cached_json
    while True:
        try:
            req = urllib.request.Request(ESP32_URL, headers=UA)
            with urllib.request.urlopen(req, timeout=3) as resp:
                data = resp.read()
            json.loads(data)  # validoi ennen julkaisua — hylkää rikkinäinen JSON
            with _cached_lock:
                _cached_json = data
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
                body = _cached_json
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
```

- [ ] **Step 2: Kirjoita `susieq_glxe300/susieq-dashboard-init.d`**

```sh
#!/bin/sh /etc/rc.common
# susieq-dashboard — dashboard-palvelin (OpenWrt procd-palvelu)
# Asennus: /etc/init.d/susieq-dashboard
# chmod +x /etc/init.d/susieq-dashboard
# /etc/init.d/susieq-dashboard enable
# /etc/init.d/susieq-dashboard start

USE_PROCD=1
START=95
STOP=10

start_service() {
    procd_open_instance
    procd_set_param command /usr/bin/python3 /usr/bin/susieq-dashboard-server.py
    # Käynnistä automaattisesti uudelleen kaatumisen jälkeen
    procd_set_param respawn 3600 5 0
    procd_set_param stdout 1
    procd_set_param stderr 1
    procd_close_instance
}
```

- [ ] **Step 3: Kirjoita `susieq_glxe300/susieq-dashboard-setup.sh`**

```sh
#!/bin/sh
# susieq-dashboard-setup.sh — ensimmäisen kerran asennus GL-XE300:lla
# Ajo: ssh root@192.168.8.1, sitten sh /tmp/susieq-dashboard-setup.sh

set -e

echo "=== SusieQ dashboard-palvelin — asennus ==="

mkdir -p /usr/share/susieq-dashboard
echo "Hakemisto luotu."

if [ -f /usr/bin/susieq-dashboard-server.py ]; then
    chmod +x /usr/bin/susieq-dashboard-server.py
fi

if [ -f /etc/init.d/susieq-dashboard ]; then
    chmod +x /etc/init.d/susieq-dashboard
    /etc/init.d/susieq-dashboard enable
    /etc/init.d/susieq-dashboard start
    echo "Palvelu käynnistetty."
fi

echo ""
echo "Valmis! Dashboard: http://192.168.8.1:8081/"
echo "Datatesti: curl http://localhost:8081/data"
```

- [ ] **Step 4: Aja palvelin paikallisesti Macilla ja varmista perustoiminta (ei vaadi venettä)**

```bash
cd susieq_glxe300 && mkdir -p /tmp/susieq-dashboard-test && cp -r . /tmp/susieq-dashboard-test 2>/dev/null; \
STATIC_DIR_OVERRIDE=/tmp/susieq-dashboard-test python3 -c "
import susieq_dashboard_server
" 2>/dev/null; \
python3 susieq-dashboard-server.py &
sleep 1
curl -s http://localhost:8081/data
kill %1
```

Expected: `curl /data` palauttaa `{}` (ESP32 ei ole tavoitettavissa Macilta, poll epäonnistuu hiljaa ja oletusarvo `{}` jää voimaan) — tämä vahvistaa, että HTTP-palvelin käynnistyy ja vastaa, vaikka cockpit ei ole verkossa. `STATIC_DIR`-polku (`/usr/share/susieq-dashboard`) ei ole olemassa Macilla, joten `curl http://localhost:8081/` palauttaa 404 — se on odotettua tässä vaiheessa (Task 2 lisää oikean tiedoston, ja tuotantoympäristössä `/usr/share/susieq-dashboard/index.html` on olemassa).

- [ ] **Step 5: Commit**

```bash
git add susieq_glxe300/susieq-dashboard-server.py susieq_glxe300/susieq-dashboard-init.d susieq_glxe300/susieq-dashboard-setup.sh
git commit -m "feat(glxe300): lisää dashboard-proxy-palvelin modeemille"
```

---

### Task 2: Dashboardin HTML/JS siirto modeemille — pollaus + absoluuttiset ESP32-kutsut

**Files:**
- Move: `susieq_dashboard/data/index.html` → `susieq_glxe300/dashboard/index.html`
- Modify: `susieq_glxe300/dashboard/index.html` (WS-logiikan korvaus, kalibrointikutsujen URL:t)

**Interfaces:**
- Consumes: `GET /data` samasta originista (Task 1:n `DashboardHandler` palvelee tätä modeemilla)
- Consumes: `ESP32_BASE = "http://192.168.8.100"` — absoluuttinen base-URL kalibrointikutsuille (`/tare`, `/tare_water`, `/tare_rum`, `/records`)

- [ ] **Step 1: Siirrä tiedosto**

```bash
mkdir -p susieq_glxe300/dashboard
git mv susieq_dashboard/data/index.html susieq_glxe300/dashboard/index.html
```

- [ ] **Step 2: Korvaa WebSocket-logiikka 1s-pollauksella**

Tiedostossa `susieq_glxe300/dashboard/index.html`, etsi rivit (entiset main.cpp-rivinumerot `data/index.html`:ssä, tarkista nykyiset rivit `grep -n "─── State"` jälkeen siirron):

```javascript
// ─── State ────────────────────────────────────────────────────────────
var ws;
var reconnectDelay = 1000;
var lastMsgTime = 0;
var staleTimer;
var _wsReconnecting = false;  // L: prevent onclose+onerror double-fire

// ─── WebSocket connection ──────────────────────────────────────────────
function connect() {
  _wsReconnecting = false;
  try {
    var host = location.hostname || "192.168.8.100";
    ws = new WebSocket("ws://" + host + "/ws");
  } catch (e) {
    console.error("WebSocket connect error", e);
    _wsReconnecting = true;
    var jitter = Math.random() * 500;
    setTimeout(connect, reconnectDelay + jitter);
    reconnectDelay = Math.min(reconnectDelay * 1.5, 15000);
    return;
  }

  ws.onopen = function() {
    document.getElementById("dot").classList.add("live");
    document.getElementById("status-text").textContent = "Live";
    reconnectDelay = 1000;
  };

  ws.onmessage = function(evt) {
    lastMsgTime = Date.now();
    clearTimeout(staleTimer);
    staleTimer = setTimeout(markStale, 8000);
    try {
      update(JSON.parse(evt.data));
    } catch (e) {
      console.error("JSON parse error", e);
    }
  };

  ws.onclose = ws.onerror = function() {
    if (_wsReconnecting) return;  // L: prevent double-fire
    _wsReconnecting = true;
    clearTimeout(staleTimer);
    document.getElementById("dot").classList.remove("live");
    document.getElementById("status-text").textContent = "Yhdistetään…";
    var jitter = Math.random() * 500;
    setTimeout(connect, reconnectDelay + jitter);
    reconnectDelay = Math.min(reconnectDelay * 1.5, 15000);
  };
}
```

Korvaa koko lohko tällä:

```javascript
// ─── State ────────────────────────────────────────────────────────────
var ESP32_BASE = "http://192.168.8.100";
var POLL_INTERVAL_MS = 1000;

// ─── Live data polling (modeemin /data, päivittyy 1s välein) ──────────
function pollData() {
  fetchWithTimeout("/data", { cache: "no-store" }, 4000)
    .then(function(r) {
      if (!r.ok) throw new Error("HTTP " + r.status);
      return r.json();
    })
    .then(function(d) {
      document.getElementById("dot").classList.add("live");
      document.getElementById("status-text").textContent = "Live";
      update(d);
    })
    .catch(function(e) {
      console.error("poll /data error", e);
      markStale();
    });
}

function startPolling() {
  pollData();
  setInterval(pollData, POLL_INTERVAL_MS);
}
```

`markStale()`-funktio (heti tämän lohkon jälkeen tiedostossa) pysyy muuttumattomana — sitä kutsutaan nyt suoraan `pollData()`:n `.catch()`-haarasta, ei enää `setTimeout`-ajastimen kautta.

- [ ] **Step 3: Vaihda käynnistyskutsu**

Etsi tiedoston lopusta:

```javascript
connect();
```

Korvaa:

```javascript
startPolling();
```

- [ ] **Step 4: Osoita kalibrointikutsut suoraan ESP32:lle**

Etsi ja korvaa nämä neljä `fetchWithTimeout`-kutsua (samat funktiot, vain URL muuttuu absoluuttiseksi):

```javascript
fetchWithTimeout("/tare", { method: "POST" }, 5000)
```
→
```javascript
fetchWithTimeout(ESP32_BASE + "/tare", { method: "POST" }, 5000)
```

```javascript
fetchWithTimeout("/tare_water", { method: "POST" }, 5000)
```
→
```javascript
fetchWithTimeout(ESP32_BASE + "/tare_water", { method: "POST" }, 5000)
```

```javascript
fetchWithTimeout("/tare_rum", { method: "POST" }, 5000)
```
→
```javascript
fetchWithTimeout(ESP32_BASE + "/tare_rum", { method: "POST" }, 5000)
```

```javascript
fetchWithTimeout("/records", {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify(_records)
  }, 5000).catch(function() {});
```
→
```javascript
fetchWithTimeout(ESP32_BASE + "/records", {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify(_records)
  }, 5000).catch(function() {});
```

```javascript
fetchWithTimeout("/records", {}, 5000)
```
→
```javascript
fetchWithTimeout(ESP32_BASE + "/records", {}, 5000)
```

- [ ] **Step 5: Tarkista, ettei jäljellä ole muita suhteellisia ESP32-kutsuja**

```bash
grep -n "fetchWithTimeout(\"/" susieq_glxe300/dashboard/index.html
```

Expected: ei tulosta (kaikki neljä on muutettu absoluuttisiksi `ESP32_BASE`-etuliitteellä; `/data`-kutsu jää tahallaan suhteelliseksi, koska se palvelee samasta originista modeemilta).

- [ ] **Step 6: Commit**

```bash
git add susieq_glxe300/dashboard/index.html
git commit -m "feat(dashboard): siirrä dashboard-HTML modeemille, korvaa WS 1s-pollauksella"
```

---

### Task 3: ESP32 — poista WebSocket ja sivunjako, lisää CORS

**Files:**
- Modify: `susieq_dashboard/src/main.cpp`
- Modify: `susieq_dashboard/include/config.h:21`

**Interfaces:**
- Produces: `GET /data` (muuttumaton — kuluttajat: Task 1:n poll-loop, vanha cron-skripti `susieq-sensors.sh`)
- Produces: `Access-Control-Allow-Origin: *` kaikissa vastauksissa (mahdollistaa Task 2:n absoluuttiset kutsut eri originista)
- Produces: `OPTIONS /records` → `200` (CORS-preflight, koska selain lähettää `Content-Type: application/json`)

- [ ] **Step 1: Poista `AsyncWebSocket`-deklaraatio**

Tiedostossa `susieq_dashboard/src/main.cpp`, etsi:

```cpp
AsyncWebServer  server(80);
AsyncWebSocket  ws("/ws");
```

Korvaa:

```cpp
AsyncWebServer  server(80);
```

- [ ] **Step 2: Poista `on_ws_event`-funktio**

Poista kokonaan tämä lohko (komentti + funktio):

```cpp
// ─── WebSocket event handler ──────────────────────────────────────────
void on_ws_event(AsyncWebSocket* server, AsyncWebSocketClient* client,
                 AwsEventType type, void* arg, uint8_t* data, size_t len) {
    if (type == WS_EVT_CONNECT) {
        Serial.printf("[ws] client #%u connected from %s\n",
                      client->id(), client->remoteIP().toString().c_str());
        // Send cached readings immediately on connect.
        // Take hx711_mutex to avoid racing loop()'s cached_json reassignment.
        String snapshot;
        if (xSemaphoreTake(hx711_mutex, pdMS_TO_TICKS(50))) {
            snapshot = cached_json;
            xSemaphoreGive(hx711_mutex);
        } else {
            snapshot = cached_json;
        }
        if (snapshot.length() > 0) client->text(snapshot);
    } else if (type == WS_EVT_DISCONNECT) {
        Serial.printf("[ws] client #%u disconnected\n", client->id());
    }
}
```

- [ ] **Step 3: Korvaa `"/"`-sivunjako tekstiviestillä, poista `serveStatic`**

Etsi:

```cpp
// ─── HTTP routes ──────────────────────────────────────────────────────
static void setup_routes() {
    // Serve HTML dashboard from LittleFS
    server.on("/", HTTP_GET, [](AsyncWebServerRequest* req) {
        req->send(LittleFS, "/index.html", "text/html");
    });
```

Korvaa:

```cpp
// ─── HTTP routes ──────────────────────────────────────────────────────
static void setup_routes() {
    // Dashboard siirretty modeemille — ks. susieq_glxe300/susieq-dashboard-server.py
    server.on("/", HTTP_GET, [](AsyncWebServerRequest* req) {
        req->send(200, "text/plain", "Dashboard: http://192.168.8.1:8081/");
    });
```

Etsi ja poista (rivi 267):

```cpp
    server.serveStatic("/", LittleFS, "/");
```

- [ ] **Step 4: Lisää CORS-headerit ja `/records`-preflight**

Etsi `/records`-reitit:

```cpp
    // GET /records → lue /records.json LittleFS:stä
    server.on("/records", HTTP_GET, [](AsyncWebServerRequest* req) {
```

Lisää juuri ennen tätä:

```cpp
    // CORS-preflight /records:lle — selain lähettää Content-Type: application/json,
    // joka laukaisee OPTIONS-esipyynnön kun dashboard pyörii eri originissa (modeemi).
    server.on("/records", HTTP_OPTIONS, [](AsyncWebServerRequest* req) {
        req->send(200);
    });

    // GET /records → lue /records.json LittleFS:stä
    server.on("/records", HTTP_GET, [](AsyncWebServerRequest* req) {
```

- [ ] **Step 5: Lisää globaalit CORS-headerit `setup()`:iin**

Etsi `setup()`-funktiosta:

```cpp
    // WebSocket + web server
    ws.onEvent(on_ws_event);
    server.addHandler(&ws);
    setup_routes();
    server.begin();
```

Korvaa:

```cpp
    // CORS — dashboard pyörii nyt modeemilla (eri origin), kalibrointikutsut
    // (/tare, /tare_water, /tare_rum, /records) tulevat siis cross-origin.
    DefaultHeaders::Instance().addHeader("Access-Control-Allow-Origin", "*");
    DefaultHeaders::Instance().addHeader("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
    DefaultHeaders::Instance().addHeader("Access-Control-Allow-Headers", "Content-Type");

    // Web server (ei enää WebSocketia — /data riittää modeemin pollaukseen)
    setup_routes();
    server.begin();
```

- [ ] **Step 6: Poista WS-kutsut `loop()`:sta**

Etsi:

```cpp
    unsigned long now = millis();
    if (now - last_sensor_read >= SENSOR_INTERVAL_MS) {
        last_sensor_read = now;
        if (xSemaphoreTake(hx711_mutex, pdMS_TO_TICKS(50))) {
            cached_json = build_json();
            xSemaphoreGive(hx711_mutex);
        }
        if (ws.count() > 0) {
            ws.textAll(cached_json);
        }
    }

    ws.cleanupClients();
}
```

Korvaa:

```cpp
    unsigned long now = millis();
    if (now - last_sensor_read >= SENSOR_INTERVAL_MS) {
        last_sensor_read = now;
        if (xSemaphoreTake(hx711_mutex, pdMS_TO_TICKS(50))) {
            cached_json = build_json();
            xSemaphoreGive(hx711_mutex);
        }
    }
}
```

- [ ] **Step 7: Nosta sensoripäivitysväli 1 sekuntiin**

Tiedostossa `susieq_dashboard/include/config.h`, etsi:

```cpp
#define SENSOR_INTERVAL_MS  2000       // 2 s between sensor polls
```

Korvaa:

```cpp
#define SENSOR_INTERVAL_MS  1000       // 1 s — modeemi pollaa /data:a 1s välein
```

- [ ] **Step 8: Käännä firmware ja varmista, ettei kääntövirheitä tule**

```bash
cd susieq_dashboard && pio run -e esp32dev-ota
```

Expected: `SUCCESS`, ei `ws`-symboliin liittyviä linkitysvirheitä (varmistaa, että kaikki `ws.`-viittaukset on poistettu).

- [ ] **Step 9: Commit**

```bash
git add susieq_dashboard/src/main.cpp susieq_dashboard/include/config.h
git commit -m "feat(cockpit): poista dashboard-sivunjako ja WebSocket, lisää CORS"
```

---

### Task 4: Käyttöönotto ja päästä-päähän-varmistus veneellä

**Files:** ei koodimuutoksia — vain deploy ja manuaalinen testaus.

**Interfaces:** ei sovellettavissa (deploy/verifiointitehtävä).

- [ ] **Step 1: Kysy/varmista työtapa (CLAUDE.md-sääntö)**

Ennen tätä tehtävää: kysy käyttäjältä onko SusieQ-Net käytössä (`scp`/`ssh` suoraan) tai etänä (GoodCloud RTTY + Playwright) — ks. projektin `CLAUDE.md`.

- [ ] **Step 2: Kopioi dashboard-palvelu GL-XE300:lle**

```bash
scp susieq_glxe300/susieq-dashboard-server.py root@192.168.8.1:/usr/bin/
scp susieq_glxe300/susieq-dashboard-init.d root@192.168.8.1:/etc/init.d/susieq-dashboard
scp susieq_glxe300/susieq-dashboard-setup.sh root@192.168.8.1:/tmp/
ssh root@192.168.8.1 "mkdir -p /usr/share/susieq-dashboard"
scp susieq_glxe300/dashboard/index.html root@192.168.8.1:/usr/share/susieq-dashboard/
```

- [ ] **Step 3: Asenna ja käynnistä palvelu**

```bash
ssh root@192.168.8.1 "sh /tmp/susieq-dashboard-setup.sh"
```

Expected: tuloste päättyy `Valmis! Dashboard: http://192.168.8.1:8081/`

- [ ] **Step 4: Varmista palvelu vastaa**

```bash
ssh root@192.168.8.1 "curl -s http://localhost:8081/data"
```

Expected: JSON, jossa `wind`/`battery`/`water`/`fuel`/`rum`/`gps`/`weather`/`uptime_s` — samat kentät kuin ennen, peräisin ESP32:n `/data`:sta.

- [ ] **Step 5: Flash-päivitä ESP32 OTA:lla**

```bash
cd susieq_dashboard && pio run -e esp32dev-ota -t upload
```

Expected: `SUCCESS`, cockpit buuttaa ja `Serial.printf("[SusieQ] ready...")`-viesti näkyy (jos sarjamonitori auki).

- [ ] **Step 6: Manuaalinen selainvarmistus**

Avaa selaimella `http://192.168.8.1:8081/` (yhdistettynä SusieQ-Net-verkkoon):
- Sivu latautuu (ei enää riippuvainen ESP32:n AsyncWebServerista raskaan sivun osalta)
- Tilaindikaattori näyttää "Live" ja pysyy siinä — avaa selaimen DevTools → Network, tarkista että `/data`-pyyntö toistuu ~1s välein
- Sulje selainvälilehti, avaa uudelleen 10 venttiminuutin kuluttua — varmista, että data on edelleen tuore (ei "Vanhentunut")
- Testaa kalibrointi: paina "Tara polttoaine" -nappia (tai vastaavaa UI-elementtiä), varmista että pyyntö menee suoraan `192.168.8.100`:een (DevTools → Network, Request URL) ja onnistuu (ei CORS-virhettä konsolissa)

- [ ] **Step 7: Varmista, että vanha cron-pohjainen Supabase-syöte ei rikkoutunut**

```bash
ssh root@192.168.8.1 "logread | grep susieq-sensors | tail -5"
```

Expected: ei uusia `ERROR: ei yhteyttä cockpittiin` -rivejä viimeisen 5 minuutin ajalta (ESP32:n `/data` toimii edelleen muuttumattomana cron-skriptille).

- [ ] **Step 8: Päivitä muisti**

Jos kaikki testit menivät läpi: kirjaa onnistuminen `project_colight_ble_findings`-tyyppiseen projektimuistiin (uusi muisti, esim. `project_dashboard_modem_hosting.md`) — mitä toimii, mitä jäi auki (esim. heap-/socket-juurisyytä ei koskaan varmistettu, vain kierretty).

---

## Plan Self-Review Notes

- **Spec coverage:** ESP32 "lukee käytännössä vain sensoreita" → Task 3 (WS+sivunjako pois). "Modeemi hoitaa dashboardin luotettavasti" → Task 1+2 (uusi palvelu + siirretty HTML). "Mahdollisimman reaaliaikainen, esim 1s" → Task 1 (`POLL_INTERVAL_S = 1.0`) + Task 2 (`POLL_INTERVAL_MS = 1000`) + Task 3 (`SENSOR_INTERVAL_MS = 1000`) — koko ketju 1s. Plotteri ei vaadi muutoksia (toimii jo modeemilla) — mainittu Architecture-osiossa, ei omaa tehtävää koska ei muutostarvetta.
- **Placeholder scan:** ei jäänyttä "TBD"/"lisää virheenkäsittely" -tason kohtaa; kaikki code-stepit sisältävät täyden koodin.
- **Type/nimikonsistenssi:** `ESP32_BASE` (Task 2) käytetty samalla nimellä kaikissa neljässä korvauksessa. `POLL_INTERVAL_S` (Python, Task 1) ja `POLL_INTERVAL_MS` (JS, Task 2) ovat tarkoituksella eri nimiä eri kielissä, molemmat arvoltaan 1s — ei ristiriitaa. `_cached_json` (Task 1) ei kosketa ESP32:n `cached_json`-muuttujaa (Task 3) — eri prosessit, nimien samankaltaisuus on tarkoituksellinen analogia, ei jaettu tila.
- **CORS-tarkkuus:** vain `/tare`, `/tare_water`, `/tare_rum`, `/records` kutsutaan dashboard-JS:stä cross-origin (vahvistettu koodista `grep`illä) — `/calibrate_rum`, `/rum_raw`, `/victron-debug` ovat manuaalisia debug-osoitteita eikä niitä kutsuta JS:stä, joten niille ei lisätä CORS-koodia turhaan (YAGNI).
