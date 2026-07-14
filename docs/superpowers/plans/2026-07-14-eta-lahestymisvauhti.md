# ETA lähestymisvauhdista — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Karttaplotterin reittipaneelin ETA lasketaan todellisesta lähestymisvauhdista (jäljellä olevan matkan lyheneminen 6 min ikkunassa) pelkän SOG:n sijaan, ja paneeliin lisätään hetkellinen VMC-näyttö.

**Architecture:** Kaikki muutokset yhteen tiedostoon `susieq_glxe300/chart.html` (selain-JS). `pollGPS()` kerää 2 s välein näytteitä puskuriin; `updateRoutePanel()` laskee ETA:t uusilla puhtailla apufunktioilla. Palvelimeen ei kosketa. `index.html` on symlink `chart.html`:ään — vain `chart.html` muokataan.

**Tech Stack:** Vanilla JS, Leaflet, GL-XE300 / OpenWrt (deploy: scp → `/usr/share/susieq-chart/index.html`)

**Spec:** `docs/superpowers/specs/2026-07-14-eta-lahestymisvauhti-design.md`

## Global Constraints

- Vain `susieq_glxe300/chart.html` muuttuu — ei palvelinmuutoksia
- UI-tekstit suomeksi, tumma GitHub-teema (olemassa olevat CSS-muuttujat/värit)
- GPS-pollaus on 2 s välein (`setInterval(pollGPS, 2000)`) — älä muuta
- Vakiot: puskuri-ikkuna 6 min, minimi-datajakso 60 s (≈ 30 näytettä), minimivauhti 0,2 kn
- Ei testiframeworkia — laskentafunktiot verifioidaan selainkonsolissa täsmällisillä syötteillä
- **Committeihin kysytään aina lupa käyttäjältä ennen `git commit` -ajoa**

---

## Tiedostokartta

| Tiedosto | Muutos |
|---|---|
| `susieq_glxe300/chart.html` | Uudet tilamuuttujat + laskentafunktiot, datankeruu `pollGPS()`:ään, puskurinollaukset, `updateRoutePanel()`-ETA-logiikka, VMC-rivi (HTML+CSS) |

Rivinumerot alla viittaavat tiedoston nykytilaan (commit 58fbeb0). Käytä aina
tekstihakua (annettu vanha koodi) äläkä sokeasti rivinumeroa — ne siirtyvät
tehtävien edetessä.

---

## Task 1: Tilamuuttujat ja laskentafunktiot

**Files:**
- Modify: `susieq_glxe300/chart.html` (route-tilamuuttujat ~rivi 777, funktiot `calcRemainingRoute()`:n perään ~rivi 794)

**Interfaces:**
- Consumes: olemassa olevat `bearing(lat1,lon1,lat2,lon2)` (deg), `distNm(...)` (nm), globaalit `route`, `routeActive`, `currentWpIdx`, `_lastSog`
- Produces (Task 2 ja 3 käyttävät näitä täsmälleen näillä nimillä):
  - `recordProgress(remNm: number): void`
  - `resetProgress(): void`
  - `closingSpeedKn(): number|null`
  - `activeLegSpeedKn(): number|null`
  - `tripAvgSogKn(): number|null`
  - `instantVmcKn(lat: number, lon: number): number|null`
  - `formatEtaMin(m: number): string`
  - muuttujat `_lastCog`, `_progressBuf`, `_tripSogSum`, `_tripSogCount`

- [ ] **Step 1: Lisää tilamuuttujat ja vakiot**

  Etsi rivi:

  ```js
  let _lastSog = 0;         // viimeisin SOG (knots) routePanel-laskentaa varten
  ```

  Lisää heti sen perään:

  ```js
  let _lastCog = 0;         // viimeisin COG (deg) VMC-laskentaa varten
  let _progressBuf = [];    // {t: ms, rem: nm} — jäljellä oleva reittimatka
  let _tripSogSum = 0;      // SOG-näytteiden summa reitin aloituksesta
  let _tripSogCount = 0;    // SOG-näytteiden määrä reitin aloituksesta

  const PROGRESS_WINDOW_MS   = 6 * 60 * 1000; // lähestymisvauhdin ikkuna
  const PROGRESS_MIN_SPAN_MS = 60 * 1000;     // minimi-ikkuna ennen tasoitettua ETAa
  const TRIP_MIN_SAMPLES     = 30;            // ~60 s @ 2 s pollaus
  const MIN_SPEED_KN         = 0.2;           // tämän alle ETA = "—"
  ```

  Huom: spec mainitsee muuttujan `_tripStartT` — sitä ei tarvita, koska 60 s
  sääntö toteutuu näytemäärällä (`TRIP_MIN_SAMPLES`).

