# SusieQ Remote Monitoring — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Mahdollistaa SusieQ:n sensoridatan ja mastokamerakuvien lukeminen mistä tahansa — GL-XE300 (4G) bridgaa datan Supabaseen, Netlify-sivu näyttää sen.

**Architecture:** GL-XE300 pyörittää OpenWRT-shell-skriptejä: cron 60s pollaa ESP32:n `/data`-endpointia ja postaa Supabaseen; cron 10s tarkistaa `camera_commands`-taulun ja nappaa JPEG:n mastokameralta tarvittaessa. Netlify-sivu on staattinen HTML/JS joka lukee Supabasesta Supabase JS -clientillä. Mastokamerat (ESP32-CAM) toimivat STA-modessa GL-XE300:n omassa verkossa.

**Tech Stack:** OpenWRT shell + curl, Supabase (PostgreSQL + Storage + Auth), Netlify (staattinen), vanilla HTML/JS, Supabase JS v2, PlatformIO (ESP32-CAM firmware)

---

## Tiedostorakenne

```
susieq_supabase/           ← SQL-migraatiot (ajetaan Supabase SQL Editorissa)
  001_tables.sql
  002_rls.sql
  003_pg_cron.sql

susieq_glxe300/            ← GL-XE300:lle SCP:llä siirrettävät skriptit
  susieq-sensors.sh
  susieq-camera.sh
  susieq.env.example
  README.md

susieq_remote/             ← Netlify-projekti (oma GitHub-repo)
  index.html
  app.js
  netlify.toml

susieq_mastocam/           ← Mastokameran PlatformIO-projekti
  platformio.ini
  include/config.h
  src/main.cpp
```

---

## Task 1: Supabase — taulut ja Storage

**Files:**
- Create: `susieq_supabase/001_tables.sql`
- Create: `susieq_supabase/002_rls.sql`
- Create: `susieq_supabase/003_pg_cron.sql`

### Esivaatimus
Luo Supabase-projekti osoitteessa https://supabase.com — valitse EU-region (Frankfurt). Kirjoita muistiin:
- **Project URL:** `https://<project-ref>.supabase.co`
- **anon key** (Settings → API → Project API keys → anon public)
- **service_role key** (Settings → API → Project API keys → service_role secret)

- [ ] **Step 1: Luo taulujen SQL-tiedosto**

```sql
-- susieq_supabase/001_tables.sql
CREATE TABLE sensor_readings (
  id          bigserial PRIMARY KEY,
  recorded_at timestamptz NOT NULL DEFAULT now(),
  data        jsonb       NOT NULL
);

CREATE INDEX sensor_readings_recorded_at_idx ON sensor_readings (recorded_at DESC);

CREATE TABLE camera_commands (
  id         bigserial PRIMARY KEY,
  created_at timestamptz NOT NULL DEFAULT now(),
  camera     text        NOT NULL CHECK (camera IN ('masto1', 'masto2')),
  status     text        NOT NULL DEFAULT 'pending'
                         CHECK (status IN ('pending', 'done', 'error')),
  image_url  text
);
```

- [ ] **Step 2: Aja SQL Supabase SQL Editorissa**

Avaa https://supabase.com → projektisi → SQL Editor → New query → liitä `001_tables.sql` → Run.

Odotettu tulos: "Success. No rows returned."

- [ ] **Step 3: Luo RLS-politiikat**

```sql
-- susieq_supabase/002_rls.sql

-- sensor_readings: authenticated lukee, kaikki kirjoittavat (service_role ohittaa RLS:n automaattisesti)
ALTER TABLE sensor_readings ENABLE ROW LEVEL SECURITY;
CREATE POLICY "authenticated can read"
  ON sensor_readings FOR SELECT TO authenticated USING (true);

-- camera_commands: authenticated lukee ja lisää rivejä
ALTER TABLE camera_commands ENABLE ROW LEVEL SECURITY;
CREATE POLICY "authenticated can read commands"
  ON camera_commands FOR SELECT TO authenticated USING (true);
CREATE POLICY "authenticated can insert commands"
  ON camera_commands FOR INSERT TO authenticated WITH CHECK (true);
```

Aja SQL Editorissa. Odotettu tulos: "Success."

- [ ] **Step 4: Luo pg_cron automaattiseen siivoon**

Settings → Extensions → ota käyttöön `pg_cron`. Sen jälkeen:

```sql
-- susieq_supabase/003_pg_cron.sql
-- Poistaa yli 7 päivää vanhat sensor_readings-rivit joka yö klo 03:00 UTC
SELECT cron.schedule(
  'cleanup-sensor-readings',
  '0 3 * * *',
  $$ DELETE FROM sensor_readings WHERE recorded_at < now() - INTERVAL '7 days' $$
);
-- Poistaa yli 24h vanhat camera_commands-rivit
SELECT cron.schedule(
  'cleanup-camera-commands',
  '30 3 * * *',
  $$ DELETE FROM camera_commands WHERE created_at < now() - INTERVAL '24 hours' $$
);
```

Aja SQL Editorissa.

- [ ] **Step 5: Luo Storage-bucket**

Supabase Dashboard → Storage → New bucket:
- Name: `kamerat`
- Public: **ei** (private, signed URLs tai authenticated-only)

- [ ] **Step 6: Aseta Storage RLS**

```sql
-- Aja SQL Editorissa:
CREATE POLICY "authenticated can read kamerat"
  ON storage.objects FOR SELECT TO authenticated
  USING (bucket_id = 'kamerat');

-- service_role kirjoittaa (ohittaa RLS:n automaattisesti)
```

- [ ] **Step 7: Luo testitunnus Supabase Authiin**

Authentication → Users → Add user:
- Email: oma sähköpostiosoitteesi
- Password: vahva salasana

- [ ] **Step 8: Testaa Supabase REST API curl:lla**

```bash
# Korvaa <URL> ja <ANON_KEY> omilla arvoilla
curl -s "https://<URL>/rest/v1/sensor_readings?limit=1" \
  -H "apikey: <ANON_KEY>" \
  -H "Authorization: Bearer <ANON_KEY>"
```

