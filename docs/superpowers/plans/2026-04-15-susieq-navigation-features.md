# SusieQ Navigation & Fun Features Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Lisää SusieQ-dashboardiin 8 uutta ominaisuutta: painetrendi, auringonlasku-laskin, yöteema, navigointi-välilehti, ankkurialarmi, nopeusennätykset, matkatilastot ja rum o'clock.

**Architecture:** Ominaisuudet jakautuvat kahteen kerrokseen: (1) pelkkä JS/CSS-muutos `index.html`:ään — painetrendi, auringonlasku, yöteema, ankkurialarmi, rum o'clock; (2) JS + pieni firmware-lisäys — nopeusennätykset ja matkatilastot tallennetaan `/records.json`-tiedostoon ESP32:n LittleFS:ään uuden `/records`-endpointin kautta. Kaikki laskenta tapahtuu selaimessa; ESP32 toimii vain tallennuspalvelimena.

**Tech Stack:** ESP32 / ESPAsyncWebServer / LittleFS / ArduinoJson 7 / vanilla JS / SVG sparkline / Web Audio API

---

## Tiedostorakenne

| Tiedosto | Muutos |
|---|---|
| `susieq_dashboard/src/main.cpp` | +~50 riviä: uusi `GET /records` + `POST /records` |
| `susieq_dashboard/data/index.html` | +~430 riviä: CSS, HTML-rakenteet, JS-funktiot |

---

## Task 1: Firmware — /records-endpoint

**Files:**
- Modify: `susieq_dashboard/src/main.cpp` (funktio `setup_routes()`, rivi 134)

- [ ] **Step 1: Lisää /records GET + POST `setup_routes()`-funktioon**

Etsi `server.serveStatic("/", LittleFS, "/");` (rivi ~179) ja lisää tämä **ennen** sitä:

```cpp
    // GET /records → lue /records.json LittleFS:stä
    server.on("/records", HTTP_GET, [](AsyncWebServerRequest* req) {
        if (!LittleFS.exists("/records.json")) {
            req->send(200, "application/json", "{}");
            return;
        }
        File f = LittleFS.open("/records.json", "r");
        String body = f.readString();
        f.close();
        req->send(200, "application/json", body);
    });

    // POST /records → kirjoita /records.json LittleFS:ään
    // index==0: avaa kirjoitukseen; index>0: lisää perään (multi-chunk body)
    server.on("/records", HTTP_POST,
        [](AsyncWebServerRequest* req) {
            req->send(200, "text/plain", "OK");
        },
        nullptr,
        [](AsyncWebServerRequest* req, uint8_t* data, size_t len,
           size_t index, size_t total) {
            File f = LittleFS.open("/records.json", index == 0 ? "w" : "a");
            if (f) { f.write(data, len); f.close(); }
        }
    );
```

- [ ] **Step 2: Buildaa ja uploadaa firmware**

```bash
cd susieq_dashboard
pio run -t upload
```

Odotettu: `[SUCCESS]`

- [ ] **Step 3: Testaa endpointit curlilla**

Yhdistä `SusieQ-Data`-verkkoon (192.168.4.1), sitten:

```bash
# GET tyhjä → {}
curl http://192.168.4.1/records
# Odotettu: {}

# POST testidataa
curl -X POST http://192.168.4.1/records \
  -H "Content-Type: application/json" \
  -d '{"best_knots":6.1,"best_date":"15.4.2026","season_nm":0,"trips":[]}'
# Odotettu: OK

# GET takaisin → tallennettu data
curl http://192.168.4.1/records
# Odotettu: {"best_knots":6.1,"best_date":"15.4.2026","season_nm":0,"trips":[]}
```

- [ ] **Step 4: Commit**

```bash
git add susieq_dashboard/src/main.cpp
git commit -m "feat(firmware): add /records GET+POST endpoint for LittleFS persistence"
```

---

## Task 2: Painetrendi + sparkline-graafi

**Files:**
- Modify: `susieq_dashboard/data/index.html`
  - HTML: painetrendi-rivi (rivi ~913–919) + SVG-elementti
  - CSS: `.pressure-trend` + sparkline-tyyli (rivi ~9 `:root`-lohkon jälkeen)
  - JS: `pushPressure()`, `drawPressSparkline()`, `updatePressTrend()` (lisätään ennen `connect()`)
  - JS: `updateWeather()` kutsuu `pushPressure()` (rivi ~1841)

- [ ] **Step 1: Lisää sparkline-SVG painerivin alle HTML:ssä**

Etsi:
```html
        <div class="pressure-trend" id="press-trend" style="align-self:flex-end;">→ Vakaa</div>
```

Korvaa:
```html
        <div style="display:flex;align-items:center;gap:8px;align-self:flex-end;">
          <svg id="press-sparkline" width="100" height="30"
               style="overflow:visible;flex-shrink:0;"></svg>
          <div class="pressure-trend" id="press-trend">→ Vakaa</div>
        </div>
```

- [ ] **Step 2: Lisää CSS `.pressure-trend` -tyyli**

Etsi `:root {` -lohko (rivi ~10). Lisää sen jälkeen ennen `* { box-sizing`:

```css
  .pressure-trend {
    font-size: 0.72rem;
    font-weight: 600;
    letter-spacing: 0.03em;
    color: var(--muted);
  }
```

- [ ] **Step 3: Lisää JS painetrendilogiikka**

Lisää ennen riviä `// ─── Boot ──` (rivi ~1934):

