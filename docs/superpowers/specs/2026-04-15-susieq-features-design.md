# SusieQ — Uudet ominaisuudet: design-dokumentti

**Päivämäärä:** 2026-04-15  
**Konteksti:** SusieQ C&C 28 -purjeveneen IoT-järjestelmä. Kaikki data tulee jo olemassa olevista sensoreista WebSocketin kautta. Lisätään 7 uutta ominaisuutta: navigointiturvallisuus, säätrendi, huomiset auringonlasku-laskin, rum o'clock, yöteema, nopeusennätykset ja matkatilastot.

---

## 1. Arkkitehtuuri

### Muutoslaajuus

| Tiedosto | Muutos |
|---|---|
| `susieq_dashboard/data/index.html` | +~430 riviä JS + CSS (kaikki UI-ominaisuudet) |
| `susieq_dashboard/src/main.cpp` | +~50 riviä (uusi `/records` REST-endpoint) |
| `susieq_dashboard/include/config.h` | Ei muutoksia |
| Muut firmware-tiedostot | Ei muutoksia |

### Data-arkkitehtuuri

```
WebSocket (/ws) — jo olemassa:
  d.gps.lat, d.gps.lon, d.gps.sog_knots, d.gps.fix
  d.weather.pressure_hpa, d.weather.temp_c
  d.rum.liters

Uusi REST:
  GET  /records  → { speed_knots: N, speed_date: "...", season_nm: N, trips: [...] }
  POST /records  → sama rakenne, kirjoitetaan /records.json LittleFS:ään
```

Kaikki laskenta tapahtuu **selaimessa** (JS). ESP32 toimii vain tallennuspalvelimena `/records`-tiedostolle.

---

## 2. Ominaisuudet

### 2.1 Ankkurialarmi

**Toiminta:**
- Käyttäjä painaa "⚓ Lasketaan ankkuri" → tallennetaan `anchorLat`, `anchorLon` (GPS:stä)
- Joka GPS-päivityksellä lasketaan etäisyys ankkuripisteeseen (Haversine)
- Jos etäisyys > kynnys (oletus 50 m, säädettävä 20–150 m liukusäätimellä):
  - Dashboard-fontti vilkkuu punaisena
  - Web Audio API -piippaus (toimii HTTP:llä ilman HTTPS-vaatimusta)
  - Selain pysyy hereillä (NoSleep.js tai wake lock API)
- Toinen nappi "⚓ Ankkuri nostettu" → poistaa alarmin

**UI-paikka:** GPS-kortti. Pieni ankkurikuvake + etäisyys metreinä näkyy kun alarmi aktiivinen.

**Rajoitukset:** GPS-fix vaaditaan ankkurin laskemiseen. Ilman fixiä nappi on disabloitu.

---

### 2.2 Painetrendi ja barogrammi

**Toiminta:**
- Kerätään painelukema kerran minuutissa (otetaan `d.weather.pressure_hpa` WebSocket-päivityksestä, tallennetaan minuutin välein → max 360 pistettä = 6h)
- Piirretään SVG-käyrä sääkortissa (100 × 40 px minimalistinen sparkline)
- Trendianalyysi: verrataan viimeistä 30 min keskiarvoa 3 h takaiseen → lasketaan muutos
  - `> +2 hPa/3h` → "↑ Nouseva — sää paranee"
  - `< −2 hPa/3h` → "↓ Laskeva — sää heikkenee"
  - Muuten → "→ Vakaa"
- Väri: vihreä (nouseva), keltainen (vakaa), punainen (laskeva)

**UI-paikka:** Sää-kortti, painearvon alle. Sparkline + trenditeksti.

---

### 2.3 Auringonlasku-laskin

**Toiminta:**
- Lasketaan auringonnousu ja -lasku GPS-koordinaateista + päivämäärästä
- Algoritmi: NOAA:n yksinkertaistettu aurinkolaskenta (vakiintuneet kaavat, ~40 riviä JS)
- Ei ulkoista API:a — toimii offline
- GPS-fix tarvitaan koordinaatteihin; ilman fixiä ei lasketa
- Päivitetään kerran minuutissa (koordinaatit voivat muuttua)

**Tulostetaan:**
- Auringonlasku klo `HH:MM` (paikallinen aika)
- Countdown: "X h Y min auringonlaskuun"
- Auringonnousu klo `HH:MM`

**UI-paikka:** GPS-kortissa kellotietojen alapuolella, tai erillinen pieni "Päivä"-rivi navigointiosion alla.

---

### 2.4 Rum o'clock

**Toiminta:**
- Perustuu auringonlasku-laskimeen (2.3)
- Auringonlaskuhetkellä (±2 min):
  - Dashboard-tausta välkähtää lämpimäksi oranssiksi 3 sekunnin ajan
  - Web Audio API toistaa simppelin fanfaari-äänen (syntetisoitu, ei tiedostoa)
  - Rum-kortissa näkyy teksti "🌅 Rum o'clock!" parin minuutin ajan
- Rum-kortissa näkyy jatkuvasti: "Annoksia jäljellä: ~N" (rum.liters / 0.06 L per annos)
- Countdown: "Rum o'clock Xh Ymin päästä" kun < 3h jäljellä

**Rajoitus:** Toimii vain kun GPS-fix on ja rum-sensori antaa lukemat.

---

### 2.5 Yöteema