Odotettu tulos: `[]` (tyhjä taulukko, ei virhettä)

- [ ] **Step 9: Commit**

```bash
git add susieq_supabase/
git commit -m "feat(supabase): tables, RLS, pg_cron, storage bucket"
```

---

## Task 2: GL-XE300 — verkkoasetukset (AP+STA)

**Tavoite:** GL-XE300 yhdistää SusieQ-Data:an (STA) ja ajaa samaan aikaan omaa SusieQ-Net AP:ta mastokameroita varten. 4G on WAN.

**Huom:** Single-radio OpenWRT. SusieQ-Data on kanavalla 6 — SusieQ-Net täytyy myös olla kanavalla 6. Muuta tarvittaessa ensin SusieQ-Data:n kanava config.h:ssa ja reflashaa cockpit.

### Esivaatimus
- Yhdistä tietokone GL-XE300:n admin-WiFiin tai LAN-porttiin
- Avaa admin UI: http://192.168.8.1
- Ota SSH käyttöön: System → Administration → SSH Access

- [ ] **Step 1: SSH GL-XE300:lle**

```bash
ssh root@192.168.8.1
```

- [ ] **Step 2: Tarkista nykyinen WiFi-tila**

```bash
uci show wireless
```

Merkitse muistiin olemassa oleva radio-nimi (yleensä `radio0`).

- [ ] **Step 3: Lisää STA-interface SusieQ-Data:an**

```bash
# Lisää uusi wireless-interface STA-modeen
uci set wireless.susieq_sta=wifi-iface
uci set wireless.susieq_sta.device='radio0'
uci set wireless.susieq_sta.mode='sta'
uci set wireless.susieq_sta.ssid='SusieQ-Data'
uci set wireless.susieq_sta.key='susieq123'
uci set wireless.susieq_sta.network='susieq_data'

# Luo sille oma network-interface (ei reititettävää — vain LAN-seginmentti)
uci set network.susieq_data=interface
uci set network.susieq_data.proto='dhcp'

uci commit wireless
uci commit network
wifi reload
```

- [ ] **Step 4: Tarkista yhteys SusieQ-Data:an**

```bash
# Odota 10s ja tarkista saiko GL-XE300 IP:n
ip addr show | grep "192.168.4"
# Pitäisi näyttää 192.168.4.x
ping -c 3 192.168.4.1
```

Odotettu: `3 packets transmitted, 3 received`

- [ ] **Step 5: Luo SusieQ-Net AP mastokameroille**

```bash
# Uusi AP-interface samalle radiolle (sama kanava = ei konfliktia)
uci set wireless.susieq_net=wifi-iface
uci set wireless.susieq_net.device='radio0'
uci set wireless.susieq_net.mode='ap'
uci set wireless.susieq_net.ssid='SusieQ-Net'
uci set wireless.susieq_net.key='susieq123'
uci set wireless.susieq_net.network='lan'

uci commit wireless
wifi reload
```

- [ ] **Step 6: Varmista molemmat interfacet toimivat**

```bash
iwinfo
# Pitäisi näyttää sekä SusieQ-Data (mode: Client) että SusieQ-Net (mode: Master)
```

- [ ] **Step 7: Testaa internet 4G:n kautta**

```bash
curl -s https://httpbin.org/ip
# Pitäisi palauttaa julkinen 4G IP-osoite
```

---

## Task 3: GL-XE300 — sensori-bridge-skripti

**Files:**
- Create: `susieq_glxe300/susieq-sensors.sh`
- Create: `susieq_glxe300/susieq.env.example`

- [ ] **Step 1: Luo ympäristömuuttujatiedosto (paikallisesti)**

```bash
# susieq_glxe300/susieq.env.example
SUPABASE_URL=https://<project-ref>.supabase.co
SUPABASE_SERVICE_KEY=eyJ...
MASTO1_IP=192.168.8.101
MASTO2_IP=192.168.8.102
```

- [ ] **Step 2: Luo sensori-bridge-skripti**

```sh
#!/bin/sh
# susieq_glxe300/susieq-sensors.sh
# Pollaa ESP32:n /data-endpointia ja postaa Supabaseen

. /etc/susieq.env

DATA=$(curl -sf --max-time 10 http://192.168.4.1/data)
if [ $? -ne 0 ] || [ -z "$DATA" ]; then
    logger -t susieq-sensors "ERROR: failed to read ESP32 /data"
    exit 1
fi

HTTP_CODE=$(curl -sf --max-time 15 \
    -o /dev/null -w "%{http_code}" \
    -X POST "${SUPABASE_URL}/rest/v1/sensor_readings" \
    -H "apikey: ${SUPABASE_SERVICE_KEY}" \
    -H "Authorization: Bearer ${SUPABASE_SERVICE_KEY}" \
    -H "Content-Type: application/json" \
    -H "Prefer: return=minimal" \
    -d "{\"data\": ${DATA}}")

if [ "$HTTP_CODE" != "201" ]; then
    logger -t susieq-sensors "ERROR: Supabase POST failed (HTTP $HTTP_CODE)"
    exit 1
fi

logger -t susieq-sensors "OK: sensor data posted"
```

- [ ] **Step 3: Siirrä skripti GL-XE300:lle**

```bash
# Ajetaan kehityskoneella
scp susieq_glxe300/susieq-sensors.sh root@192.168.8.1:/usr/bin/
ssh root@192.168.8.1 chmod +x /usr/bin/susieq-sensors.sh
```

- [ ] **Step 4: Luo /etc/susieq.env GL-XE300:lle**

```bash
ssh root@192.168.8.1
# GL-XE300:lla:
cat > /etc/susieq.env << 'EOF'
SUPABASE_URL=https://<project-ref>.supabase.co
SUPABASE_SERVICE_KEY=eyJ...oikea_avain...
MASTO1_IP=192.168.8.101
MASTO2_IP=192.168.8.102
EOF
chmod 600 /etc/susieq.env
```

