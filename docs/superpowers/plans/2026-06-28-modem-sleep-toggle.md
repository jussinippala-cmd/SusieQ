# Modeemin lepotila-toggle Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Lisää susieq.net-ohjauspaneeliin toggle jolla voi kytkeä GL-XE300-modeemin 4G-lepotilan (22–06) päälle/pois pysyvästi Supabasen kautta.

**Architecture:** Supabaseen luodaan `modem_settings`-taulukko (yksi rivi, `sleep_enabled boolean`). susieq-remote-frontend lukee ja kirjoittaa arvon. GL-XE300:n `susieq-sleep.sh` tekee curl-kyselyn Supabaseen klo 22:00 ennen nukkumispäätöstä.

**Tech Stack:** Supabase (PostgreSQL + RLS + JS client), vanilla JS, HTML/CSS, BusyBox sh, python3 (OpenWrt)

## Global Constraints

- susieq-remote: `index.html` + `app.js` — ei build-steppiä, plain JS
- Supabase project ref: `wrcylgkeduujvoilqquz`, URL: `https://wrcylgkeduujvoilqquz.supabase.co`
- Supabase anon key: `sb_publishable_H51PHTwMasoUY1m7FnEKgQ_O5ceFj8O`
- Tyyli: dark GitHub -teema, CSS-muuttujat `--brass`, `--muted`, `--border-hi`, `--ff-mono`, `--parchment-dim`
- Koodi ja UI-tekstit suomeksi
- susieq_glxe300/susieq-sleep.sh — OpenWrt/BusyBox ash, python3 JSON-parsing kuten sensors.sh:ssa
- Modeemilla `/etc/susieq.env` sisältää `SUPABASE_URL` ja `SUPABASE_SERVICE_KEY`

---

## Tiedostokartta

| Tiedosto | Muutos |
|---|---|
| Supabase migration | Uusi taulukko `modem_settings` + RLS-politiikat |
| `susieq-remote/index.html` | CSS + HTML-kortti Ohjauspaneeli-välilehdelle |
| `susieq-remote/app.js` | `loadModemSettings()`, `_renderModemSleep()`, `toggleModemSleep()` + kutsu `initSwitchPanel`:ssa |
| `susieq_glxe300/susieq-sleep.sh` | Supabase-tarkistus ennen `AT+CFUN=4` |

---

## Task 1: Supabase — modem_settings-taulukko + RLS

**Files:**
- Supabase migration (aja MCP-työkalulla tai Supabase Dashboard → SQL Editor)

**Interfaces:**
- Produces: `modem_settings`-taulukko, jota Task 3 ja Task 4 käyttävät

- [ ] **Step 1: Aja migration Supabase-projektissa**

Aja seuraava SQL Supabase Dashboard → SQL Editorissa tai MCP:n `apply_migration`-työkalulla (project ref: `wrcylgkeduujvoilqquz`):

```sql
-- Modeemin lepotila-asetus
CREATE TABLE IF NOT EXISTS modem_settings (
  id int PRIMARY KEY DEFAULT 1,
  sleep_enabled boolean NOT NULL DEFAULT true,
  updated_at timestamptz DEFAULT now()
);

-- Varmista että vain yksi rivi on olemassa
INSERT INTO modem_settings (id, sleep_enabled)
VALUES (1, true)
ON CONFLICT (id) DO NOTHING;

-- RLS
ALTER TABLE modem_settings ENABLE ROW LEVEL SECURITY;

CREATE POLICY "anon voi lukea modem_settings"
  ON modem_settings FOR SELECT TO anon USING (true);

CREATE POLICY "auth voi paivittaa modem_settings"
  ON modem_settings FOR UPDATE TO authenticated USING (true);
```

- [ ] **Step 2: Tarkista että taulukko ja data ovat oikein**

Aja SQL Editorissa:
```sql
SELECT * FROM modem_settings;
```
Odotettu tulos: `id=1, sleep_enabled=true, updated_at=<aikaleima>`

- [ ] **Step 3: Tarkista RLS anon-lukuoikeus**

Aja SQL Editorissa:
```sql
SET ROLE anon;
SELECT * FROM modem_settings;
RESET ROLE;
```
Odotettu tulos: palauttaa rivin (ei "permission denied")