```javascript
// ─── Painetrendi ───────────────────────────────────────────────────────
var _pressHistory = [];     // { t: ms, p: hPa }
var _pressLastSample = 0;
var PRESS_SAMPLE_MS = 60000;   // yksi näyte per minuutti
var PRESS_MAX_SAMPLES = 360;   // 6 tuntia

function pushPressure(hpa) {
  if (hpa == null) return;
  var now = Date.now();
  if (now - _pressLastSample < PRESS_SAMPLE_MS) return;
  _pressLastSample = now;
  _pressHistory.push({ t: now, p: hpa });
  if (_pressHistory.length > PRESS_MAX_SAMPLES) _pressHistory.shift();
  drawPressSparkline();
  updatePressTrend();
}

function drawPressSparkline() {
  var svg = document.getElementById("press-sparkline");
  if (!svg || _pressHistory.length < 2) { if (svg) svg.innerHTML = ""; return; }
  var ps = _pressHistory.map(function(x) { return x.p; });
  var min = Math.min.apply(null, ps);
  var max = Math.max.apply(null, ps);
  var range = max - min || 1;
  var W = 100, H = 28;
  var pts = ps.map(function(p, i) {
    var x = (i / (ps.length - 1)) * W;
    var y = H - ((p - min) / range) * (H - 2) - 1;
    return x.toFixed(1) + "," + y.toFixed(1);
  }).join(" ");
  svg.innerHTML = '<polyline fill="none" stroke="var(--blue)" stroke-width="1.5"'
    + ' stroke-linejoin="round" points="' + pts + '"/>';
}

function updatePressTrend() {
  if (_pressHistory.length < 6) return;  // tarvitaan vähintään 6 min dataa
  var recent = _pressHistory.slice(-Math.min(31, _pressHistory.length));
  var old    = _pressHistory.slice(0, Math.min(30, _pressHistory.length));
  function avg(arr) { return arr.reduce(function(s,x) { return s+x.p; }, 0) / arr.length; }
  var diff = avg(recent) - avg(old);
  var el = document.getElementById("press-trend");
  if (diff > 2) {
    el.textContent = "↑ Nouseva";
    el.style.color = "var(--green)";
  } else if (diff < -2) {
    el.textContent = "↓ Laskeva";
    el.style.color = "var(--red)";
  } else {
    el.textContent = "→ Vakaa";
    el.style.color = "var(--muted)";
  }
}
```

- [ ] **Step 4: Kutsu `pushPressure()` `updateWeather()`-funktiosta**

Etsi `function updateWeather(w) {` (rivi ~1841). Lisää sen **ensimmäiseksi riviksi** funktion body:n sisään:

```javascript
  if (w.pressure != null) pushPressure(w.pressure);
```

- [ ] **Step 5: Uploadaa LittleFS ja testaa**

```bash
pio run -t uploadfs
```

Avaa `http://192.168.4.1` selaimessa → Sää-välilehti → Ilmanpaine. Minuutin kuluttua sparkline alkaa piirtyä.

Vaihtoehto: avaa `susieq_preview.html` lokaalisesti. Koska previessä on mock-data, muokkaa tilapäisesti `pushPressure()` testaukseen lisäämällä testikutsu selaimen konsolista:
```javascript
// Konsoli: simuloi 10 minuutin data
for(var i=0;i<10;i++) { _pressLastSample=0; pushPressure(1013 - i*0.5); }
```

- [ ] **Step 6: Commit**

```bash
git add susieq_dashboard/data/index.html
git commit -m "feat(dashboard): pressure trend sparkline and 6h history"
```

---

## Task 3: Auringonlasku-laskin + yöteema

**Files:**
- Modify: `susieq_dashboard/data/index.html`
  - CSS: `.night-mode` overridet + `.night-btn` -tyyli
  - HTML header: yöteema-nappi (rivi ~666)
  - HTML gps-time-bar: `#solar-info` -elementti (rivi ~681)
  - JS: `calcSunriseSunset()`, `updateSolarInfo()`, `toggleNightMode()`, `applyNightMode()`, päivitetty `updateGpsTime()` (lisätään ennen `// ─── Boot ──`)

- [ ] **Step 1: Lisää `.night-mode` CSS-overridet**

Etsi `* { box-sizing: border-box; margin: 0; padding: 0; }` (rivi ~23). Lisää **ennen** sitä:

```css
  /* ── Yöteema — punainen palette silmille ─────────────────────────── */
  body.night-mode {
    --bg:    #000000;
    --card:  #0a0000;
    --border:#330000;
    --text:  #ff4444;
    --muted: #882200;
    --blue:  #cc3300;
  }

  .night-btn {
    background: none;
    border: 1px solid var(--border);
    color: var(--muted);
    width: 28px; height: 28px;
    border-radius: 50%;
    font-size: 0.9rem;
    cursor: pointer;
    display: flex; align-items: center; justify-content: center;
    flex-shrink: 0;
    transition: color 0.3s;
  }

  #solar-info {
    font-size: 0.72rem;
    color: var(--orange);
    font-weight: 600;
    letter-spacing: 0.03em;
    margin-left: auto;
  }
```

- [ ] **Step 2: Lisää yöteema-nappi headeriin**

Etsi:
```html
    <button id="help-btn" onclick="toggleHelp()" title="Käyttöopas"
```

Lisää **ennen** tätä riviä:

```html
    <button class="night-btn" id="night-btn" onclick="toggleNightMode()" title="Yöteema">☀️</button>
```

- [ ] **Step 3: Lisää `#solar-info` gps-time-bariin**

Etsi:
```html
  <span id="gps-time-utc">UTC --:--:--</span>
</div>
```

Korvaa:
```html
  <span id="gps-time-utc">UTC --:--:--</span>
  <span id="solar-info" style="display:none"></span>
</div>
```

- [ ] **Step 4: Lisää auringonlasku-JS ennen `// ─── Boot ──`**