- [ ] **Step 5: Testaa skripti manuaalisesti**

```bash
# GL-XE300:lla:
/usr/bin/susieq-sensors.sh
echo "Exit code: $?"
# Odotettu: Exit code: 0

# Tarkista Supabase SQL Editorissa:
SELECT recorded_at, data->>'uptime_s' as uptime FROM sensor_readings ORDER BY recorded_at DESC LIMIT 1;
# Pitäisi näyttää rivi muutaman sekunnin ikäisenä
```

- [ ] **Step 6: Lisää crontab**

```bash
# GL-XE300:lla:
echo "* * * * * /usr/bin/susieq-sensors.sh" >> /etc/crontabs/root
/etc/init.d/cron restart
```

- [ ] **Step 7: Varmista cron toimii**

```bash
# Odota 2 minuuttia, sitten tarkista:
logread | grep susieq-sensors | tail -5
# Pitäisi näyttää "OK: sensor data posted" kahdesti
```

- [ ] **Step 8: Commit**

```bash
git add susieq_glxe300/
git commit -m "feat(glxe300): sensor bridge script + env example"
```

---

## Task 4: Netlify — projektirakenne ja autentikointi

**Files:**
- Create: `susieq_remote/netlify.toml`
- Create: `susieq_remote/index.html`
- Create: `susieq_remote/app.js`

**Huom:** `susieq_remote/` on oma erillinen GitHub-repo Netlifya varten, ei tämä SusieQ-repo. Luo uusi repo GitHubissa nimellä `susieq-remote`.

- [ ] **Step 1: Luo netlify.toml**

```toml
# susieq_remote/netlify.toml
[build]
  publish = "."

[[headers]]
  for = "/*"
  [headers.values]
    Content-Security-Policy = "default-src 'self' https://*.supabase.co https://cdn.jsdelivr.net; script-src 'self' 'unsafe-inline' https://cdn.jsdelivr.net; style-src 'self' 'unsafe-inline'"
    X-Frame-Options = "DENY"
```

- [ ] **Step 2: Luo index.html — kirjautumissivu ja päärakenne**

```html
<!DOCTYPE html>
<html lang="fi">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>SusieQ — Etähallinta</title>
  <style>
    :root {
      --bg: #0d1117;
      --surface: #161b22;
      --border: #30363d;
      --text: #e6edf3;
      --muted: #8b949e;
      --green: #3fb950;
      --yellow: #d29922;
      --red: #f85149;
      --blue: #58a6ff;
      --accent: #1f6feb;
    }
    * { box-sizing: border-box; margin: 0; padding: 0; }
    body { background: var(--bg); color: var(--text); font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', sans-serif; min-height: 100vh; }

    /* Login */
    #login-view { display: flex; align-items: center; justify-content: center; min-height: 100vh; }
    .login-card { background: var(--surface); border: 1px solid var(--border); border-radius: 12px; padding: 2rem; width: 100%; max-width: 380px; }
    .login-card h1 { font-size: 1.4rem; margin-bottom: 1.5rem; text-align: center; }
    .login-card input { width: 100%; padding: .75rem 1rem; background: var(--bg); border: 1px solid var(--border); border-radius: 8px; color: var(--text); font-size: 1rem; margin-bottom: 1rem; }
    .login-card button { width: 100%; padding: .75rem; background: var(--accent); border: none; border-radius: 8px; color: #fff; font-size: 1rem; cursor: pointer; }
    .login-card button:hover { background: #388bfd; }
    .error-msg { color: var(--red); font-size: .875rem; margin-top: .5rem; min-height: 1.2em; }

    /* Dashboard */
    #dashboard-view { display: none; }
    header { background: var(--surface); border-bottom: 1px solid var(--border); padding: 1rem 1.5rem; display: flex; align-items: center; justify-content: space-between; }
    header h1 { font-size: 1.2rem; }
    .status-dot { width: 10px; height: 10px; border-radius: 50%; display: inline-block; margin-right: .4rem; }
    .dot-green { background: var(--green); }
    .dot-yellow { background: var(--yellow); }
    .dot-red { background: var(--red); }
    .logout-btn { background: none; border: 1px solid var(--border); color: var(--muted); border-radius: 6px; padding: .4rem .8rem; cursor: pointer; font-size: .875rem; }

    main { padding: 1.5rem; max-width: 1100px; margin: 0 auto; }
    .grid { display: grid; grid-template-columns: repeat(auto-fill, minmax(260px, 1fr)); gap: 1rem; margin-bottom: 2rem; }
    .card { background: var(--surface); border: 1px solid var(--border); border-radius: 12px; padding: 1.25rem; }
    .card h2 { font-size: .875rem; color: var(--muted); text-transform: uppercase; letter-spacing: .05em; margin-bottom: .75rem; }
    .card .value { font-size: 2rem; font-weight: 600; }
    .card .unit { font-size: 1rem; color: var(--muted); margin-left: .25rem; }
    .card .sub { font-size: .875rem; color: var(--muted); margin-top: .25rem; }
    .invalid { color: var(--muted); font-size: 1rem; }

    /* Kamerat */
    .cameras-section h2 { font-size: 1rem; margin-bottom: 1rem; }
    .cameras-grid { display: grid; grid-template-columns: repeat(auto-fill, minmax(300px, 1fr)); gap: 1.5rem; }
    .camera-card { background: var(--surface); border: 1px solid var(--border); border-radius: 12px; overflow: hidden; }
    .camera-card .cam-header { padding: 1rem 1.25rem; display: flex; align-items: center; justify-content: space-between; }
    .camera-card h3 { font-size: .9rem; }
    .snap-btn { background: var(--accent); border: none; border-radius: 6px; color: #fff; padding: .5rem 1rem; cursor: pointer; font-size: .875rem; }
    .snap-btn:disabled { background: var(--border); cursor: not-allowed; }
    .cam-image { width: 100%; aspect-ratio: 4/3; object-fit: cover; background: var(--bg); display: block; }
    .cam-placeholder { width: 100%; aspect-ratio: 4/3; display: flex; align-items: center; justify-content: center; color: var(--muted); font-size: .875rem; background: var(--bg); }
    .cam-status { padding: .5rem 1.25rem; font-size: .8rem; color: var(--muted); min-height: 2rem; }
  </style>
</head>
<body>

<!-- Kirjautuminen -->
<div id="login-view">
  <div class="login-card">
    <h1>⛵ SusieQ</h1>
    <input type="email" id="email" placeholder="Sähköposti" autocomplete="email">
    <input type="password" id="password" placeholder="Salasana" autocomplete="current-password">
    <button id="login-btn">Kirjaudu sisään</button>
    <div class="error-msg" id="login-error"></div>
  </div>
</div>

<!-- Dashboard -->
<div id="dashboard-view">
  <header>
    <h1>⛵ SusieQ — Etähallinta</h1>
    <div style="display:flex;align-items:center;gap:1rem">
      <span id="connection-status"></span>
      <button class="logout-btn" id="logout-btn">Kirjaudu ulos</button>
    </div>
  </header>
  <main>
    <div class="grid" id="sensor-grid">
      <!-- Täytetään app.js:stä -->
    </div>
    <div class="cameras-section">
      <h2>Mastokamerat</h2>
      <div class="cameras-grid">
        <div class="camera-card" id="cam-masto1">
          <div class="cam-header">
            <h3>Masto 1</h3>
            <button class="snap-btn" data-cam="masto1">Ota kuva</button>
          </div>
          <div class="cam-placeholder" id="cam-masto1-placeholder">Ei kuvaa</div>
          <img class="cam-image" id="cam-masto1-img" style="display:none" alt="Masto 1">
          <div class="cam-status" id="cam-masto1-status"></div>
        </div>
        <div class="camera-card" id="cam-masto2">
          <div class="cam-header">
            <h3>Masto 2</h3>
            <button class="snap-btn" data-cam="masto2">Ota kuva</button>
          </div>
          <div class="cam-placeholder" id="cam-masto2-placeholder">Ei kuvaa</div>
          <img class="cam-image" id="cam-masto2-img" style="display:none" alt="Masto 2">
          <div class="cam-status" id="cam-masto2-status"></div>
        </div>
      </div>
    </div>
  </main>
</div>

<script src="https://cdn.jsdelivr.net/npm/@supabase/supabase-js@2/dist/umd/supabase.min.js"></script>
<script src="app.js"></script>
</body>
</html>
```