- [ ] **Step 2: Lisää laskentafunktiot**

  Etsi funktio `calcRemainingRoute()` (päättyy `return nm;` + `}`). Lisää heti
  sen sulkevan aaltosulkeen jälkeen:

  ```js
  function recordProgress(remNm) {
    const now = Date.now();
    _progressBuf.push({ t: now, rem: remNm });
    while (_progressBuf.length && now - _progressBuf[0].t > PROGRESS_WINDOW_MS)
      _progressBuf.shift();
  }

  function resetProgress() {
    _progressBuf = [];
  }

  // Tasoitettu lähestymisvauhti (kn): jäljellä olevan reittimatkan
  // lyheneminen puskurin ikkunassa. null jos dataa alle 60 s.
  function closingSpeedKn() {
    if (_progressBuf.length < 2) return null;
    const first = _progressBuf[0];
    const last  = _progressBuf[_progressBuf.length - 1];
    const spanMs = last.t - first.t;
    if (spanMs < PROGRESS_MIN_SPAN_MS) return null;
    return (first.rem - last.rem) / (spanMs / 3600000);
  }

  // Aktiivisen legin ETA-vauhti: tasoitettu jos saatavilla, muuten
  // SOG-fallback (kuten vanha laskenta). null = näytä "—".
  function activeLegSpeedKn() {
    const cs = closingSpeedKn();
    if (cs === null) return _lastSog > MIN_SPEED_KN ? _lastSog : null;
    return cs >= MIN_SPEED_KN ? cs : null;
  }

  // Matkan keskivauhti (kn) reitin aloituksesta. null jos dataa alle ~60 s.
  function tripAvgSogKn() {
    if (_tripSogCount < TRIP_MIN_SAMPLES) return null;
    return _tripSogSum / _tripSogCount;
  }

  // Hetkellinen VMC (kn) kohti aktiivista reittipistettä.
  // Negatiivinen = etäännytään. null jos ei laskettavissa.
  function instantVmcKn(lat, lon) {
    if (!routeActive || currentWpIdx >= route.length) return null;
    if (_lastSog <= MIN_SPEED_KN) return null;
    const wp = route[currentWpIdx];
    const brg = bearing(lat, lon, wp.lat, wp.lon);
    return _lastSog * Math.cos((brg - _lastCog) * Math.PI / 180);
  }

  function formatEtaMin(m) {
    return m < 60 ? Math.round(m) + ' min' : (m / 60).toFixed(1) + ' h';
  }
  ```

- [ ] **Step 3: Verifioi konsolissa**

  Avaa `chart.html` selaimessa (esim. `python3 susieq-chart-server.py`
  hakemistossa `susieq_glxe300/` ja `http://localhost:8080/`, tai suoraan
  `file://` — kartta saa olla rikki, vain JS-konsoli kiinnostaa).
  DevTools → Console, aja rivi kerrallaan:

  ```js
  // 1) Tasainen lähestyminen: 0.01 nm / 12 s = 3.0 kn
  var t0 = Date.now();
  _progressBuf = [];
  for (let i = 0; i <= 30; i++) _progressBuf.push({ t: t0 - (30 - i) * 12000, rem: 5.0 - i * 0.01 });
  closingSpeedKn()
  // Odotettu: ≈ 3.0 (esim. 2.9999999999999996)

  // 2) Liian lyhyt ikkuna → null
  _progressBuf = [{ t: t0 - 30000, rem: 5.0 }, { t: t0, rem: 4.98 }];
  closingSpeedKn()
  // Odotettu: null

  // 3) Fallback SOG:iin kun puskuri tyhjä
  resetProgress(); _lastSog = 4.5;
  activeLegSpeedKn()
  // Odotettu: 4.5

  // 4) Negatiivinen eteneminen (etäännytään) → null (ETA "—")
  _progressBuf = [];
  for (let i = 0; i <= 30; i++) _progressBuf.push({ t: t0 - (30 - i) * 12000, rem: 5.0 + i * 0.01 });
  activeLegSpeedKn()
  // Odotettu: null

  // 5) Keskivauhti
  _tripSogSum = 0; _tripSogCount = 0; tripAvgSogKn()
  // Odotettu: null
  _tripSogSum = 150; _tripSogCount = 30; tripAvgSogKn()
  // Odotettu: 5

  // 6) VMC: kohde suoraan pohjoisessa, COG pohjoiseen / itään / etelään
  route = [{ lat: 61.6, lon: 23.75 }]; routeActive = true; currentWpIdx = 0;
  _lastSog = 5; _lastCog = 0;
  instantVmcKn(61.5, 23.75)
  // Odotettu: ≈ 5.0
  _lastCog = 90; instantVmcKn(61.5, 23.75)
  // Odotettu: ≈ 0 (itseisarvo < 0.01)
  _lastCog = 180; instantVmcKn(61.5, 23.75)
  // Odotettu: ≈ -5.0
  routeActive = false; instantVmcKn(61.5, 23.75)
  // Odotettu: null

  // 7) ETA-muotoilu
  formatEtaMin(45)   // Odotettu: "45 min"
  formatEtaMin(90)   // Odotettu: "1.5 h"
  ```

  Kaikkien tulosten pitää vastata odotettuja. Lataa sivu lopuksi uudelleen,
  ettei konsolisotku jää tilaan.

