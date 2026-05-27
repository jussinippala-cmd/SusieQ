# Design: Monireittipistejärjestelmä karttaplotteriin

**Päivämäärä:** 2026-05-27  
**Tila:** Hyväksytty  
**Tiedostot:** `susieq_glxe300/chart.html`, `susieq_glxe300/susieq-chart-server.py`

---

## Tausta

Karttaplotterissa on tällä hetkellä yksittäinen reittipiste (WP): käyttäjä napauttaa karttaa, piste asetetaan, info-paneeli näyttää suunnan/etäisyyden/ETA:n. Tarve on laajentaa tämä monireittipistejärjestelmäksi jotta kattavan reitin suunnittelu on mahdollista.

Tulevaisuudessa reitti pitää voida lähettää Raymarine ST1000+ -autopilotille SeaTalk1-väylän kautta (erillinen spare ESP32). Tämä integraatio toteutetaan myöhemmin kesällä 2026 — arkkitehtuuri suunnitellaan niin, ettei karttaplotteriin tarvita muutoksia silloin.

---

## Arkkitehtuuripäätös

**Vaihtoehto valittu:** Korvataan nykyinen single-WP-järjestelmä kokonaan reittijärjestelmällä joka tukee 1…N pistettä. Yksi piste käyttäytyy identtisesti nykytoiminnon kanssa. Ei kahta rinnakkaista järjestelmää.

---

## chart.html — muutokset

### Poistetaan

| Muuttuja / funktio | Kuvaus |
|---|---|
| `wpMarker`, `wpLine`, `wpLatLng` | Yksittäisen WP:n tila |
| `wpMode` | Napautustila yksittäiselle WP:lle |
| `toggleWpMode()`, `setWaypoint()`, `clearWaypoint()` | WP-hallintafunktiot |
| `updateWpLine()`, `updateWpInfo()` | WP-päivitysfunktiot |
| `#wp-info` info-paneelin lohko | Nykyinen WP-tietonäyttö |
| `#btn-wp`, `#btn-clear-wp-wrap` asetuspaneelissa | Vanhat napit |

### Lisätään — tila

```js
let route = [];          // [{lat, lon}, ...]
let currentWpIdx = 0;    // aktiivisen WP:n indeksi
let wpAddMode = false;   // napautustila uuden pisteen lisäämiseen
let advanceRadiusNm = 0.10;  // automaattiohituksen säde
let autoAdvance = true;  // automaattiohitus päällä/pois
```

### Lisätään — funktiot

| Funktio | Kuvaus |
|---|---|
| `addRouteWp(lat, lon)` | Lisää pisteen `route`-taulukkoon, päivittää kartan |
| `removeRouteWp(idx)` | Poistaa yksittäisen pisteen indeksillä, järjestää markkerit uudelleen. Jos `idx < currentWpIdx`: decrementtaa currentWpIdx. Jos `idx == currentWpIdx`: siirtyy seuraavaan (tai merkitsee reitin valmiiksi jos viimeinen). Jos `idx > currentWpIdx`: ei muutosta indeksiin. |
| `clearRoute()` | Tyhjentää koko reitin, poistaa kaikki markkerit ja linjan |
| `startRoute()` | Asettaa `currentWpIdx = 0`, aktivoi navigoinnin |
| `advanceToNextWp()` | Siirtyy seuraavaan pisteeseen tai merkitsee reitin valmiiksi |
| `checkAutoAdvance(lat, lon)` | Kutsutaan GPS-pollissa: jos etäisyys aktiiviseen WP < `advanceRadiusNm`, kutsuu `advanceToNextWp()` |
| `updateRoutePanel()` | Päivittää reittilistan, "Seuraava"-napin tilan ja kokonaistiedot |
| `calcRemainingRoute(lat, lon)` | Palauttaa `{nm, etaMin}`: etäisyys pos→activeWP + jäljellä olevat etapit |
| `renderRouteOnMap()` | Piirtää/päivittää reittilinja ja numeroidut markkerit kartalle |

### Lisätään — kartalle

- **Reittilinja:** `L.polyline` kaikkien pisteiden välille, katkoviiva `#4fc3f7`
- **Markkerit 1…N:**
  - Ohitettu: harmaa kehä, harmaa numero
  - Aktiivinen: punainen kehä, punainen numero, vaalea tausta
  - Tuleva: tumma kehä, tumma numero