- [ ] **Step 3: Luo app.js — Supabase-client ja autentikointi**

```js
// susieq_remote/app.js
const SUPABASE_URL  = 'https://<project-ref>.supabase.co';
const SUPABASE_ANON = '<anon-key>';

const sb = supabase.createClient(SUPABASE_URL, SUPABASE_ANON);

// ── Auth ──────────────────────────────────────────────────────────────
async function initAuth() {
  const { data: { session } } = await sb.auth.getSession();
  if (session) showDashboard();
  else showLogin();

  sb.auth.onAuthStateChange((_event, session) => {
    if (session) showDashboard();
    else showLogin();
  });
}

function showLogin() {
  document.getElementById('login-view').style.display = 'flex';
  document.getElementById('dashboard-view').style.display = 'none';
}

function showDashboard() {
  document.getElementById('login-view').style.display = 'none';
  document.getElementById('dashboard-view').style.display = 'block';
  startDashboard();
}

document.getElementById('login-btn').addEventListener('click', async () => {
  const email    = document.getElementById('email').value.trim();
  const password = document.getElementById('password').value;
  const errEl    = document.getElementById('login-error');
  errEl.textContent = '';

  const { error } = await sb.auth.signInWithPassword({ email, password });
  if (error) errEl.textContent = error.message;
});

document.getElementById('logout-btn').addEventListener('click', () => sb.auth.signOut());

// ── Dashboard ─────────────────────────────────────────────────────────
let refreshTimer = null;

function startDashboard() {
  loadSensorData();
  if (refreshTimer) clearInterval(refreshTimer);
  refreshTimer = setInterval(loadSensorData, 60_000);
  setupCameraButtons();
}

async function loadSensorData() {
  const { data, error } = await sb
    .from('sensor_readings')
    .select('recorded_at, data')
    .order('recorded_at', { ascending: false })
    .limit(1)
    .single();

  if (error || !data) {
    updateConnectionStatus(null);
    return;
  }

  updateConnectionStatus(data.recorded_at);
  renderSensors(data.data);
}

function updateConnectionStatus(recordedAt) {
  const el = document.getElementById('connection-status');
  if (!recordedAt) {
    el.innerHTML = '<span class="status-dot dot-red"></span>Ei yhteyttä';
    return;
  }
  const ageMin = (Date.now() - new Date(recordedAt).getTime()) / 60_000;
  if (ageMin < 2)       el.innerHTML = `<span class="status-dot dot-green"></span>Yhteys OK`;
  else if (ageMin < 10) el.innerHTML = `<span class="status-dot dot-yellow"></span>${Math.round(ageMin)} min sitten`;
  else                  el.innerHTML = `<span class="status-dot dot-red"></span>Offline ${Math.round(ageMin)} min`;
}

function renderSensors(d) {
  const grid = document.getElementById('sensor-grid');
  grid.innerHTML = '';

  const cards = [
    batteryCard(d.battery),
    solarCard(d.solar),
    windCard(d.wind),
    waterCard(d.water),
    fuelCard(d.fuel),
    rumCard(d.rum),
    gpsCard(d.gps),
    weatherCard(d.weather),
  ];

  cards.forEach(html => {
    const div = document.createElement('div');
    div.className = 'card';
    div.innerHTML = html;
    grid.appendChild(div);
  });
}

function val(v, unit, decimals = 1) {
  if (v === null || v === undefined) return '<span class="invalid">–</span>';
  return `<span class="value">${Number(v).toFixed(decimals)}</span><span class="unit">${unit}</span>`;
}

function batteryCard(b) {
  if (!b?.valid) return `<h2>Akku</h2><p class="invalid">Ei tietoa</p>`;
  return `<h2>Akku</h2>${val(b.voltage, 'V')}
    <div class="sub">Virta: ${b.current >= 0 ? '+' : ''}${b.current?.toFixed(1)} A &nbsp;·&nbsp; SOC: ${b.soc?.toFixed(0)}%</div>`;
}

function solarCard(s) {
  if (!s?.valid) return `<h2>Aurinkopaneeli</h2><p class="invalid">Ei tietoa</p>`;
  return `<h2>Aurinkopaneeli</h2>${val(s.pv_power, 'W', 0)}
    <div class="sub">${s.pv_voltage?.toFixed(1)} V &nbsp;·&nbsp; Tila: ${s.state ?? '–'}</div>`;
}

function windCard(w) {
  if (!w?.valid) return `<h2>Tuuli</h2><p class="invalid">Ei tietoa</p>`;
  return `<h2>Tuuli</h2>${val(w.speed, 'm/s')}
    <div class="sub">Suunta: ${w.direction?.toFixed(0)}°</div>`;
}

function waterCard(w) {
  if (!w?.valid) return `<h2>Vesitankki</h2><p class="invalid">Ei tietoa</p>`;
  return `<h2>Vesitankki</h2>${val(w.liters, 'L')}
    <div class="sub">${w.level_pct?.toFixed(0)}%</div>`;
}

function fuelCard(f) {
  if (!f?.valid) return `<h2>Polttoaine</h2><p class="invalid">Ei tietoa</p>`;
  return `<h2>Polttoaine</h2>${val(f.liters, 'L')}
    <div class="sub">${f.pct?.toFixed(0)}%</div>`;
}

function rumCard(r) {
  if (!r?.valid) return `<h2>Rommi</h2><p class="invalid">Ei tietoa</p>`;
  return `<h2>Rommi</h2>${val(r.liters, 'L')}
    <div class="sub">${r.pct?.toFixed(0)}%</div>`;
}

function gpsCard(g) {
  if (!g?.fix) return `<h2>GPS</h2><p class="invalid">Ei fixi</p>`;
  return `<h2>GPS</h2><div class="sub" style="font-size:.95rem">
    ${g.lat?.toFixed(5)}° N, ${g.lon?.toFixed(5)}° E<br>
    SOG: ${g.sog_knots?.toFixed(1)} kn &nbsp;·&nbsp; COG: ${g.cog_deg}°
  </div>`;
}

function weatherCard(w) {
  if (!w?.valid) return `<h2>Sää</h2><p class="invalid">Ei tietoa</p>`;
  return `<h2>Sää</h2>${val(w.air_temp, '°C')}
    <div class="sub">
      Kosteus: ${w.humidity?.toFixed(0)}% &nbsp;·&nbsp;
      Paine: ${w.pressure?.toFixed(1)} hPa<br>
      Vesitemp: ${w.water_temp?.toFixed(1) ?? '–'} °C
    </div>`;
}

// ── Kamerat ───────────────────────────────────────────────────────────
function setupCameraButtons() {
  document.querySelectorAll('.snap-btn').forEach(btn => {
    btn.addEventListener('click', () => requestSnapshot(btn.dataset.cam));
  });
}

async function requestSnapshot(camera) {
  const btn    = document.querySelector(`.snap-btn[data-cam="${camera}"]`);
  const status = document.getElementById(`cam-${camera}-status`);

  btn.disabled = true;
  status.textContent = 'Lähetetään pyyntö...';

  const { data, error } = await sb
    .from('camera_commands')
    .insert({ camera, status: 'pending' })
    .select('id')
    .single();

  if (error || !data) {
    status.textContent = 'Virhe: pyynnön lähetys epäonnistui';
    btn.disabled = false;
    return;
  }

  const commandId = data.id;
  status.textContent = 'Odotetaan GL-XE300:a... (max 30s)';
  pollCommandStatus(commandId, camera, btn, status);
}

async function pollCommandStatus(commandId, camera, btn, statusEl) {
  const start   = Date.now();
  const timeout = 60_000; // 60s max

  const poll = setInterval(async () => {
    if (Date.now() - start > timeout) {
      clearInterval(poll);
      statusEl.textContent = 'Aikakatkaisu — tarkista GL-XE300:n yhteys';
      btn.disabled = false;
      return;
    }

    const { data } = await sb
      .from('camera_commands')
      .select('status, image_url')
      .eq('id', commandId)
      .single();

    if (!data) return;

    if (data.status === 'done' && data.image_url) {
      clearInterval(poll);
      // image_url on storage-polku — generoi signed URL (1h voimassa)
      const { data: signedData } = await sb.storage
        .from('kamerat')
        .createSignedUrl(data.image_url, 3600);
      if (signedData?.signedUrl) showCameraImage(camera, signedData.signedUrl);
      else statusEl.textContent = 'Virhe: kuvan URL:n generointi epäonnistui';
      statusEl.textContent = `Kuva otettu ${new Date().toLocaleTimeString('fi-FI')}`;
      btn.disabled = false;
    } else if (data.status === 'error') {
      clearInterval(poll);
      statusEl.textContent = 'Virhe: kameran kuvaus epäonnistui';
      btn.disabled = false;
    }
  }, 2_000); // pollaa 2s välein
}

function showCameraImage(camera, url) {
  const img         = document.getElementById(`cam-${camera}-img`);
  const placeholder = document.getElementById(`cam-${camera}-placeholder`);
  img.src           = url;
  img.style.display = 'block';
  placeholder.style.display = 'none';
}

// ── Init ──────────────────────────────────────────────────────────────
initAuth();
```