```javascript
// ─── Auringonlasku-laskin (NOAA simplified) ────────────────────────────
// Palauttaa { sunriseUtcMin, sunsetUtcMin } tai null (yöttömyyöö / napayö)
function calcSunriseSunset(lat, lon) {
  var now = new Date();
  // Julian Day Number (midday UTC tänään)
  var JD = Math.floor(Date.UTC(now.getUTCFullYear(), now.getUTCMonth(),
                               now.getUTCDate()) / 86400000) + 2440587.5 + 0.5;
  var n  = JD - 2451545.0;
  var L  = ((280.460 + 0.9856474 * n) % 360 + 360) % 360;
  var g  = ((357.528 + 0.9856003 * n) % 360 + 360) % 360;
  var gR = g * Math.PI / 180;
  var lam = L + 1.915 * Math.sin(gR) + 0.020 * Math.sin(2 * gR);
  var lamR = lam * Math.PI / 180;
  var sinDec = Math.sin(23.439 * Math.PI / 180) * Math.sin(lamR);
  var dec = Math.asin(sinDec);
  var latR = lat * Math.PI / 180;
  // -0.01454 rad = -0.833° (aurinko 50' horisontin alla refraktiosta)
  var cosH = (Math.sin(-0.01454) - Math.sin(latR) * sinDec)
             / (Math.cos(latR) * Math.cos(dec));
  if (cosH < -1) return { sunriseUtcMin: 0,    sunsetUtcMin: 1440 }; // yöttömyyöö
  if (cosH >  1) return null;                                          // napayö
  var H = Math.acos(cosH) * 180 / Math.PI;
  // Equation of time (Fourier approx, minuuteissa)
  var B   = 2 * Math.PI * (n - 1) / 365;
  var EoT = (0.000075 + 0.001868*Math.cos(B) - 0.032077*Math.sin(B)
             - 0.014615*Math.cos(2*B) - 0.04089*Math.sin(2*B)) * 229.18 / 60;
  var noonUtc = 12 - lon / 15 - EoT;
  return {
    sunriseUtcMin: (noonUtc - H / 15) * 60,
    sunsetUtcMin:  (noonUtc + H / 15) * 60
  };
}

var _solar = null;         // { sunriseUtcMin, sunsetUtcMin } tai null
var _solarLat = null;
var _solarLon = null;
var _nightModeManual = null;   // null=auto, true=yö, false=päivä

function updateSolarInfo(lat, lon) {
  if (lat == null || lon == null) return;
  // Laske vain jos koordinaatit muuttuneet merkittävästi (>500m) tai ei laskettu
  if (_solarLat != null && Math.abs(lat - _solarLat) < 0.005 && Math.abs(lon - _solarLon) < 0.005) return;
  _solarLat = lat; _solarLon = lon;
  _solar = calcSunriseSunset(lat, lon);
  updateNightModeAuto();
}

function updateNightModeAuto() {
  if (_nightModeManual !== null) return;  // manuaali ohittaa
  if (!_solar) return;
  var now = new Date();
  var utcMin = now.getUTCHours() * 60 + now.getUTCMinutes();
  var isNight = utcMin < _solar.sunriseUtcMin || utcMin > _solar.sunsetUtcMin;
  applyNightMode(isNight);
}

function applyNightMode(on) {
  document.body.classList.toggle("night-mode", on);
  document.getElementById("night-btn").textContent = on ? "🌙" : "☀️";
  localStorage.setItem("nightMode", on ? "1" : "0");
}

function toggleNightMode() {
  var isNight = document.body.classList.contains("night-mode");
  _nightModeManual = !isNight;
  applyNightMode(_nightModeManual);
  // Jos käyttäjä osuu automaatin tilaan, nollataan manuaali 5 min päästä
  setTimeout(function() { _nightModeManual = null; updateNightModeAuto(); }, 300000);
}

function solarCountdownText(utcMin) {
  var now = new Date();
  var nowMin = now.getUTCHours() * 60 + now.getUTCMinutes();
  var diffMin = ((utcMin - nowMin) + 1440) % 1440;
  if (diffMin > 12 * 60) return null;  // yli 12h päästä — ei näytetä
  var h = Math.floor(diffMin / 60), m = diffMin % 60;
  var pad = function(n) { return String(n).padStart(2, '0'); };
  // UTC-minuutit → paikallinen kellonaika
  var localD = new Date(Date.UTC(now.getUTCFullYear(), now.getUTCMonth(),
                                 now.getUTCDate(), 0, utcMin));
  var timeStr = pad(localD.getHours()) + ':' + pad(localD.getMinutes());
  return { timeStr: timeStr, diffMin: diffMin, h: h, m: m };
}

function refreshSolarBar() {
  var el = document.getElementById("solar-info");
  if (!_solar) { el.style.display = "none"; return; }
  var su = solarCountdownText(_solar.sunriseUtcMin);
  var ss = solarCountdownText(_solar.sunsetUtcMin);
  var now = new Date();
  var nowMin = now.getUTCHours() * 60 + now.getUTCMinutes();
  var isDay = nowMin >= _solar.sunriseUtcMin && nowMin <= _solar.sunsetUtcMin;
  var text = "";
  if (isDay && ss) {
    text = "🌅 " + ss.timeStr + " (" + ss.h + "h " + ss.m + "m)";
  } else if (!isDay && su) {
    text = "🌄 " + su.timeStr + " (" + su.h + "h " + su.m + "m)";
  }
  if (text) { el.textContent = text; el.style.display = ""; }
  else el.style.display = "none";
}

// Alusta yöteema localStoragesta (ennen ensimmäistä päivitystä)
(function() {
  var saved = localStorage.getItem("nightMode");
  if (saved === "1") { applyNightMode(true); _nightModeManual = true; }
})();

setInterval(refreshSolarBar, 30000);
```

- [ ] **Step 5: Kutsu `updateSolarInfo()` ja `refreshSolarBar()` GPS-päivityksessä**

Etsi `update()`-funktiossa (rivi ~1690):
```javascript
  // GPS → coordinates (DDM format, below boat speed)
  var coordsOk = d.gps && d.gps.valid && d.gps.fix && d.gps.lat != null && d.gps.lon != null;
```

Lisää **ennen** tätä:
```javascript
  // Solar: laske auringonlasku koordinaateista
  if (coordsOk || (d.gps && d.gps.lat != null)) {
    updateSolarInfo(d.gps.lat, d.gps.lon);
  }
```

Ja lisää `updateNightModeAuto();` + `refreshSolarBar();` kutsujen jälkeen GPS-blokin loppuun (ennen Battery-kommenttia).

- [ ] **Step 6: Testaa selaimessa**