**Toiminta:**
- **Automaattinen:** aktivoituu auringonlaskun jälkeen (käyttää 2.3:n laskemaa aikaa), poistuu auringonnousun jälkeen
- **Manuaalinen toggle:** 🌙/☀️ -nappi oikeassa yläkulmassa (ohittaa automaation)
- Teema tallennetaan `localStorage`:aan (muistaa valitun manuaalisen tilan)

**CSS-muutos:**
- Päätausta: `#0d1117` → `#000000` (täysin musta)
- Teksti ja ikonit: nykyinen siniharmaa-palette → punainen palette
  - `--color-accent`: `#58a6ff` → `#cc2200`
  - `--color-text`: `#e6edf3` → `#ff4444`
  - `--color-muted`: `#8b949e` → `#882200`
- Kortit: `#161b22` → `#0a0000`
- Live-dot: vihreä pysyy vihreänä (status-info ei muutu)
- **Huom implementoijalle:** Tarkista CSS-muuttujien tarkat nimet `index.html`:n `:root`-lohkosta ennen kuin vaihdat arvoja — yllä olevat ovat tämän hetken arvoja, saattavat muuttua.

---

### 2.6 Nopeusennätykset

**Seurattavat arvot:**
- Päivän huippunopeus (solmua) — nollaantuu päivän vaihtuessa
- Kauden paras huippunopeus + päivämäärä

**Data-flow:**
1. Joka WebSocket-päivityksellä: jos `d.gps.sog_knots` > sessio-max → päivitetään
2. Kun sessio-max kasvaa: `POST /records` ESP32:lle (throttlattu, max 1 req/min)
3. Sivun latautuessa: `GET /records` → haetaan tallennettu kauden ennätys

**ESP32-tallennusrakenne (`/records.json`):**
```json
{
  "best_knots": 7.2,
  "best_date": "2026-07-14",
  "season_nm": 142.3
}
```

**UI-paikka:** GPS-kortissa SOG:n alapuolella. Kaksi riviä: "Tänään max: X.X kn" ja "Kauden ennätys: X.X kn (PP.KK)".

---

### 2.7 Matkatilastot (ilman reittiä)

**Seurattavat arvot per matka:**
- Matkan kesto (hh:mm)
- Matkan pituus (nm) — integroitu SOG × aikaintervallin avulla
- Keskinopeus (nm / aika)
- Matkan huippunopeus

**Data-flow:**
1. "▶ Aloita matka" -nappi → tallennetaan `tripStart` timestamp, nollataan laskerit
2. Joka WebSocket-päivityksellä: `trip_nm += sog_knots × (dt / 3600)` (dt sekunteina)
3. "⏹ Lopeta matka" -nappi → POST `/records` (lisätään `trips`-listaan, max 20 viimeistä)
4. Matka-tilastot näytetään live matkalla; historia haetaan GET `/records` sivun latautuessa

**ESP32-tallennusrakenne (lisätään `/records.json`:iin):**
```json
{
  "best_knots": 7.2,
  "best_date": "2026-07-14",
  "season_nm": 142.3,
  "trips": [
    { "date": "2026-07-14", "duration_min": 187, "nm": 18.4, "avg_kn": 5.9, "max_kn": 7.2 }
  ]
}
```

**UI-paikka:** GPS-kortin laajennusosa — "▶ Aloita matka" -nappi GPS-kortissa, joka avaa korttin alle liukuvan laajennuksen. Laajennus sisältää: live-tilastot matkalla (kesto, nm, keskinopeus, huippu) + 3 viimeistä matkaa historialistana. Sulkeutuu "⏹ Lopeta matka" -napin jälkeen.

---

## 3. Firmware-muutos: `/records`-endpoint

**Tiedosto:** `susieq_dashboard/src/main.cpp`

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
// Body voi tulla useassa TCP-paketissa → käytetään index:iä
server.on("/records", HTTP_POST,
    [](AsyncWebServerRequest* req) {
        req->send(200, "text/plain", "OK");
    },
    nullptr,
    [](AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t index, size_t total) {
        File f = LittleFS.open("/records.json", index == 0 ? "w" : "a");
        if (f) { f.write(data, len); f.close(); }
    }
);
```

Suojaus: hx711_mutex ei tarvita (LittleFS-kirjoitus ei kilpaile HX711:n kanssa).

---

## 4. Implementointijärjestys

1. Firmware: `/records` GET + POST endpoint → `main.cpp`
2. JS: Auringonlasku-laskin (pohja rum o'clockille ja yöteemalle)
3. JS+CSS: Yöteema
4. JS: Painetrendi + sparkline-graafi
5. JS: Ankkurialarmi
6. JS: Rum o'clock (käyttää auringonlasku-laskinta)
7. JS: Nopeusennätykset (GET /records sivun latautuessa)
8. JS: Matkatilastot + matka-kortti

---

## 5. Verifiointi

- `pio run -t upload` (firmware) + `pio run -t uploadfs` (HTML)
- Avaa dashboard → tarkista painetrendi näkyy heti
- Aseta puhelin GPS-paikkatietojen lähettämiseen → ankkurialarmi testiajo
- Simuloi auringonlasku: muuta laskimen testisyötettä → rum o'clock + yöteema aktivoituvat
- `curl -X GET http://192.168.4.1/records` → `{}`
- `curl -X POST http://192.168.4.1/records -d '{"best_knots":6.1,"best_date":"2026-04-15","season_nm":0,"trips":[]}'`
- `curl -X GET http://192.168.4.1/records` → palauttaa tallennetun JSON:n
- Lataa sivu uudelleen → ennätys näkyy haettuna ESP32:lta