- [ ] **Step 4: Pyydä lupa committiin ja committaa**

  ```bash
  git add susieq_glxe300/chart.html
  git commit -m "feat(chart): lähestymisvauhdin ja VMC:n laskentafunktiot"
  ```

---

## Task 2: Datankeruu ja puskurinollaukset

**Files:**
- Modify: `susieq_glxe300/chart.html` (`pollGPS()` ~rivi 1166, `startRoute()` ~rivi 974, `advanceToNextWp()` ~rivi 984, `checkAutoAdvance()` ~rivi 996, `addRouteWp()` ~rivi 927, `removeRouteWp()` ~rivi 934, `clearRoute()` ~rivi 949)

**Interfaces:**
- Consumes (Task 1): `recordProgress(remNm)`, `resetProgress()`, muuttujat `_lastCog`, `_tripSogSum`, `_tripSogCount`
- Produces: `advanceToNextWp(auto?: boolean)` — `true` = automaattiohitus (ei puskurinollausta), ilman argumenttia = manuaali (nollaus). Puskuri täyttyy ajossa `pollGPS()`-kierroksilla.

- [ ] **Step 1: Kerää näytteet `pollGPS()`-funktiossa**

  Etsi `pollGPS()`-funktiosta rivit:

  ```js
      const cog = gps.cog_deg || 0, sog = gps.sog_knots || 0;
      _lastSog = sog;
  ```

  Korvaa ne tällä:

  ```js
      const cog = gps.cog_deg || 0, sog = gps.sog_knots || 0;
      _lastSog = sog;
      _lastCog = cog;
      if (routeActive && currentWpIdx < route.length) {
        const rem = calcRemainingRoute(lat, lon);
        if (rem !== null) recordProgress(rem);
        _tripSogSum += sog;
        _tripSogCount++;
      }
  ```

  Huom: keruu tapahtuu ENNEN `checkAutoAdvance(lat, lon)` -kutsua, joka on
  muutamaa riviä alempana — järjestys on oikein kun korvaus tehdään
  yllä olevaan kohtaan.

- [ ] **Step 2: Nollaa laskurit `startRoute()`-funktiossa**

  Etsi:

  ```js
  function startRoute() {
    if (!route.length) return;
    routeActive = true;
    currentWpIdx = 0;
  ```

  Korvaa tällä:

  ```js
  function startRoute() {
    if (!route.length) return;
    routeActive = true;
    currentWpIdx = 0;
    resetProgress();
    _tripSogSum = 0;
    _tripSogCount = 0;
  ```

- [ ] **Step 3: Lippuparametri `advanceToNextWp()`-funktioon**

  Etsi:

  ```js
  function advanceToNextWp() {
    if (!routeActive) return;
    currentWpIdx++;
  ```

  Korvaa tällä:

  ```js
  function advanceToNextWp(auto) {
    if (!routeActive) return;
    if (auto !== true) resetProgress();
    currentWpIdx++;
  ```

  Paneelin nappi (`onclick="advanceToNextWp()"`) kutsuu ilman argumenttia →
  nollaus. Sitä EI muuteta.

- [ ] **Step 4: Automaattiohitus kutsuu lipun kanssa**

  Etsi `checkAutoAdvance()`-funktiosta:

  ```js
    if (distNm(lat, lon, wp.lat, wp.lon) < advanceRadiusNm) {
      advanceToNextWp();
    }
  ```

  Korvaa tällä:

  ```js
    if (distNm(lat, lon, wp.lat, wp.lon) < advanceRadiusNm) {
      advanceToNextWp(true);
    }
  ```