Avaa `susieq_preview.html`. Lisää konsolissa:
```javascript
updateSolarInfo(61.5, 23.8);  // Tampere
refreshSolarBar();
```
Odotus: `#solar-info` näyttää auringonlasku- tai nousuajan.

Testaa yöteema-nappi: klikkaa ☀️ → 🌙, dashboard muuttuu punaiseksi. Klikkaa uudelleen → takaisin normaaliin.

- [ ] **Step 7: Commit**

```bash
git add susieq_dashboard/data/index.html
git commit -m "feat(dashboard): sunset calculator and auto night mode"
```

---

## Task 4: Navigointi-välilehti (GPS-kortin HTML-rakenne)

**Files:**
- Modify: `susieq_dashboard/data/index.html`
  - HTML: uusi tab-nappi + `#tab-nav` panel
  - CSS: navigointi-kortin tyylit
  - JS: `updateNavTab()` kutsutaan `update()`-funktiosta

Tämä task luo HTML-scaffoldin jolle Tasks 5–7 rakentavat.

- [ ] **Step 1: Lisää CSS navigointi-kortille**

Etsi `.pressure-trend {` (lisätty Task 3:ssa). Lisää sen jälkeen:

```css
  /* ── Navigointi-kortti ────────────────────────────────────────────── */
  .nav-big { font-size: 2rem; font-weight: 700; letter-spacing: -0.02em;
             color: var(--text); font-variant-numeric: tabular-nums; }
  .nav-big-unit { font-size: 0.85rem; color: var(--muted); margin-left: 2px; }
  .nav-row { display: flex; justify-content: space-between; align-items: baseline;
             padding: 4px 0; border-bottom: 1px solid var(--border); font-size: 0.85rem; }
  .nav-row:last-child { border-bottom: none; }
  .nav-label { color: var(--muted); }
  .nav-section { margin-top: 12px; padding-top: 12px; border-top: 2px solid var(--border); }
  .nav-btn {
    background: var(--card); border: 1px solid var(--border); color: var(--text);
    padding: 6px 14px; border-radius: 6px; font-size: 0.8rem; font-family: inherit;
    cursor: pointer; transition: background 0.2s;
  }
  .nav-btn:hover { background: var(--border); }
  .nav-btn-danger { border-color: var(--red); color: var(--red); }
  .nav-btn-danger:hover { background: rgba(248,81,73,0.12); }
  .nav-btn-row { display: flex; gap: 8px; flex-wrap: wrap; margin-bottom: 8px; }
  .nav-alarm-active { animation: nav-blink 1s ease-in-out infinite; }
  @keyframes nav-blink { 0%,100% { background: var(--card); } 50% { background: rgba(248,81,73,0.2); } }
```

- [ ] **Step 2: Lisää "Navigointi" tab-nappi**

Etsi:
```html
  <button class="tab" data-tab="weather">Sää</button>
```

Lisää sen jälkeen:
```html
  <button class="tab" data-tab="nav">Nav</button>
```

- [ ] **Step 3: Lisää `#tab-nav` panel**

Etsi `</div><!-- #tab-weather -->` (rivi ~934). Lisää sen jälkeen:

```html
<div id="tab-nav" class="tab-panel">
  <div class="card" id="card-nav">
    <h2>Navigointi</h2>

    <!-- SOG + COG -->
    <div style="display:flex;gap:24px;padding:8px 0 12px;border-bottom:1px solid var(--border);">
      <div>
        <div class="nav-big"><span id="nav-sog">—</span><span class="nav-big-unit">kn</span></div>
        <div style="font-size:0.72rem;color:var(--muted);margin-top:2px;">vauhti</div>
      </div>
      <div>
        <div class="nav-big"><span id="nav-cog">—</span><span class="nav-big-unit">°</span></div>
        <div style="font-size:0.72rem;color:var(--muted);margin-top:2px;">suunta</div>
      </div>
    </div>

    <!-- Nopeusennätykset -->
    <div class="nav-section" id="records-section">
      <div style="font-size:0.72rem;color:var(--muted);font-weight:600;
                  letter-spacing:0.06em;text-transform:uppercase;margin-bottom:6px;">Nopeus</div>
      <div class="nav-row">
        <span class="nav-label">Sessio max</span>
        <span id="speed-session-max">— kn</span>
      </div>
      <div class="nav-row">
        <span class="nav-label">Kauden ennätys</span>
        <span id="speed-season-best">— kn</span>
      </div>
      <div class="nav-row" style="border:none;padding-top:0;">
        <span class="nav-label" style="font-size:0.72rem;" id="speed-season-date"></span>
        <span id="season-nm" style="font-size:0.8rem;color:var(--muted);">— nm kaudella</span>
      </div>
    </div>

    <!-- Ankkurialarmi -->
    <div class="nav-section" id="anchor-section">
      <div style="font-size:0.72rem;color:var(--muted);font-weight:600;
                  letter-spacing:0.06em;text-transform:uppercase;margin-bottom:6px;">Ankkuri</div>
      <div class="nav-btn-row">
        <button id="anchor-set-btn" class="nav-btn" onclick="setAnchor()">⚓ Laske ankkuri</button>
        <button id="anchor-clear-btn" class="nav-btn nav-btn-danger" onclick="clearAnchor()"
                style="display:none">✕ Nosta ankkuri</button>
      </div>
      <div id="anchor-live" style="display:none">
        <div class="nav-row">
          <span class="nav-label">Etäisyys ankkurista</span>
          <span id="anchor-dist">— m</span>
        </div>
        <div class="nav-row" style="gap:8px;flex-wrap:wrap;">
          <span class="nav-label">Hälytysraja</span>
          <input type="range" id="anchor-radius" min="20" max="150" value="50"
                 oninput="updateAnchorRadius(this.value)"
                 style="width:80px;accent-color:var(--blue);">
          <span id="anchor-radius-val" style="min-width:36px;text-align:right;">50 m</span>
        </div>
      </div>
    </div>

    <!-- Matkatilastot (GPS-kortin laajennus) -->
    <div class="nav-section" id="trip-section">
      <div style="font-size:0.72rem;color:var(--muted);font-weight:600;
                  letter-spacing:0.06em;text-transform:uppercase;margin-bottom:6px;">Matka</div>
      <div class="nav-btn-row">
        <button id="trip-start-btn" class="nav-btn" onclick="startTrip()">▶ Aloita matka</button>
        <button id="trip-stop-btn" class="nav-btn nav-btn-danger" onclick="stopTrip()"
                style="display:none">⏹ Lopeta matka</button>
      </div>
      <div id="trip-live" style="display:none">
        <div class="nav-row"><span class="nav-label">Kesto</span><span id="trip-duration">0h 0m</span></div>
        <div class="nav-row"><span class="nav-label">Matka</span><span id="trip-nm">0.0 nm</span></div>
        <div class="nav-row"><span class="nav-label">Keskinopeus</span><span id="trip-avg-kn">— kn</span></div>
        <div class="nav-row"><span class="nav-label">Huippu</span><span id="trip-max-kn">— kn</span></div>
      </div>
      <div id="trip-history" style="margin-top:8px;font-size:0.8rem;color:var(--muted);"></div>
    </div>

  </div>
</div><!-- #tab-nav -->
```