- [ ] **Step 4: Commit**

```bash
git commit --allow-empty -m "feat(supabase): add modem_settings table with RLS"
```

---

## Task 2: Toggle-kortti index.html:ään (HTML + CSS)

**Files:**
- Modify: `susieq-remote/index.html` (CSS ~rivi 513, HTML ~rivi 1929)

**Interfaces:**
- Produces: `#modem-sleep-toggle`, `#modem-sleep-state`, `#modem-sleep-updated` DOM-elementit, joita Task 3 käyttää

- [ ] **Step 1: Lisää CSS Ohjauspaneeli-lohkoon**

Etsi tiedostosta kommentti `/* Ohjauspaneeli (CoLight-kytkinpaneeli) */` (~rivi 513). Lisää CSS-lohkon loppuun (ennen seuraavaa kommenttia):

```css
/* Modeemin lepotila -toggle */
.modem-sleep-card{
  display:flex; align-items:center; gap:16px;
  background:linear-gradient(180deg,rgba(26,22,19,0.9),rgba(15,12,10,0.9));
  border:1px solid var(--border-hi); border-radius:6px;
  padding:14px 18px; margin-bottom:18px;
}
.modem-sleep-label{
  font-family:var(--ff-mono); font-size:11px;
  letter-spacing:0.1em; text-transform:uppercase;
  color:var(--parchment-dim); flex:1;
}
.modem-sleep-toggle{
  font-family:var(--ff-mono); font-size:11px;
  letter-spacing:0.12em; text-transform:uppercase;
  border:1px solid var(--border-hi); border-radius:4px;
  background:transparent; color:var(--muted);
  padding:6px 14px; cursor:pointer; transition:all .15s;
}
.modem-sleep-toggle.on{ border-color:var(--brass); color:var(--brass) }
.modem-sleep-toggle:disabled{ opacity:0.4; cursor:default }
.modem-sleep-updated{
  font-family:var(--ff-mono); font-size:10px;
  color:var(--muted); white-space:nowrap;
}
```

- [ ] **Step 2: Lisää HTML Ohjauspaneeli-diviin**

Etsi tiedostosta:
```html
  <!-- Ohjauspaneeli (CoLight-kytkinpaneeli) -->
  <div class="dash-panel" data-panel="paneeli">
    <div class="switch-banner" id="switch-banner">Ei yhteyttä paneeliin</div>
```

Korvaa:
```html
  <!-- Ohjauspaneeli (CoLight-kytkinpaneeli) -->
  <div class="dash-panel" data-panel="paneeli">
    <div class="modem-sleep-card">
      <div class="modem-sleep-label">Modeemin lepotila 22–06</div>
      <button class="modem-sleep-toggle" id="modem-sleep-toggle" title="Kirjaudu muuttaaksesi">
        <span id="modem-sleep-state">KÄYTÖSSÄ</span>
      </button>
      <div class="modem-sleep-updated" id="modem-sleep-updated"></div>
    </div>
    <div class="switch-banner" id="switch-banner">Ei yhteyttä paneeliin</div>
```

- [ ] **Step 3: Tarkista visuaalisesti**

Avaa `index.html` selaimessa (tai dev-serverillä), navigoi Ohjauspaneeli-välilehdelle. Pitäisi näkyä kortti "MODEEMIN LEPOTILA 22–06" + nappi "KÄYTÖSSÄ" ennen switch-banneria.

- [ ] **Step 4: Commit**

```bash
cd /Users/jussinippala/Claude/projects/susieq-remote
git add index.html
git commit -m "feat(ui): add modem sleep toggle card to control panel"
```

---

## Task 3: app.js — loadModemSettings + toggleModemSleep

**Files:**
- Modify: `susieq-remote/app.js`

**Interfaces:**
- Consumes: `#modem-sleep-toggle`, `#modem-sleep-state`, `#modem-sleep-updated` (Task 2), `modem_settings`-taulukko (Task 1), `window.sbSession` (olemassa oleva auth)
- Consumes: `window.initSwitchPanel` — lisätään kutsu funktion sisälle (rivi ~645)

- [ ] **Step 1: Lisää modem-sleep-lohko app.js:ään**

Etsi tiedostosta kommentti `// ── Puutelista` (~rivi 724). Lisää seuraava lohko välittömästi ennen sitä:

```js
// ── Modeemin lepotila ─────────────────────────────────────────────────────
let _modemSleepEnabled = true;

function _fmtModemTs(iso) {
  return new Date(iso).toLocaleString('fi-FI', {
    day: 'numeric', month: 'numeric',
    hour: '2-digit', minute: '2-digit',
    timeZone: 'Europe/Helsinki'
  });
}

function _renderModemSleep(enabled, updatedAt) {
  const toggle  = document.getElementById('modem-sleep-toggle');
  const stateEl = document.getElementById('modem-sleep-state');
  const updEl   = document.getElementById('modem-sleep-updated');
  if (!toggle) return;
  _modemSleepEnabled = enabled;
  toggle.classList.toggle('on', enabled);
  toggle.disabled = !window.sbSession?.user;
  stateEl.textContent = enabled ? 'KÄYTÖSSÄ' : 'POIS';
  if (updEl && updatedAt) updEl.textContent = 'Muutettu ' + _fmtModemTs(updatedAt);
}

async function loadModemSettings() {
  const { data, error } = await sb
    .from('modem_settings')
    .select('sleep_enabled, updated_at')
    .eq('id', 1)
    .maybeSingle();
  if (error || !data) return;
  _renderModemSleep(data.sleep_enabled, data.updated_at);
}

async function toggleModemSleep() {
  if (!window.sbSession?.user) return;
  const newVal = !_modemSleepEnabled;
  const now = new Date().toISOString();
  const { error } = await sb
    .from('modem_settings')
    .update({ sleep_enabled: newVal, updated_at: now })
    .eq('id', 1);
  if (error) return;
  _renderModemSleep(newVal, now);
}
```

- [ ] **Step 2: Lisää `loadModemSettings()`-kutsu `initSwitchPanel`:iin**

Etsi tiedostosta:
```js
window.initSwitchPanel = function initSwitchPanel() {
  if (_switchPanelReady) return;
  _switchPanelReady = true;

  fetchSwitchState();
```

Korvaa:
```js
window.initSwitchPanel = function initSwitchPanel() {
  if (_switchPanelReady) return;
  _switchPanelReady = true;

  loadModemSettings();
  fetchSwitchState();
```

- [ ] **Step 3: Lisää click-handler `initSwitchPanel`:iin**

Etsi tiedostosta (pian `initSwitchPanel`:n sisällä, switch-gridin click-handlerin jälkeen):
```js
  document.getElementById('switch-grid')?.addEventListener('click', e => {
```

Lisää välittömästi ennen tätä riviä:
```js
  document.getElementById('modem-sleep-toggle')?.addEventListener('click', toggleModemSleep);

```

- [ ] **Step 4: Testaa selaimessa**

1. Avaa Ohjauspaneeli-välilehti ilman kirjautumista → nappi näkyy disabled (opacity 0.4)
2. Kirjaudu sisään → navigoi takaisin Ohjauspaneeli-välilehdelle → nappi aktiivinen
3. Klikkaa nappia → tila vaihtuu KÄYTÖSSÄ ↔ POIS, brass-väri vaihtuu
4. Päivitä sivu → tila pysyy samana (haetaan Supabasesta)
5. Tarkista Supabase Dashboardista: `SELECT sleep_enabled, updated_at FROM modem_settings WHERE id=1;` — arvon pitäisi muuttua

- [ ] **Step 5: Commit**

```bash
git add app.js
git commit -m "feat(app): add modem sleep toggle — load/render/toggle from Supabase"
```

---

## Task 4: susieq-sleep.sh — Supabase-tarkistus + deploy

**Files:**
- Modify: `susieq_glxe300/susieq-sleep.sh`
- Deploy: kopioi modeemille GoodCloud RTTY:llä tai scp:llä

**Interfaces:**
- Consumes: `modem_settings`-taulukko (Task 1), `/etc/susieq.env` (`SUPABASE_URL`, `SUPABASE_SERVICE_KEY`)

- [ ] **Step 1: Päivitä susieq-sleep.sh**

Avaa `/Users/jussinippala/Claude/projects/SusieQ/susieq_glxe300/susieq-sleep.sh`.