- [ ] **Step 4: Luo uusi GitHub-repo ja deploy Netlifyyn**

1. Luo uusi GitHub-repo nimellä `susieq-remote` (private)
2. Pushaa `susieq_remote/`-kansion sisältö sinne:
```bash
cd susieq_remote
git init
git add .
git commit -m "feat: initial Netlify dashboard"
git remote add origin git@github.com:<username>/susieq-remote.git
git push -u origin main
```
3. Netlify Dashboard → Add new site → Import from Git → valitse `susieq-remote`
4. Build settings: Base = `.`, Publish = `.`, ei build-komentoa
5. Deploy

- [ ] **Step 5: Testaa kirjautuminen**

Avaa Netlify URL selaimessa. Kirjaudu aiemmin luodulla testitunnuksella. Odotettu: dashboard näkyy sensorikorttien kanssa (vaikka arvot ovat "–" jos ESP32 ei ole tavoitettavissa).

- [ ] **Step 6: Korvaa placeholderit app.js:ssä**

Avaa `app.js`, korvaa:
- `https://<project-ref>.supabase.co` → oikea Supabase URL
- `<anon-key>` → oikea anon-avain

Pushaa muutokset GitHubiin → Netlify deployaa automaattisesti.

---

## Task 5: GL-XE300 — kamera-bridge-skripti