- [ ] **Step 4: Lisää `updateNavTab()` JS-funktio ja kutsu `update()`-funktiosta**

Lisää ennen `// ─── Boot ──`:

```javascript
// ─── Navigointi-välilehti ──────────────────────────────────────────────
function updateNavTab(gps) {
  var fix = gps && gps.fix;
  document.getElementById("nav-sog").textContent = fix && gps.sog_knots != null
    ? gps.sog_knots.toFixed(1) : "—";
  document.getElementById("nav-cog").textContent = fix && gps.cog_deg != null
    ? Math.round(gps.cog_deg) : "—";
}
```

Lisää `update()`-funktiossa GPS-blokin loppuun (rivi ~1694, ennen `// Battery`):
```javascript
  // Navigointi-välilehti
  updateNavTab(d.gps);
```

- [ ] **Step 5: Testaa selaimessa**

Avaa `susieq_preview.html`. Klikkaa "Nav"-välilehti → kortti näkyy oikein. SOG ja COG päivittyvät kun mock-data tulee WebSocketista (tai lisää `updateNavTab({fix:true, sog_knots:5.2, cog_deg:215})` konsolista).

- [ ] **Step 6: Commit**

```bash
git add susieq_dashboard/data/index.html
git commit -m "feat(dashboard): Navigointi tab scaffold with GPS card"
```

---

## Task 5: Ankkurialarmi

**Files:**
- Modify: `susieq_dashboard/data/index.html`
  - JS: `_anchor*`-muuttujat, `setAnchor()`, `clearAnchor()`, `updateAnchorRadius()`, `checkAnchorAlarm()`, `anchorBeep()`
  - JS: kutsu `checkAnchorAlarm()` GPS-päivityksessä

- [ ] **Step 1: Lisää ankkurialarmi-JS ennen `// ─── Boot ──`**

```javascript
// ─── Ankkurialarmi ─────────────────────────────────────────────────────
// _anchorLat/_anchorLon: viimeisin GPS-sijainti (päivittyy joka tick)
// _anchorSetLat/_anchorSetLon: ankkuripisteen koordinaatit (kiinteä kun alarmi asetettu)
var _anchorLat = null, _anchorLon = null;
var _anchorSetLat = null, _anchorSetLon = null;
var _anchorRadiusM = 50;
var _anchorAlarmFiring = false;
var _anchorBeepCtx = null;
var _anchorBeepInterval = null;

function haversineMeters(lat1, lon1, lat2, lon2) {
  var R = 6371000;
  var dLat = (lat2 - lat1) * Math.PI / 180;
  var dLon = (lon2 - lon1) * Math.PI / 180;
  var a = Math.sin(dLat/2) * Math.sin(dLat/2)
        + Math.cos(lat1*Math.PI/180) * Math.cos(lat2*Math.PI/180)
          * Math.sin(dLon/2) * Math.sin(dLon/2);
  return R * 2 * Math.atan2(Math.sqrt(a), Math.sqrt(1 - a));
}

function setAnchor() {
  if (_anchorLat == null) { alert("GPS-signaali puuttuu — odota fixiä."); return; }
  _anchorSetLat = _anchorLat;   // kiinnitä ankkuripiste tähän hetkeen
  _anchorSetLon = _anchorLon;
  document.getElementById("anchor-set-btn").style.display   = "none";
  document.getElementById("anchor-clear-btn").style.display = "";
  document.getElementById("anchor-live").style.display      = "";
  document.getElementById("anchor-radius-val").textContent  = _anchorRadiusM + " m";
}

function clearAnchor() {
  _anchorSetLat = null; _anchorSetLon = null;
  _anchorAlarmFiring = false;
  if (_anchorBeepInterval) { clearInterval(_anchorBeepInterval); _anchorBeepInterval = null; }
  document.getElementById("anchor-set-btn").style.display   = "";
  document.getElementById("anchor-clear-btn").style.display = "none";
  document.getElementById("anchor-live").style.display      = "none";
  document.getElementById("card-nav").classList.remove("nav-alarm-active");
}

function updateAnchorRadius(val) {
  _anchorRadiusM = parseInt(val);
  document.getElementById("anchor-radius-val").textContent = _anchorRadiusM + " m";
}

function anchorBeep() {
  try {
    if (!_anchorBeepCtx) _anchorBeepCtx = new (window.AudioContext || window.webkitAudioContext)();
    var osc = _anchorBeepCtx.createOscillator();
    var gain = _anchorBeepCtx.createGain();
    osc.connect(gain); gain.connect(_anchorBeepCtx.destination);
    osc.frequency.value = 880;
    gain.gain.setValueAtTime(0.3, _anchorBeepCtx.currentTime);
    gain.gain.exponentialRampToValueAtTime(0.001, _anchorBeepCtx.currentTime + 0.6);
    osc.start(); osc.stop(_anchorBeepCtx.currentTime + 0.6);
  } catch(e) {}
}

function checkAnchorAlarm(lat, lon) {
  _anchorLat = lat; _anchorLon = lon;   // päivitä viimeisin sijainti aina
  if (_anchorSetLat == null) return;    // alarmi ei asetettu
  if (lat == null || lon == null) return;
  var dist = haversineMeters(_anchorSetLat, _anchorSetLon, lat, lon);
  document.getElementById("anchor-dist").textContent = Math.round(dist) + " m";
  var alarm = dist > _anchorRadiusM;
  document.getElementById("card-nav").classList.toggle("nav-alarm-active", alarm);
  if (alarm && !_anchorAlarmFiring) {
    _anchorAlarmFiring = true;
    anchorBeep();
    _anchorBeepInterval = setInterval(anchorBeep, 3000);
  }
  if (!alarm && _anchorAlarmFiring) {
    _anchorAlarmFiring = false;
    if (_anchorBeepInterval) { clearInterval(_anchorBeepInterval); _anchorBeepInterval = null; }
  }
}
```