- GPS-poll kutsuu `checkAutoAdvance()` ja `updateRoutePanel()` jokaisen sijainnin päivityksen yhteydessä

### Lisätään — reittipaneeli `#route-panel`

Kiinteä paneeli kartan oikealla reunalla (näkyy vain kun `route.length > 0`):

```
┌─────────────────┐
│ REITTI          │
├─────────────────┤
│ ✓ 1  Lielahti  │  ← ohitettu, harmaa, yliviivaus
│ ✓ 2  Niihama   │  ← ohitettu
│ ▶ 3  Kuru      │  ← aktiivinen, punainen, korostettu
│    2.4 nm·34min │
│ ○ 4  Ruovesi   │  ← tuleva, tumma
├─────────────────┤
│ [▶ Seuraava]   │
├─────────────────┤
│ KOKO REITTI     │
│ 5.8 nm          │  ← jäljellä oleva matka
│ ETA: 1h 22min   │  ← nykyisellä SOG:lla
└─────────────────┘
```

- Jokaisen pisteen rivin oikealla puolella ✕-nappi → `removeRouteWp(idx)`
- "Seuraava"-nappi → `advanceToNextWp()` (manuaalinen ohitus)
- Kokonaismatka ja ETA päivittyvät joka GPS-pollilla — vähenee sitä mukaa kun matka etenee
- Paneeli piilotetaan kun reitti on tyhjä

### Lisätään — asetuspaneeli muutokset

**Toimintorivin muutos:**

| Vanha | Uusi |
|---|---|
| 📍 Kohde | 📍 Lisää WP |
| ✕ Poista | ▶ Aloita |
| 🗑 Reitti | 🗑 Tyhjennä |

**Uusi asetus — automaattiohitus:**

```
Automaattiohitus    [toggle on/off]
  [−]  0.10 nm  [+]
```

- Säde: 0.05–1.00 nm, askel 0.05 nm

Kaikki muut asetukset (Seuraa, Minuuttiviiva, Ääni, Hälytys) säilyvät ennallaan.

### Käyttövirta

1. Avaa asetukset → napauta **Lisää WP** → kartalle ilmestyy vihje "Napauta karttaa"
2. Napautat karttaa → piste lisätään numerolla, linja piirtyy edelliseen pisteeseen
3. Toista kunnes reitti on valmis
4. Avaa asetukset → napauta **Aloita** → navigointi alkaa WP 1:stä
5. Kun lähestyt pistettä < `advanceRadiusNm`, siirrytään automaattisesti seuraavaan
6. Tai paina reittilistan "**Seuraava**"-nappia manuaalisesti
7. Viimeinen piste saavutettu → reitti merkitään valmiiksi, paneeli jää näkyviin

---

## susieq-chart-server.py — muutokset

### Lisätään — `/route` endpoint

Uusi GET-reitti palvelimelle. Karttaplotteri lähettää aktiivisen reitin tilan palvelimelle POST-kutsulla, palvelin tarjoaa sen `/route`-endpointina.

**`GET /route` — vastaus:**

```json
{
  "waypoints": [
    {"lat": 61.5155, "lon": 23.7904},
    {"lat": 61.6200, "lon": 23.8500},
    {"lat": 61.7800, "lon": 23.9100}
  ],
  "active_idx": 1,
  "total_remaining_nm": 5.83,
  "ts": 1716800000
}
```

**`POST /route` — chart.html lähettää päivityksen** aina kun reitti muuttuu (piste lisätään/poistetaan, ohitus tapahtuu).

Tämä endpoint on tarkoitettu tulevaa SeaTalk1-autopilotti-integraatiota varten: spare ESP32 pollaa `/route`, muodostaa NMEA APB-lauseet ja lähettää ne ST1000+:lle.

---

## Ei-scope (tässä versiossa)

- Reittien tallennus/lataus nimellä — myöhempi ominaisuus
- GPX-tuonti/vienti — myöhempi ominaisuus
- MOB-nappi — erillinen ominaisuus
- Ankkurivahti — erillinen ominaisuus
- Varsinainen SeaTalk1 APB-lähetys — toteutetaan spare ESP32 -firmwaressa erikseen

---

## Tiedostomuutokset

| Tiedosto | Muutostyyppi |
|---|---|
| `susieq_glxe300/chart.html` | Korvaa WP-järjestelmän, lisää reittipaneeli ja -logiikka |
| `susieq_glxe300/susieq-chart-server.py` | Lisää `/route` GET+POST endpoint |