**Huom:** Tämä task vaatii mastokameroiden (Task 6) olevan valmiita ja yhdistettynä `SusieQ-Net`-verkkoon. Asenna mastokamerat ensin.

**Files:**
- Create: `susieq_glxe300/susieq-camera.sh`

- [ ] **Step 1: Luo kamera-bridge-skripti**

```sh
#!/bin/sh
# susieq_glxe300/susieq-camera.sh
# Tarkistaa pending-kamerakomennot Supabasessa ja suorittaa ne

. /etc/susieq.env

# Hae pending-komennot (max 5 kerrallaan)
COMMANDS=$(curl -sf --max-time 10 \
    "${SUPABASE_URL}/rest/v1/camera_commands?status=eq.pending&select=id,camera&limit=5" \
    -H "apikey: ${SUPABASE_SERVICE_KEY}" \
    -H "Authorization: Bearer ${SUPABASE_SERVICE_KEY}")

if [ -z "$COMMANDS" ] || [ "$COMMANDS" = "[]" ]; then
    exit 0  # Ei komentoja — nopea exit
fi

# Jäsennä ID:t ja kamerat (yksinkertainen awk, ei jq:ta tarvita)
echo "$COMMANDS" | tr ',' '\n' | grep -E '"id"|"camera"' | while read line; do
    echo "$line"
done | awk -F'"' '
  /"id"/{id=$4}
  /"camera"/{
    cam=$4
    print id " " cam
  }
' | while read CMD_ID CAM_NAME; do
    [ -z "$CMD_ID" ] && continue
    [ -z "$CAM_NAME" ] && continue

    # Selvitä kameran IP
    case "$CAM_NAME" in
        masto1) CAM_IP="$MASTO1_IP" ;;
        masto2) CAM_IP="$MASTO2_IP" ;;
        *)
            logger -t susieq-camera "ERROR: unknown camera $CAM_NAME"
            continue
            ;;
    esac

    # Nappaa JPEG kameralta
    TMPFILE="/tmp/susieq_${CAM_NAME}_$$.jpg"
    curl -sf --max-time 15 "http://${CAM_IP}/capture" -o "$TMPFILE"
    if [ $? -ne 0 ] || [ ! -s "$TMPFILE" ]; then
        logger -t susieq-camera "ERROR: capture failed for $CAM_NAME"
        rm -f "$TMPFILE"
        # Merkitse error
        curl -sf --max-time 10 \
            -X PATCH "${SUPABASE_URL}/rest/v1/camera_commands?id=eq.${CMD_ID}" \
            -H "apikey: ${SUPABASE_SERVICE_KEY}" \
            -H "Authorization: Bearer ${SUPABASE_SERVICE_KEY}" \
            -H "Content-Type: application/json" \
            -d '{"status":"error"}'
        continue
    fi

    # Upload Supabase Storageen
    STORAGE_PATH="${CAM_NAME}_latest.jpg"
    HTTP_CODE=$(curl -sf --max-time 30 \
        -o /dev/null -w "%{http_code}" \
        -X PUT \
        "${SUPABASE_URL}/storage/v1/object/kamerat/${STORAGE_PATH}" \
        -H "apikey: ${SUPABASE_SERVICE_KEY}" \
        -H "Authorization: Bearer ${SUPABASE_SERVICE_KEY}" \
        -H "Content-Type: image/jpeg" \
        --data-binary "@${TMPFILE}")
    rm -f "$TMPFILE"

    if [ "$HTTP_CODE" != "200" ] && [ "$HTTP_CODE" != "201" ]; then
        logger -t susieq-camera "ERROR: storage upload failed (HTTP $HTTP_CODE) for $CAM_NAME"
        curl -sf --max-time 10 \
            -X PATCH "${SUPABASE_URL}/rest/v1/camera_commands?id=eq.${CMD_ID}" \
            -H "apikey: ${SUPABASE_SERVICE_KEY}" \
            -H "Authorization: Bearer ${SUPABASE_SERVICE_KEY}" \
            -H "Content-Type: application/json" \
            -d '{"status":"error"}'
        continue
    fi

    # Tallenna storage-polku (Netlify generoi signed URL:n) ja merkitse done
    curl -sf --max-time 10 \
        -X PATCH "${SUPABASE_URL}/rest/v1/camera_commands?id=eq.${CMD_ID}" \
        -H "apikey: ${SUPABASE_SERVICE_KEY}" \
        -H "Authorization: Bearer ${SUPABASE_SERVICE_KEY}" \
        -H "Content-Type: application/json" \
        -d "{\"status\":\"done\",\"image_url\":\"${STORAGE_PATH}\"}"

    logger -t susieq-camera "OK: snapshot done for $CAM_NAME (cmd $CMD_ID)"
done
```

- [ ] **Step 2: Siirrä GL-XE300:lle**

```bash
scp susieq_glxe300/susieq-camera.sh root@192.168.8.1:/usr/bin/
ssh root@192.168.8.1 chmod +x /usr/bin/susieq-camera.sh
```

- [ ] **Step 3: Testaa manuaalisesti**