- [ ] **Step 2: Kutsu `checkAnchorAlarm()` GPS-päivityksestä**

Etsi `updateNavTab(d.gps);` (lisätty Task 4). Lisää heti sen jälkeen:
```javascript
  // Ankkurialarmi
  if (d.gps && d.gps.lat != null && d.gps.lon != null) {
    checkAnchorAlarm(d.gps.lat, d.gps.lon);
  }
```

- [ ] **Step 4: Testaa**

Avaa `susieq_preview.html`. Kutsu konsolissa:
```javascript
// Simuloi: paina "Laske ankkuri" -nappi Nav-välilehdellä
// Aseta koordinaatit ensin
_anchorLat = 61.500; _anchorLon = 23.800;
// Klikkaa "⚓ Laske ankkuri" UI:ssa
// Sitten simuloi ajautuminen 80m päähän:
checkAnchorAlarm(61.4993, 23.800);
// Odotus: kortti alkaa vilkkua punaisena + piippaus
```

- [ ] **Step 5: Commit**

```bash
git add susieq_dashboard/data/index.html
git commit -m "feat(dashboard): anchor alarm with Haversine distance and audio alert"
```

---

## Task 6: Nopeusennätykset

**Files:**
- Modify: `susieq_dashboard/data/index.html`
  - JS: `_records`, `_sessionMaxKn`, `loadRecords()`, `checkSpeedRecord()`, `postRecords()`, `updateRecordsUI()`
  - JS: kutsu `loadRecords()` bootin yhteydessä; `checkSpeedRecord()` GPS-päivityksessä

- [ ] **Step 1: Lisää nopeusennätys-JS ennen `// ─── Boot ──`**

```javascript
// ─── Nopeusennätykset & matkatilastot (tallennus ESP32 /records) ────────
var _records = { best_knots: 0, best_date: "", season_nm: 0, trips: [] };
var _sessionMaxKn = 0;
var _lastRecordPost = 0;

function postRecords() {
  fetchWithTimeout("/records", {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify(_records)
  }, 5000).catch(function() {});
}

function loadRecords() {
  fetchWithTimeout("/records", {}, 5000)
    .then(function(r) { return r.json(); })
    .then(function(j) {
      if (j && (j.best_knots || j.trips)) {
        _records.best_knots = j.best_knots || 0;
        _records.best_date  = j.best_date  || "";
        _records.season_nm  = j.season_nm  || 0;
        _records.trips      = j.trips      || [];
        updateRecordsUI();
        // updateTripHistoryUI määritellään Task 7:ssa — guard estää virheet jos toteutettu erikseen
        if (typeof updateTripHistoryUI === "function") updateTripHistoryUI();
      }
    })
    .catch(function() {});  // offline — ei haittaa
}

function updateRecordsUI() {
  document.getElementById("speed-season-best").textContent =
    _records.best_knots > 0 ? _records.best_knots.toFixed(1) + " kn" : "— kn";
  document.getElementById("speed-season-date").textContent =
    _records.best_date ? _records.best_date : "";
  document.getElementById("season-nm").textContent =
    _records.season_nm > 0 ? _records.season_nm.toFixed(1) + " nm kaudella" : "— nm kaudella";
}

function checkSpeedRecord(kn) {
  if (kn == null || kn <= _sessionMaxKn) return;
  _sessionMaxKn = kn;
  document.getElementById("speed-session-max").textContent = kn.toFixed(1) + " kn";
  if (kn > _records.best_knots) {
    _records.best_knots = Math.round(kn * 10) / 10;
    _records.best_date  = new Date().toLocaleDateString("fi-FI");
    updateRecordsUI();
    var now = Date.now();
    if (now - _lastRecordPost > 60000) {   // throttle: max 1 POST/min
      _lastRecordPost = now;
      postRecords();
    }
  }
}
```

- [ ] **Step 2: Kutsu `checkSpeedRecord()` GPS-päivityksestä**

Etsi `checkAnchorAlarm(d.gps.lat, d.gps.lon);` (lisätty Task 5). Lisää sen jälkeen:
```javascript
  // Nopeusennätys
  if (d.gps && d.gps.fix && d.gps.sog_knots != null) {
    checkSpeedRecord(d.gps.sog_knots);
  }
```

- [ ] **Step 3: Kutsu `loadRecords()` bootin yhteydessä**

Etsi `// ─── Boot ──` -rivi. Lisää `connect();` jälkeen:
```javascript
loadRecords();
```

- [ ] **Step 4: Testaa**

```bash
# Varmista firmware on uploadattu (Task 1)
# Lisää testiennätys:
curl -X POST http://192.168.4.1/records \
  -H "Content-Type: application/json" \
  -d '{"best_knots":7.8,"best_date":"14.7.2025","season_nm":187.3,"trips":[]}'
```

Lataa dashboard uudelleen → "Kauden ennätys: 7.8 kn" näkyy Nav-välilehdellä.

Avaa `susieq_preview.html` → kutsu konsolissa:
```javascript
checkSpeedRecord(8.2);
// Odotus: "Sessio max: 8.2 kn" päivittyy
// Jos >7.8 → POST /records lähetetään (näkyy network tabissa tai curl GET:ssä)
```

