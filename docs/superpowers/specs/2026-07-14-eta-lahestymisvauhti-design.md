# Plotterin ETA lähestymisvauhdista — Design Spec

**Päivämäärä:** 2026-07-14
**Kohde:** `susieq_glxe300/chart.html` (kaikki muutokset yhteen tiedostoon; palvelimeen ei kosketa)

## Ongelma

Karttaplotterin reittipaneelin ETA lasketaan nykyisin suoraan `etäisyys / SOG`
(`chart.html`, `updateRoutePanel()`). Laskenta olettaa, että vene etenee suoraan
kohti reittipistettä. Purjehdittaessa — erityisesti kryssatessa — todellinen
lähestymisnopeus on selvästi pienempi kuin SOG, joten ETA on usein hyvin
optimistinen ja siksi käytännössä hyödytön.

## Ratkaisu

Kolme muutosta reittipaneelin laskentaan:

1. **Tasoitettu lähestymisvauhti** — mitataan jäljellä olevan reittimatkan
   todellista lyhenemistä ~6 minuutin ikkunassa. Käytetään aktiivisen
   reittipisteen ETA:han ja koko reitin ETA:n ensimmäiseen legiin.
2. **Matkan keskivauhti** — SOG-näytteiden keskiarvo reitin aloituksesta
   lähtien. Käytetään koko reitin ETA:n loppulegeille.
3. **Hetkellinen VMC-näyttö** — uusi rivi reittipaneeliin, josta näkee heti
   onko nykyinen halssi tehokas.

Kaikki laskenta tapahtuu selaimessa. Data kerätään `pollGPS()`-funktiossa,
joka ajetaan 2 sekunnin välein.

## 1. Näytepuskuri ja tasoitettu lähestymisvauhti

Uusi puskuri `_progressBuf`: taulukko pareja `{t: ms-aikaleima, rem: nm}`,
missä `rem` on jäljellä oleva kokonaisreittimatka (`calcRemainingRoute()`).

- `pollGPS()` lisää näytteen joka kierroksella, kun reitti on aktiivinen ja
  GPS-fix on voimassa.
- Yli 6 minuuttia vanhat näytteet pudotetaan lisäyksen yhteydessä.
- Tasoitettu lähestymisvauhti (kn) =
  `(vanhimman näytteen rem − uusimman rem) / kulunut aika tunteina`.

**Miksi kokonaisreittimatka eikä etäisyys aktiiviseen pisteeseen:**
automaattiohituksen WP-vaihdossa kokonaismatka on lähes jatkuva (hyppy
korkeintaan ~2 × ohitussäde ≈ 0,2 nm alaspäin), joten vauhtilukema ei
hypähdä. Etäisyys aktiiviseen pisteeseen sen sijaan kasvaisi äkillisesti.

**Puskurin nollaus:** puskuri tyhjennetään aina kun jäljellä oleva matka
muuttuu epäjatkuvasti muusta syystä kuin veneen liikkeestä:

- `startRoute()` — reitin aloitus
- `addRouteWp()` / `removeRouteWp()` / `clearRoute()` — reitin muokkaus
- manuaalinen `advanceToNextWp()` (paneelin ▶ Seuraava -nappi) — toisin kuin
  automaattiohitus, manuaaliohitus voi tapahtua kaukana pisteestä, jolloin
  matka lyhenee äkillisesti paljon

Automaattiohitus **ei** nollaa puskuria (jatkuvuus, ks. yllä).

**Fallback-säännöt (tässä järjestyksessä):**

1. Puskurin aikaväli < 60 s → ETA lasketaan SOG:sta kuten nykyisin.
2. Tasoitettu lähestymisvauhti < 0,2 kn → ETA = "—" (ei ääretöntä/negatiivista).

GPS-katkoksessa näytteitä ei kerry; puskurin näytteet vanhenevat ja ikkuna
kutistuu, jolloin sääntö 1 palauttaa SOG-fallbackin. Erillistä
virhekäsittelyä ei tarvita.

## 2. Aktiivisen reittipisteen rivi

`updateRoutePanel()`-funktiossa aktiivisen rivin ETA-minuutit lasketaan
tasoitetulla lähestymisvauhdilla SOG:n sijaan (fallback-säännöt yllä).
Etäisyys ja suuntima säilyvät ennallaan.