Ensin luo pending-komento Supabase SQL Editorissa:
```sql
INSERT INTO camera_commands (camera, status) VALUES ('masto1', 'pending');
```

Sitten GL-XE300:lla:
```bash
/usr/bin/susieq-camera.sh
logread | grep susieq-camera | tail -5
# Odotettu: "OK: snapshot done for masto1 (cmd X)"
```

Tarkista Supabase:
```sql
SELECT id, camera, status, image_url FROM camera_commands ORDER BY created_at DESC LIMIT 3;
```

- [ ] **Step 4: Lisää crontab**

```bash
ssh root@192.168.8.1
crontab -e
# Lisää nämä rivit (ajaa kamera-checkin joka 10s):
# * * * * * /usr/bin/susieq-camera.sh
# * * * * * sleep 10; /usr/bin/susieq-camera.sh
# * * * * * sleep 20; /usr/bin/susieq-camera.sh
# * * * * * sleep 30; /usr/bin/susieq-camera.sh
# * * * * * sleep 40; /usr/bin/susieq-camera.sh
# * * * * * sleep 50; /usr/bin/susieq-camera.sh
```

- [ ] **Step 5: Commit**

```bash
git add susieq_glxe300/susieq-camera.sh
git commit -m "feat(glxe300): camera bridge script with on-demand snapshots"
```

---

## Task 6: Mastokamera — ESP32-CAM firmware (STA-mode)

**Huom:** Tämä task toteutetaan kun mastokameran hardware on rakennettu. Tarvitset kaksi ESP32-CAM (AI-Thinker) -yksikköä 5W aurinkopaneeleineen ja LiPo-akkuineen.

**Files:**
- Create: `susieq_mastocam/platformio.ini`
- Create: `susieq_mastocam/include/config.h`
- Create: `susieq_mastocam/src/main.cpp`

- [ ] **Step 1: Luo platformio.ini**

```ini
; susieq_mastocam/platformio.ini
[env:mastocam]
platform     = espressif32
board        = esp32cam
framework    = arduino
monitor_speed = 115200
upload_speed  = 921600

lib_deps =
  ESP Async WebServer
  ArduinoJson

build_flags =
  -DCORE_DEBUG_LEVEL=0

[env:mastocam-ota]
extends      = env:mastocam
upload_protocol = espota
upload_port   = ${env:mastocam.upload_port}
```

- [ ] **Step 2: Luo config.h**

```cpp
// susieq_mastocam/include/config.h
#pragma once

// WiFi — yhdistää GL-XE300:n AP:iin
#define WIFI_SSID       "SusieQ-Net"
#define WIFI_PASSWORD   "susieq123"

// Kameran laatu (PSRAM tarvitaan SVGA:lle)
#define CAM_FRAMESIZE   FRAMESIZE_SVGA   // 800x600
#define CAM_QUALITY     12               // 0=paras, 63=huonoin

// Staattinen IP (helpottaa GL-XE300:n skriptejä)
// masto1: .101, masto2: .102 — muuta per yksikkö
#define STATIC_IP       "192.168.8.101"
#define GATEWAY_IP      "192.168.8.1"
#define SUBNET_MASK     "255.255.255.0"

// OTA
#define OTA_HOSTNAME    "susieq-masto1"
#define OTA_PASSWORD    "susieq_ota"
```

- [ ] **Step 3: Luo main.cpp**

```cpp
// susieq_mastocam/src/main.cpp
#include <Arduino.h>
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <ArduinoOTA.h>
#include "esp_camera.h"
#include "esp_wifi.h"
#include "../include/config.h"

// AI-Thinker ESP32-CAM pinout
#define PWDN_GPIO_NUM   32
#define RESET_GPIO_NUM  -1
#define XCLK_GPIO_NUM    0
#define SIOD_GPIO_NUM   26
#define SIOC_GPIO_NUM   27
#define Y9_GPIO_NUM     35
#define Y8_GPIO_NUM     34
#define Y7_GPIO_NUM     39
#define Y6_GPIO_NUM     36
#define Y5_GPIO_NUM     21
#define Y4_GPIO_NUM     19
#define Y3_GPIO_NUM     18
#define Y2_GPIO_NUM      5
#define VSYNC_GPIO_NUM  25
#define HREF_GPIO_NUM   23
#define PCLK_GPIO_NUM   22

AsyncWebServer server(80);

static bool init_camera() {
    camera_config_t config;
    config.ledc_channel = LEDC_CHANNEL_0;
    config.ledc_timer   = LEDC_TIMER_0;
    config.pin_d0       = Y2_GPIO_NUM;
    config.pin_d1       = Y3_GPIO_NUM;
    config.pin_d2       = Y4_GPIO_NUM;
    config.pin_d3       = Y5_GPIO_NUM;
    config.pin_d4       = Y6_GPIO_NUM;
    config.pin_d5       = Y7_GPIO_NUM;
    config.pin_d6       = Y8_GPIO_NUM;
    config.pin_d7       = Y9_GPIO_NUM;
    config.pin_xclk     = XCLK_GPIO_NUM;
    config.pin_pclk     = PCLK_GPIO_NUM;
    config.pin_vsync    = VSYNC_GPIO_NUM;
    config.pin_href     = HREF_GPIO_NUM;
    config.pin_sccb_sda = SIOD_GPIO_NUM;
    config.pin_sccb_scl = SIOC_GPIO_NUM;
    config.pin_pwdn     = PWDN_GPIO_NUM;
    config.pin_reset    = RESET_GPIO_NUM;
    config.xclk_freq_hz = 20000000;
    config.pixel_format = PIXFORMAT_JPEG;
    config.grab_mode    = CAMERA_GRAB_LATEST;

    if (psramFound()) {
        config.frame_size   = CAM_FRAMESIZE;
        config.jpeg_quality = CAM_QUALITY;
        config.fb_count     = 2;
        config.fb_location  = CAMERA_FB_IN_PSRAM;
    } else {
        config.frame_size   = FRAMESIZE_QVGA;
        config.jpeg_quality = 15;
        config.fb_count     = 1;
        config.fb_location  = CAMERA_FB_IN_DRAM;
    }

    esp_err_t err = esp_camera_init(&config);
    if (err != ESP_OK) {
        Serial.printf("[cam] init failed: 0x%x\n", err);
        return false;
    }
    Serial.println("[cam] initialized");
    return true;
}

static void handle_capture(AsyncWebServerRequest* req) {
    camera_fb_t* fb = esp_camera_fb_get();
    if (!fb) { req->send(500, "text/plain", "Capture failed"); return; }
    AsyncResponseStream* resp = req->beginResponseStream("image/jpeg");
    resp->write(fb->buf, fb->len);
    esp_camera_fb_return(fb);
    req->send(resp);
}

static void handle_status(AsyncWebServerRequest* req) {
    String json = "{\"ip\":\"" + WiFi.localIP().toString() +
                  "\",\"rssi\":" + WiFi.RSSI() +
                  ",\"heap\":" + ESP.getFreeHeap() +
                  ",\"uptime_s\":" + (millis() / 1000) + "}";
    req->send(200, "application/json", json);
}

void setup() {
    Serial.begin(115200);
    delay(500);
    Serial.println("\n[SusieQ-Masto] starting...");

    // Staattinen IP
    IPAddress ip, gw, sn;
    ip.fromString(STATIC_IP);
    gw.fromString(GATEWAY_IP);
    sn.fromString(SUBNET_MASK);
    WiFi.config(ip, gw, sn);

    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    esp_wifi_set_ps(WIFI_PS_NONE);

    Serial.print("[wifi] connecting to " WIFI_SSID);
    unsigned long t = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - t < 30000) {
        delay(500); Serial.print(".");
    }
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("\n[wifi] FAILED — rebooting in 5s");
        delay(5000);
        ESP.restart();
    }
    Serial.printf("\n[wifi] connected: %s (RSSI %d)\n",
                  WiFi.localIP().toString().c_str(), WiFi.RSSI());

    if (!init_camera()) {
        Serial.println("[cam] FATAL — rebooting");
        delay(3000);
        ESP.restart();
    }

    DefaultHeaders::Instance().addHeader("Access-Control-Allow-Origin", "*");
    server.on("/capture", HTTP_GET, handle_capture);
    server.on("/status",  HTTP_GET, handle_status);
    server.onNotFound([](AsyncWebServerRequest* req) {
        req->send(404, "text/plain", "Not found");
    });
    server.begin();
    Serial.println("[http] server started");

    ArduinoOTA.setHostname(OTA_HOSTNAME);
    ArduinoOTA.setPassword(OTA_PASSWORD);
    ArduinoOTA.begin();
    Serial.println("[ota] ready");
}

void loop() {
    ArduinoOTA.handle();
    delay(10);
}
```