- [ ] **Step 5: Commit**

```bash
git add susieq_dashboard/data/index.html
git commit -m "feat(dashboard): speed records with ESP32 persistence"
```

---

## Task 7: Matkatilastot

**Files:**
- Modify: `susieq_dashboard/data/index.html`
  - JS: `_trip`, `startTrip()`, `stopTrip()`, `updateTripDistance()`, `updateTripHistoryUI()`
  - JS: kutsu `updateTripDistance()` GPS-päivityksessä

- [ ] **Step 1: Lisää matkatilastot-JS ennen `// ─── Boot ──`**

```javascript
// ─── Matkatilastot ──────────────────────────────────────────────────────
var _trip = null;   // { startTime_ms, nm, maxKn, lastTime_ms }

function startTrip() {
  var now = Date.now();
  _trip = { startTime_ms: now, nm: 0, maxKn: 0, lastTime_ms: now };
  document.getElementById("trip-start-btn").style.display = "none";
  document.getElementById("trip-stop-btn").style.display  = "";
  document.getElementById("trip-live").style.display = "";
  updateTripLiveUI();
}

function stopTrip() {
  if (!_trip) return;
  var now = Date.now();
  var durationMin = Math.round((now - _trip.startTime_ms) / 60000);
  var avgKn = durationMin > 0 ? _trip.nm / (durationMin / 60) : 0;
  var summary = {
    date:         new Date().toLocaleDateString("fi-FI"),
    duration_min: durationMin,
    nm:           Math.round(_trip.nm * 10) / 10,
    avg_kn:       Math.round(avgKn * 10) / 10,
    max_kn:       Math.round(_trip.maxKn * 10) / 10
  };
  // Lisää matkan nm kausiyhteensä
  _records.season_nm = Math.round((_records.season_nm + summary.nm) * 10) / 10;
  // Prepend ja rajoita 20:een
  _records.trips = [summary].concat((_records.trips || []).slice(0, 19));
  postRecords();
  updateRecordsUI();
  _trip = null;
  document.getElementById("trip-start-btn").style.display = "";
  document.getElementById("trip-stop-btn").style.display  = "none";
  document.getElementById("trip-live").style.display = "none";
  updateTripHistoryUI();
}

function updateTripDistance(sogKn) {
  if (!_trip || sogKn == null) return;
  var now = Date.now();
  var dtHours = (now - _trip.lastTime_ms) / 3600000;
  if (dtHours > 0.1) dtHours = 0.1;  // katkaise jos >6min tauko (esim. yhteys poikki)
  _trip.nm += sogKn * dtHours;
  if (sogKn > _trip.maxKn) _trip.maxKn = sogKn;
  _trip.lastTime_ms = now;
  updateTripLiveUI();
}

function updateTripLiveUI() {
  if (!_trip) return;
  var now  = Date.now();
  var elapsed = Math.floor((now - _trip.startTime_ms) / 60000);
  var h = Math.floor(elapsed / 60), m = elapsed % 60;
  document.getElementById("trip-duration").textContent = h + "h " + m + "m";
  document.getElementById("trip-nm").textContent       = _trip.nm.toFixed(1) + " nm";
  var avgKn = elapsed > 0 ? _trip.nm / (elapsed / 60) : 0;
  document.getElementById("trip-avg-kn").textContent   = avgKn.toFixed(1) + " kn";
  document.getElementById("trip-max-kn").textContent   = _trip.maxKn.toFixed(1) + " kn";
}

function updateTripHistoryUI() {
  var el = document.getElementById("trip-history");
  if (!_records.trips || _records.trips.length === 0) { el.innerHTML = ""; return; }
  var html = '<div style="font-size:0.72rem;color:var(--muted);font-weight:600;'
    + 'letter-spacing:0.06em;text-transform:uppercase;margin-bottom:4px;">Viimeisimmät matkat</div>';
  _records.trips.slice(0, 3).forEach(function(t) {
    html += '<div style="display:flex;justify-content:space-between;padding:3px 0;'
      + 'border-top:1px solid var(--border);font-size:0.8rem;">'
      + '<span style="color:var(--muted);">' + t.date + '</span>'
      + '<span>' + t.nm + ' nm</span>'
      + '<span>' + Math.floor(t.duration_min/60) + 'h ' + (t.duration_min%60) + 'm</span>'
      + '<span style="color:var(--muted);">↑' + t.max_kn + ' kn</span>'
      + '</div>';
  });
  el.innerHTML = html;
}

// Päivitä live-UI kerran minuutissa vaikka GPS ei päivity
setInterval(function() { if (_trip) updateTripLiveUI(); }, 30000);
```

- [ ] **Step 2: Kutsu `updateTripDistance()` GPS-päivityksestä**

Etsi `checkSpeedRecord(d.gps.sog_knots);` (Task 6). Lisää sen jälkeen:
```javascript
  // Matkaetäisyys
  if (d.gps && d.gps.fix && d.gps.sog_knots != null) {
    updateTripDistance(d.gps.sog_knots);
  }
```

- [ ] **Step 3: Testaa**

Avaa `susieq_preview.html`. Navigointi-välilehti → "▶ Aloita matka":
- Live-tilastot näkyvät
- Kutsu konsolissa `updateTripDistance(6.2)` → matka kasvaa
- Klikkaa "⏹ Lopeta matka" → matka tallentuu historiaan
- Lataa sivu uudelleen → `loadRecords()` hakee historian ESP32:lta (tai mockissa tyhjä)

- [ ] **Step 4: Commit**

```bash
git add susieq_dashboard/data/index.html
git commit -m "feat(dashboard): trip stats with nm accumulation and 20-trip history"
```

---

## Task 8: Rum o'clock

**Files:**
- Modify: `susieq_dashboard/data/index.html`
  - HTML: rum-countdown tankki-kortissa
  - CSS: `@keyframes rum-flash`
  - JS: `updateRumOClock()`, sunset-trigger