- [ ] **Step 5: Nollaus reittimuokkauksissa**

  Lisää `resetProgress();` kolmeen funktioon, kuhunkin ensimmäiseksi
  muutosriviksi:

  `addRouteWp()`:
  ```js
  function addRouteWp(lat, lon) {
    resetProgress();
    route.push({ lat, lon });
  ```

  `removeRouteWp()`:
  ```js
  function removeRouteWp(idx) {
    resetProgress();
    route.splice(idx, 1);
  ```

  `clearRoute()` (nollaus vasta confirmin jälkeen):
  ```js
  function clearRoute() {
    if (!confirm('Tyhjennetäänkö reitti?')) return;
    resetProgress();
    route = [];
  ```

- [ ] **Step 6: Verifioi konsolissa**

  Lataa sivu uudelleen selaimessa, avaa Console:

  ```js
  // Lisää reitti ja käynnistä
  route = [{ lat: 61.6, lon: 23.75 }, { lat: 61.7, lon: 23.75 }];
  renderRouteOnMap(); startRoute();
  _progressBuf.length
  // Odotettu: 0 (startRoute nollasi)

  // Odota ~10 s GPS-pollausta. Jos oikeaa GPS-fixiä ei ole (pöytätesti),
  // puskuri jää tyhjäksi — silloin syötä näyte käsin ja testaa vain nollaukset:
  recordProgress(5.0); _progressBuf.length
  // Odotettu: 1

  advanceToNextWp()        // manuaali → nollaa
  _progressBuf.length
  // Odotettu: 0

  recordProgress(5.0); addRouteWp(61.8, 23.75); _progressBuf.length
  // Odotettu: 0

  recordProgress(5.0); removeRouteWp(route.length - 1); _progressBuf.length
  // Odotettu: 0
  ```

  Jos käytössä on GPS-fix (veneellä/simulaattorilla): tarkista että
  `_progressBuf.length` kasvaa ~1/2 s ja `_tripSogCount` samaa tahtia.

  Lataa sivu lopuksi uudelleen.

- [ ] **Step 7: Pyydä lupa committiin ja committaa**

  ```bash
  git add susieq_glxe300/chart.html
  git commit -m "feat(chart): lähestymisvauhtinäytteiden keruu ja puskurinollaukset"
  ```

---

## Task 3: Reittipaneelin ETA-logiikka ja VMC-rivi

**Files:**
- Modify: `susieq_glxe300/chart.html` (CSS ~rivi 350, HTML ~rivi 474, `updateRoutePanel()` ~rivi 796)

**Interfaces:**
- Consumes (Task 1): `activeLegSpeedKn()`, `tripAvgSogKn()`, `instantVmcKn(lat, lon)`, `formatEtaMin(m)`, `MIN_SPEED_KN`, `_lastSog`
- Produces: uusi DOM-elementti `#route-total-vmc`

- [ ] **Step 1: CSS — VMC-rivi samalla tyylillä kuin ETA-rivi**

  Etsi:

  ```css
  #route-total-eta {
    font-size: 10px;
    color: #8b949e;
    margin-top: 2px;
  }
  ```

  Korvaa tällä:

  ```css
  #route-total-eta, #route-total-vmc {
    font-size: 10px;
    color: #8b949e;
    margin-top: 2px;
  }
  ```

- [ ] **Step 2: HTML — VMC-rivi paneeliin**

  Etsi:

  ```html
    <div id="route-total-dist">—</div>
    <div id="route-total-eta">—</div>
  ```

  Korvaa tällä:

  ```html
    <div id="route-total-dist">—</div>
    <div id="route-total-eta">—</div>
    <div id="route-total-vmc">VMC —</div>
  ```

- [ ] **Step 3: Aktiivisen rivin ETA lähestymisvauhdista**

  Etsi `updateRoutePanel()`-funktiosta:

  ```js
      if (_lastSog > 0.2) distTxt += '<br>' + Math.round(d / _lastSog * 60) + ' min';
  ```

  Korvaa tällä:

  ```js
      const legSp = activeLegSpeedKn();
      if (legSp !== null) distTxt += '<br>' + Math.round(d / legSp * 60) + ' min';
  ```