- [ ] **Step 4: Flash masto1**

Yhdistä ESP32-CAM FTDI-adapteriin (TX→RX, RX→TX, GND→GND, 5V→5V, IO0→GND flash-modessa). Varmista `STATIC_IP = "192.168.8.101"` config.h:ssa.

```bash
cd susieq_mastocam
pio run -e mastocam -t upload
```

- [ ] **Step 5: Testaa masto1**

GL-XE300:lla tai siihen yhdistetyllä laitteella:
```bash
curl -s http://192.168.8.101/status
# Odotettu: {"ip":"192.168.8.101","rssi":-XX,"heap":XXXXX,"uptime_s":XX}

curl -o /tmp/test.jpg http://192.168.8.101/capture
ls -la /tmp/test.jpg
# Odotettu: tiedosto > 5000 tavua
```

- [ ] **Step 6: Flash masto2**

Muuta config.h: `STATIC_IP = "192.168.8.102"`, `OTA_HOSTNAME = "susieq-masto2"`. Flash ja testaa samoin.

- [ ] **Step 7: Päivitä GL-XE300:n susieq.env**

```bash
ssh root@192.168.8.1
vi /etc/susieq.env
# Päivitä:
# MASTO1_IP=192.168.8.101
# MASTO2_IP=192.168.8.102
```

- [ ] **Step 8: Commit**

```bash
git add susieq_mastocam/
git commit -m "feat(mastocam): ESP32-CAM firmware STA-mode + /capture endpoint"
```

---

## Task 7: End-to-end integraatiotesti

- [ ] **Step 1: Sensoridata end-to-end**

1. Tarkista että GL-XE300:n cron pyörii: `logread | grep susieq-sensors | tail -3`
2. Avaa Netlify-sivu selaimessa
3. Kirjaudu sisään
4. Varmista sensorikortit näyttävät arvoja (ei "–")
5. Tarkista yhteysindikaattori: vihreä

- [ ] **Step 2: Kamerakuva end-to-end**

1. Klikkaa "Ota kuva" Masto 1 -kortissa
2. Odotettu flow: nappi harmaantuu → "Lähetetään pyyntö..." → "Odotetaan GL-XE300:a..." → kuva ilmestyy alle 30s

- [ ] **Step 3: Offline-testi**

Sammuta GL-XE300 tai irrota 4G-SIM. Odota 3 minuuttia. Lataa sivu uudelleen. Odotettu: yhteysindikaattori muuttuu keltaiseksi/punaiseksi.

- [ ] **Step 4: Lopullinen commit**

```bash
git add .
git commit -m "feat: remote monitoring complete — GL-XE300 bridge + Netlify + Supabase"
git push
```

---

## Muistettavaa

- **service_role-avain** pysyy vain GL-XE300:n `/etc/susieq.env`:ssä (chmod 600). Ei koskaan Netlify-koodissa.
- **anon-avain** on julkinen mutta RLS suojaa tietokannan — vain kirjautunut käyttäjä näkee datan.
- GL-XE300:n WiFi-kanava: SusieQ-Data (ch6) ja SusieQ-Net täytyy olla samalla kanavalla — single-radio rajoitus.
- Mastokameroiden staattinen IP helpottaa scriptiä; vaihtoehto on mDNS (ei tuettu OpenWRT:n oletusasennuksessa).