- [ ] **Step 1: Lisää rum-countdown HTML tankki-korttiin**

Etsi `.tanks-inner`-divin sulkeva `</div>` (rivi ~878). Se on heti ennen tara-nappeja:
```html
    </div>
    <div style="display:flex;gap:12px;margin-top:4px;flex-wrap:wrap;">
      <button onclick="tareWater()"
```

Lisää rum-countdown näiden kahden `</div>` ja `<div ...tara>` väliin:
```html
    </div>
    <!-- Rum o'clock countdown -->
    <div id="rum-oclock-row" style="display:none;padding:8px 0 4px;text-align:center;font-size:0.8rem;">
      <span id="rum-oclock-text" style="color:var(--orange);font-weight:600;"></span>
    </div>
    <div style="display:flex;gap:12px;margin-top:4px;flex-wrap:wrap;">
      <button onclick="tareWater()"
```

- [ ] **Step 2: Lisää rum flash CSS**

Lisää `@keyframes nav-blink` :n jälkeen (Task 4 CSS):

```css
  @keyframes rum-flash {
    0%,100% { background: var(--card); }
    50%      { background: rgba(219,109,40,0.25); }
  }
  .rum-flash-active { animation: rum-flash 0.8s ease-in-out 6; }
```

- [ ] **Step 3: Lisää rum o'clock JS ennen `// ─── Boot ──`**

```javascript
// ─── Rum o'clock ───────────────────────────────────────────────────────
var _rumOClockFired = false;

function updateRumOClock(rumLiters) {
  var el = document.getElementById("rum-oclock-row");
  var txt = document.getElementById("rum-oclock-text");
  if (!_solar) { el.style.display = "none"; return; }

  var now = new Date();
  var nowMin = now.getUTCHours() * 60 + now.getUTCMinutes();
  var diffMin = ((_solar.sunsetUtcMin - nowMin) + 1440) % 1440;

  // Vain näytetään kun auringonlasku < 3h päässä tai hetki sitten
  var afterSunset = nowMin > _solar.sunsetUtcMin && nowMin < _solar.sunsetUtcMin + 30;
  if (diffMin > 180 && !afterSunset) { el.style.display = "none"; _rumOClockFired = false; return; }

  el.style.display = "";
  var shots = rumLiters != null ? Math.floor(rumLiters / 0.06) : null;
  var shotStr = shots != null ? " · " + shots + " annosta jäljellä" : "";

  if (afterSunset) {
    txt.textContent = "🌅 Rum o'clock!" + shotStr;
    if (!_rumOClockFired) {
      _rumOClockFired = true;
      rumOClockCelebrate();
    }
  } else {
    var h = Math.floor(diffMin / 60), m = diffMin % 60;
    txt.textContent = "🌅 Rum o'clock " + h + "h " + m + "m päästä" + shotStr;
  }
}

function rumOClockCelebrate() {
  // Visuaalinen välähdys
  var card = document.getElementById("card-tanks");
  card.classList.add("rum-flash-active");
  setTimeout(function() { card.classList.remove("rum-flash-active"); }, 5000);

  // Web Audio -fanfaari (3 nousevaa ääntä)
  try {
    var ctx = new (window.AudioContext || window.webkitAudioContext)();
    [440, 554, 659].forEach(function(freq, i) {
      var osc  = ctx.createOscillator();
      var gain = ctx.createGain();
      osc.connect(gain); gain.connect(ctx.destination);
      osc.type = "triangle";
      osc.frequency.value = freq;
      var t = ctx.currentTime + i * 0.2;
      gain.gain.setValueAtTime(0.0, t);
      gain.gain.linearRampToValueAtTime(0.25, t + 0.05);
      gain.gain.exponentialRampToValueAtTime(0.001, t + 0.5);
      osc.start(t); osc.stop(t + 0.5);
    });
  } catch(e) {}
}
```

- [ ] **Step 4: Kutsu `updateRumOClock()` `update()`-funktiosta**

Etsi `update()`-funktiossa rum-datan päivityskohta (rivi ~1773–1781). Lisää sen jälkeen:
```javascript
  // Rum o'clock countdown
  updateRumOClock(rumOk ? d.rum.liters : null);
```

- [ ] **Step 5: Testaa**

Avaa `susieq_preview.html`. Kutsu konsolissa:
```javascript
// Simuloi aurinko 5 min päässä laskeudusta
_solar = { sunriseUtcMin: 300, sunsetUtcMin: new Date().getUTCHours()*60 + new Date().getUTCMinutes() + 5 };
updateRumOClock(0.72);
// Odotus: "🌅 Rum o'clock 0h 5m päästä · 12 annosta jäljellä"

// Simuloi auringonlasku juuri tapahtunut
_solar.sunsetUtcMin = new Date().getUTCHours()*60 + new Date().getUTCMinutes() - 5;
_rumOClockFired = false;
updateRumOClock(0.72);
// Odotus: "🌅 Rum o'clock!" + välähdys + fanfaari
```

- [ ] **Step 6: Deploy ja loppuvalidointi**

```bash
pio run -t uploadfs
```

Avaa `http://192.168.4.1`:
- Sää → Ilmanpaine: sparkline piirtyy minuutin jälkeen
- Navigointi → Nav-välilehti: SOG/COG + ankkuri + speed records + matkatilastot näkyvät
- Header: ☀️/🌙 -nappi vaihtaa teeman
- `#gps-time-bar`: auringonlasku-countdown näkyy kun GPS-fix saatu
- Tankit-välilehti: rum o'clock row näkyy < 3h ennen auringonlaskua

- [ ] **Step 7: Commit**

```bash
git add susieq_dashboard/data/index.html
git commit -m "feat(dashboard): rum o'clock with sunset trigger, audio fanfare, and countdown"
```

---

## Yhteenveto deploysta

```bash
# Firmware (Task 1):
cd susieq_dashboard && pio run -t upload

# Dashboard HTML (kaikki muut taskit):
pio run -t uploadfs
```

OTA-deploylla (langaton):
```bash
pio run -e susieq-dashboard-ota -t uploadfs
```