- [ ] **Step 4: Koko reitin ETA kahdessa osassa**

  Etsi `updateRoutePanel()`-funktiosta:

  ```js
  if (routeActive && currentPos) {
    const rem = calcRemainingRoute(currentPos[0], currentPos[1]);
    if (rem !== null) {
      distEl.textContent = rem.toFixed(1) + ' nm';
      if (_lastSog > 0.2) {
        const m = rem / _lastSog * 60;
        etaEl.textContent = 'ETA: ' + (m < 60 ? Math.round(m) + ' min' : (m/60).toFixed(1) + ' h');
      } else {
        etaEl.textContent = '—';
      }
    }
  } else if (route.length > 1) {
  ```

  Korvaa tällä:

  ```js
  if (routeActive && currentPos) {
    const rem = calcRemainingRoute(currentPos[0], currentPos[1]);
    if (rem !== null) {
      distEl.textContent = rem.toFixed(1) + ' nm';
      const awp = route[currentWpIdx];
      const dActive = distNm(currentPos[0], currentPos[1], awp.lat, awp.lon);
      const dRest = rem - dActive;
      const legSp = activeLegSpeedKn();
      let restSp = tripAvgSogKn();
      if (restSp === null || restSp < MIN_SPEED_KN)
        restSp = _lastSog > MIN_SPEED_KN ? _lastSog : null;
      let etaMin = null;
      if (legSp !== null) {
        etaMin = dActive / legSp * 60;
        if (dRest > 0.001)
          etaMin = restSp !== null ? etaMin + dRest / restSp * 60 : null;
      }
      etaEl.textContent = etaMin !== null ? 'ETA: ' + formatEtaMin(etaMin) : '—';
    }
  } else if (route.length > 1) {
  ```

- [ ] **Step 5: VMC-rivin päivitys**

  Etsi `updateRoutePanel()`-funktion loppu:

  ```js
  nextBtn.style.display = routeActive ? 'block' : 'none';
  nextBtn.disabled = !routeActive || currentWpIdx >= route.length - 1;
  updateCurrentLegLine();
  ```

  Korvaa tällä:

  ```js
  const vmcEl = document.getElementById('route-total-vmc');
  const vmc = currentPos ? instantVmcKn(currentPos[0], currentPos[1]) : null;
  vmcEl.textContent = vmc !== null ? 'VMC ' + vmc.toFixed(1) + ' kn' : 'VMC —';

  nextBtn.style.display = routeActive ? 'block' : 'none';
  nextBtn.disabled = !routeActive || currentWpIdx >= route.length - 1;
  updateCurrentLegLine();
  ```

- [ ] **Step 6: Verifioi selaimessa (pöytätesti)**

  Lataa sivu uudelleen, avaa Console. Simuloi tilanne käsin:

  ```js
  // Reitti: kaksi legiä, vene 1.0 nm ensimmäisestä pisteestä etelään
  route = [{ lat: 61.6167, lon: 23.75 }, { lat: 61.7167, lon: 23.75 }];
  currentPos = [61.6, 23.75];
  renderRouteOnMap(); startRoute();

  // Syötä puskuriin tasainen 3 kn lähestyminen (6 min ikkuna)
  var t0 = Date.now();
  _progressBuf = [];
  for (let i = 0; i <= 30; i++) _progressBuf.push({ t: t0 - (30 - i) * 12000, rem: 7.0 - i * 0.01 });

  // Keskivauhti 5 kn
  _tripSogSum = 150; _tripSogCount = 30;

  // VMC-syötteet: SOG 5 kn suoraan kohti pistettä (pohjoiseen)
  _lastSog = 5; _lastCog = 0;

  updateRoutePanel();
  document.getElementById('route-total-eta').textContent
  // Odotettu: "ETA: 1.5 h"
  //   (aktiivinen legi 1.0 nm / 3 kn = 20 min; loppulegi 6.0 nm / 5 kn = 72 min;
  //    yhteensä 92 min → formatEtaMin → "1.5 h")
  document.getElementById('route-total-vmc').textContent
  // Odotettu: "VMC 5.0 kn"

  // Aktiivisen WP-rivin pitää näyttää "... 20 min" (1.0 nm / 3.0 kn)
  document.querySelector('.route-wp-row.active .route-wp-dist').innerHTML
  // Odotettu: sisältää "20 min"

  // Hidas: kaikki alle kynnyksen → viivat
  _lastSog = 0.1; resetProgress(); _tripSogSum = 0; _tripSogCount = 0;
  updateRoutePanel();
  document.getElementById('route-total-eta').textContent   // Odotettu: "—"
  document.getElementById('route-total-vmc').textContent   // Odotettu: "VMC —"
  ```

  Huom: leg-pituudet yllä: 61.6→61.6167 on 1.002 nm, 61.6167→61.7167 on
  6.001 nm (1° lat = 60 nm). Pieni poikkeama odotuksissa (±1 min) on ok.

  Tarkista lisäksi ettei konsolissa ole `ReferenceError`/`TypeError`-virheitä
  normaalin pollauksen pyöriessä. Lataa sivu lopuksi uudelleen.