Etsi tiedostosta:
```sh
logger -t susieq-sleep "Vene kotisatamassa (tila: ${STATE:-home}) — 4G lepotilaan"
gl_modem AT AT+CFUN=4 && touch /tmp/susieq_slept
```

Korvaa:
```sh
# Tarkista Supabase-asetus (fallback: nukutaan jos ei vastausta)
SLEEP_ENABLED=$(curl -s --connect-timeout 5 --max-time 10 \
  "${SUPABASE_URL}/rest/v1/modem_settings?id=eq.1&select=sleep_enabled" \
  -H "apikey: ${SUPABASE_SERVICE_KEY}" | \
  python3 -c "import sys,json; d=json.load(sys.stdin); print(d[0]['sleep_enabled'])" 2>/dev/null)
if [ "$SLEEP_ENABLED" = "False" ]; then
    logger -t susieq-sleep "Ohitettu: lepotila poistettu susieq.net-asetuksista"
    exit 0
fi

logger -t susieq-sleep "Vene kotisatamassa (tila: ${STATE:-home}) — 4G lepotilaan"
gl_modem AT AT+CFUN=4 && touch /tmp/susieq_slept
```

- [ ] **Step 2: Tarkista syntaksi paikallisesti**

```bash
sh -n susieq_glxe300/susieq-sleep.sh && echo "syntax OK"
```
Odotettu: `syntax OK`

- [ ] **Step 3: Commit paikallinen muutos**

```bash
git add susieq_glxe300/susieq-sleep.sh
git commit -m "feat(modem): query Supabase sleep_enabled before AT+CFUN=4"
```

- [ ] **Step 4: Deploy modeemille**

Jos SusieQ-Net käytössä:
```bash
scp susieq_glxe300/susieq-sleep.sh root@192.168.8.1:/usr/bin/susieq-sleep.sh
```

Jos etänä (GoodCloud RTTY), kirjaudu terminaaliin ja aja:
```sh
cat > /usr/bin/susieq-sleep.sh << 'EOF'
#!/bin/sh
STATE=$(cat /tmp/susieq_state 2>/dev/null)
if [ "$STATE" = "away" ]; then
    logger -t susieq-sleep "Ohitettu: vene poissa kotisatamasta — 4G pysyy paalla"
    exit 0
fi
if [ -f /etc/susieq_no_sleep ]; then
    logger -t susieq-sleep "Ohitettu: manuaalinen yliajo — 4G pysyy paalla"
    rm -f /etc/susieq_no_sleep
    exit 0
fi
SLEEP_ENABLED=$(curl -s --connect-timeout 5 --max-time 10 \
  "${SUPABASE_URL}/rest/v1/modem_settings?id=eq.1&select=sleep_enabled" \
  -H "apikey: ${SUPABASE_SERVICE_KEY}" | \
  python3 -c "import sys,json; d=json.load(sys.stdin); print(d[0]['sleep_enabled'])" 2>/dev/null)
if [ "$SLEEP_ENABLED" = "False" ]; then
    logger -t susieq-sleep "Ohitettu: lepotila poistettu susieq.net-asetuksista"
    exit 0
fi
logger -t susieq-sleep "Vene kotisatamassa (tila: ${STATE:-home}) — 4G lepotilaan"
gl_modem AT AT+CFUN=4 && touch /tmp/susieq_slept
EOF
sh -n /usr/bin/susieq-sleep.sh && echo "OK"
```

- [ ] **Step 5: Testaa modeemilla**

SSH:lla modeemille, aseta `sleep_enabled = false` Supabasessa, sitten aja:
```sh
. /etc/susieq.env
/usr/bin/susieq-sleep.sh
logread | grep susieq-sleep | tail -3
```
Odotettu logi: `Ohitettu: lepotila poistettu susieq.net-asetuksista`

Aseta `sleep_enabled = true` takaisin, aja uudelleen:
```sh
/usr/bin/susieq-sleep.sh
logread | grep susieq-sleep | tail -3
```
Odotettu logi: `Vene kotisatamassa … — 4G lepotilaan` (ja `AT+CFUN=4` ajaa)

**Huom:** Viimeinen testi oikeasti sammuttaa 4G:n — aja vain jos haluat testata täyden ketjun, muuten testaa susieq.net-togglella.