## 3. Koko reitin ETA

```
kokonais-ETA = (etäisyys aktiiviseen WP:hen) / tasoitettu lähestymisvauhti
             + (loppulegien yhteispituus) / matkan keskivauhti
```

**Matkan keskivauhti:** juokseva SOG-näytteiden keskiarvo (summa + lukumäärä)
reitin aloituksesta (`startRoute()`) lähtien. Näyte lisätään samassa
`pollGPS()`-kohdassa kuin puskurinäyte. Nollataan `startRoute()`-kutsussa.

**Fallbackit:**

- Alle 60 s keskivauhtidataa TAI keskivauhti < 0,2 kn → loppulegit lasketaan
  nykyisellä SOG:lla.
- Ensimmäinen termi noudattaa kohdan 1 fallback-sääntöjä; jos se on "—",
  koko ETA on "—".

Esitysmuoto säilyy ennallaan: `< 60 min → "N min"`, muuten `"N.N h"`.

## 4. VMC-rivi reittipaneelissa

`#route-total`-lohkoon lisätään uusi rivi (id `route-total-vmc`):

```
VMC 3.2 kn
```

- VMC = `SOG × cos(suuntima aktiiviseen WP:hen − COG)`, lasketaan joka
  pollauksella tuoreista GPS-arvoista (ei tasoitusta — tämä on tarkoituksella
  hetkellinen luku).
- Negatiivinen arvo näytetään miinusmerkkisenä (vene etääntyy pisteestä).
- Kun reitti ei ole aktiivinen tai SOG < 0,2 kn → "—".
- Tarvitaan uusi tilamuuttuja `_lastCog` (asetetaan `pollGPS()`:ssä
  `_lastSog`-muuttujan viereen).

## Muutettavat kohdat (`chart.html`)

| Kohta | Muutos |
|---|---|
| Route-tilamuuttujat | Lisää `_progressBuf`, `_tripSogSum`, `_tripSogCount`, `_tripStartT`, `_lastCog` |
| `pollGPS()` | Näytteiden keräys puskuriin + keskivauhtiin, `_lastCog`-päivitys |
| `updateRoutePanel()` | Aktiivisen rivin ETA, kokonais-ETA, VMC-rivi uusilla kaavoilla |
| `startRoute()` | Nollaa puskuri ja keskivauhti |
| `addRouteWp()`, `removeRouteWp()`, `clearRoute()`, `advanceToNextWp()` | Nollaa puskuri. `advanceToNextWp(auto)` saa lippuparametrin: `checkAutoAdvance()` kutsuu `advanceToNextWp(true)` (ei nollausta), nappi kutsuu ilman argumenttia (nollaus) |
| `#route-total` HTML | Uusi VMC-rivi |

Uudet laskentafunktiot erillisiksi funktioiksi (esim. `closingSpeedKn()`,
`instantVmcKn()`, `formatEta()`), jotta niitä voi testata selainkonsolissa
syntetisoiduilla näytteillä.

## Ei kuulu tähän työhön

- Palvelinmuutokset (`susieq-chart-server.py`, `/route`-endpoint)
- Tuulidataan perustuva ennuste loppulegeille (ei tuulisensoria plotterilla)
- Puskurin persistointi sivulatauksen yli (historia kertyy 6 minuutissa)

## Testaus

1. **Konsolitestit:** syötetään `_progressBuf`:iin käsin näytteitä
   (esim. tasainen 0,01 nm / 12 s lyheneminen → 3,0 kn) ja tarkistetaan
   `closingSpeedKn()`- ja ETA-tulokset. Testataan fallbackit: tyhjä puskuri,
   < 60 s ikkuna, negatiivinen eteneminen.
2. **VMC-tarkistus:** suuntima = COG → VMC = SOG; suuntima ⊥ COG → VMC ≈ 0;
   vastakkainen → VMC = −SOG.
3. **Käyttötesti veneellä:** kryssilegillä ETA:n tulee asettua realistiseksi
   ~2–6 min sisällä ja pysyä vakaana halssien yli; VMC-luvun tulee elää
   halssin mukana.