- [ ] **Step 7: Pyydä lupa committiin ja committaa**

  ```bash
  git add susieq_glxe300/chart.html
  git commit -m "feat(chart): ETA lähestymisvauhdista ja matkan keskivauhdista + VMC-rivi"
  ```

---

## Task 4: Deploy modeemille ja loppuverifiointi

**Files:**
- Deploy: `susieq_glxe300/chart.html` → `root@192.168.8.1:/usr/share/susieq-chart/index.html`

**Interfaces:**
- Consumes: valmis `chart.html` (Task 1–3)
- Produces: päivitetty plotteri osoitteessa `http://192.168.8.1:8080/`

- [ ] **Step 1: Varmista verkko**

  Kysy käyttäjältä: onko kone SusieQ-Net-verkossa (192.168.8.x)? Jos ei,
  deploy tehdään myöhemmin veneellä (tai GoodCloud RTTY:n kautta, ks.
  boat-ops-skill) — merkitse task odottavaksi ja lopeta tähän.

- [ ] **Step 2: Kopioi tiedosto**

  ```bash
  scp susieq_glxe300/chart.html root@192.168.8.1:/usr/share/susieq-chart/index.html
  ```

  Odotettu: siirto onnistuu ilman virheitä.

- [ ] **Step 3: Verifioi selaimessa**

  Avaa `http://192.168.8.1:8080/` ja tee kova uudelleenlataus
  (Cmd-Shift-R). Tarkista:

  1. Sivu latautuu, ei konsolivirheitä
  2. Lisää 2 reittipistettä, paina ▶ Aloita → paneelissa näkyy
     "VMC —" -rivi kokonaismatkan alla
  3. Ensimmäiset ~60 s: ETA näkyy (SOG-fallback); sen jälkeen ETA
     siirtyy tasoitettuun lähestymisvauhtiin
  4. Jos vene on paikallaan (SOG ≈ 0): ETA "—" ja "VMC —" — oikein

- [ ] **Step 4: Käyttötesti purjehduksella (kirjataan, ei blokkaa)**

  Seuraavalla purjehduksella, kryssilegillä:
  - ETA asettuu realistiseksi ~2–6 min sisällä ja pysyy vakaana halssien yli
  - VMC-luku elää halssin mukana (pienenee kun ajetaan sivuun, negatiivinen
    jos poispäin)
  - Automaattiohituksen WP-vaihto ei aiheuta ETA-hyppyä

---

## Spec coverage -tarkistus

| Spec-vaatimus | Task |
|---|---|
| Näytepuskuri 6 min, näyte/pollaus | Task 1 Step 1–2, Task 2 Step 1 |
| Tasoitettu vauhti kokonaisreittimatkasta | Task 1 Step 2 (`closingSpeedKn`), Task 2 Step 1 (`calcRemainingRoute` → `recordProgress`) |
| Nollaus: startRoute, add/remove/clear, manuaaliohitus | Task 2 Step 2–5 |
| Automaattiohitus EI nollaa | Task 2 Step 3–4 (lippuparametri) |
| Fallback: < 60 s → SOG; < 0,2 kn → "—" | Task 1 Step 2 (`activeLegSpeedKn`) |
| Aktiivisen rivin ETA tasoitetulla vauhdilla | Task 3 Step 3 |
| Koko reitin ETA: aktiivinen legi + loppulegit keskivauhdilla | Task 3 Step 4 |
| Matkan keskivauhti + fallbackit | Task 1 Step 2 (`tripAvgSogKn`), Task 3 Step 4 |
| VMC-rivi, negatiivinen sallittu, "—"-säännöt | Task 1 Step 2 (`instantVmcKn`), Task 3 Step 1–2, 5 |
| Esitysmuoto min/h ennallaan | Task 1 Step 2 (`formatEtaMin`) |
| Ei palvelinmuutoksia | — (vain chart.html) |
| Konsolitestit + käyttötesti veneellä | Task 1 Step 3, Task 2 Step 6, Task 3 Step 6, Task 4 Step 4 |
