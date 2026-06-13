# Design: CoLight BLE-protokollan selvitys (Vaihe 1)

**Päivämäärä:** 2026-06-14
**Tila:** Hyväksytty
**Tiedostot:** `tools/colight-ble/` (uusi), `CC28_varustelu.md`

---

## Tausta

Veneessä on **COLIGHT 12/8 Gang Switch Panel** (12 kytkinpaikkaa), jota voi ohjata fyysisesti tai älypuhelinsovelluksella Bluetoothin (BLE) kautta. Tavoitteena on pidemmällä aikavälillä saada paneelin kytkintilat näkyviin ja osa kytkimistä ohjattavaksi **susieq-remote**-etädashboardin kautta — tilannekuva kaikista 12 kytkimestä + ohjauspainikkeet valituille ei-kriittisille piireille (esim. valot, USB).

Tämä vaatii kaksi tuntematonta asiaa:

1. **CoLight-paneelin BLE-protokolla** (GATT-palvelut/characteristicsit, mitkä raportoivat tilan ja mitkä ottavat ohjauskomennot) — ei dokumentoitua tietoa saatavilla, pitää selvittää itse.
2. **Kytkentäkartta** — mihin kytkimeen (1-12) mikä laite on kytketty. Merkitty TODO:ksi `CC28_varustelu.md`:ssä.

Cockpit ESP32 (192.168.8.100) tekee jo BLE-yhteyden Victron-laturiin, ja CoLight-paneeli sijaitsee samassa tilassa (<10 m) — joten se on todennäköinen BLE-bridge tulevalle Vaihe 2:lle. Mutta ennen kuin Vaihe 2 (ESP32-bridge) ja Vaihe 3 (susieq-remote UI) voidaan suunnitella, tarvitaan tämän specin kattama selvitystyö.

**Tämä spec kattaa vain Vaihe 1:** selvitystyökalun ja -prosessin. Vaihe 2/3 on luonnosteltu lopussa kontekstiksi, mutta niiden tarkka suunnittelu tehdään erillisessä specissä Vaihe 1:n tulosten pohjalta.

---

## Arkkitehtuuripäätös

Selvitystyö tehdään **Macilla, Python-skriptillä (`bleak`-kirjasto)**, ajettuna veneellä BLE-kantamalla CoLight-paneelista. Skripti ei ole osa mitään firmwarea tai tuotantojärjestelmää — se on kertaluonteinen/toistettava tutkimustyökalu, jonka tulosteet (GATT-kartta JSON + notifikaatioloki CSV) ohjaavat Vaihe 2:n suunnittelua.

Sijainti: **`tools/colight-ble/`** projektin juuressa — uusi hakemisto, koska kyseessä on Mac-puolen kertaluonteinen tutkimustyökalu, joka ei kuulu mihinkään olemassa olevaan firmware- tai GL-XE300-skriptihakemistoon.

---

## Komponentit — `tools/colight-ble/`

### `colight_ble.py`

Yksi CLI-skripti kolmella alikomennolla:

| Komento | Kuvaus |
|---|---|
| `scan` | Skannaa lähistön BLE-laitteet 10 s ajan, tulostaa nimi + osoite + RSSI -taulukon. Käytetään CoLight-paneelin osoitteen/nimen tunnistamiseen. |
| `discover <address>` | Yhdistää annettuun osoitteeseen, käy läpi kaikki GATT-palvelut ja characteristicsit (UUID, properties: read/write/notify/indicate, deskriptorit), lukee read-kykyisten characteristicsien arvot. Tallentaa tuloksen JSON-tiedostoon. |
| `monitor <address>` | Yhdistää, tilaa kaikki notify/indicate-kykyiset characteristicsit, ja kirjoittaa CSV-riviä (`timestamp, characteristic_uuid, value_hex`) joka kerta kun arvo muuttuu. Ajetaan kunnes Ctrl+C. |

### Riippuvuudet

`tools/colight-ble/requirements.txt`:
```
bleak
```

### Tulosteet

`tools/colight-ble/output/` (gitignoroitu, paitsi esimerkkitiedostot):
- `discover_<timestamp>.json` — GATT-kartta
- `monitor_<timestamp>.csv` — notifikaatioloki

### Virhetilanteet

- `discover`/`monitor`: jos osoitteeseen ei saada yhteyttä → selkeä virheviesti ja ehdotus ajaa `scan` ensin
- BLE-yhteys katkeaa kesken `monitor`-ajon → yritetään automaattisesti uudelleenyhdistää ja tilata notifikaatiot uudelleen (kunnes Ctrl+C)

---

## Selvitysprosessi veneellä

### 1. Kytkentäkartta (BLE:stä riippumaton, voi tehdä ensin)

Käydään läpi paneelin 12 kytkintä yksi kerrallaan **fyysisesti** (ei BLE:tä), kirjataan mikä laite/piiri reagoi. Tulos täydentää `CC28_varustelu.md`:n kytkentäkartta-TODO:n suoraan taulukkona:

| Kytkin # | Laite/piiri | Huomiot |
|---|---|---|
| 1 | … | … |
| … | … | … |

### 2. GATT-rakenteen selvitys (turvallinen, luku)

```
python colight_ble.py scan
python colight_ble.py discover <CoLight-osoite>
```

Tuottaa JSON-kartan kaikista palveluista/characteristicsista. Täysin turvallista — ei kirjoiteta mitään paneeliin.

### 3. Tilanlukuformaatin selvitys (turvallinen)

```
python colight_ble.py monitor <CoLight-osoite>
```

Kytkimiä käännetään **fyysisesti** paneelista päälle/pois yksi kerrallaan, ja `monitor`-loki näyttää mikä characteristic ja millainen arvonmuutos vastaa mitä kytkintä. Tämä riittää Vaihe 3:n "tilannekuva"-osuuden suunnitteluun, koska tilaraportointi toimii kytkimen ohjaustavasta riippumatta.

### 4. Yhteysrajoitustesti (turvallinen)

Ajetaan `monitor` Macilla **samaan aikaan** kun CoLight-puhelinsovellus on yhteydessä paneeliin. Kahdesta vaihtoehdosta:

- **Molemmat yhteydet toimivat samanaikaisesti** → `monitor` näkee puhelinsovelluksella tehdyt tilamuutokset notifikaatioina (vahvistaa kohdan 3 tulokset). Tieto siitä, että ESP32-bridge (Vaihe 2) voi pitää oman BLE-yhteyden auki käyttäjän puhelimen rinnalla, on hyödyllinen Vaihe 2 -arkkitehtuurille.
- **Vain yksi yhteys sallittu** → kirjataan tämä rajoitus löydöksiin. Vaihe 2/3-specissä pitää silloin ratkaista, miten ESP32-bridge ja käyttäjän puhelinsovellus jakavat BLE-yhteyden (esim. ESP32 yhdistää vain pyynnöstä).

Tämän testin tulos ei vaikuta kohtaan 5 — write-komentojen selvitys on oma erillinen tarve "Molemmat"-tavoitteen (tilannekuva + ohjaus) takia, riippumatta yhteysrajoituksesta.

### 5. Write-komentojen tunnistus (varovainen)

Tarvitaan Vaihe 3:n ohjausosuutta varten — ESP32-bridge tarvitsee oman write-komentonsa kytkimien kääntämiseen, eikä voi nojata käyttäjän puhelinsovellukseen. Tehdään varovasti:

- Testataan kirjoituksia **vain yhteen**, helposti valvottavaan ja vaarattomaan piiriin (esim. sisävalo tai USB-portti, jonka tilan näkee/kuulee välittömästi)
- Fyysinen kytkin pidetään koko ajan "ON"-asennossa, jotta BLE-relekomento on ainoa muuttuja
- Ei kosketa kytkimiin joiden takana on pumppu tai muu turvallisuuteen vaikuttava laite, ennen kuin kytkentäkartta (kohta 1) on valmis ja write-komentojen formaatti on varmistettu vaarattomalla piirillä

Jos ensimmäisellä venekäynnillä ei ehditä tähän asti (esim. kohdat 1-4 vievät odotettua kauemmin), tämä kohta voidaan siirtää seuraavaan käyntiin — kohdat 1-4 riittävät jo Vaihe 2:n tilanlukuosuuden suunnitteluun.

---

## Tulosten dokumentointi

Selvityksen tulokset kootaan löydösdokumenttiin (`tools/colight-ble/FINDINGS.md` tai liite tähän speciin — päätetään selvityksen jälkeen):

- GATT-palvelu/characteristic-UUID:t ja roolit (tila vs. ohjaus)
- Tilaraportoinnin arvoformaatti (mikä bitti/tavu vastaa mitä kytkintä)
- Yhteysrajoitustuloksen tulos (kohta 4)
- Write-komentoformaatti, jos selvitetty (kohta 5)
- Täytetty kytkentäkartta

Tämä dokumentti on Vaihe 2/3-specin lähtötieto.

---

## Vaihe 2/3 -luonnos (kontekstiksi, ei osa tätä specia)

Selvityksen tulosten perusteella suunnitellaan myöhemmin erillisessä specissä:

- **Vaihe 2** — Cockpit ESP32 lisää toisen BLE-keskusyhteyden (NimBLE) CoLight-paneeliin Victron-yhteyden rinnalle, lukee tilanotifikaatiot ja kirjoittaa ne Supabaseen samaan tapaan kuin muu sensoridata. Mahdollinen ohjauskomentojen kirjoitus (jos selvitetty) toteutetaan Supabase-pollauksen kautta, samalla periaatteella kuin `susieq-camera.sh`:n kamerapyynnöt.
- **Vaihe 3** — susieq-remote saa uuden paneelin: tilannekuva 12 kytkimestä (kytkentäkartan nimillä) + ohjauspainikkeet valituille ei-kriittisille piireille.

---

## Testaus

- `colight_ble.py scan`/`discover`/`monitor`: virheenkäsittely (laitetta ei löydy, yhteys katkeaa) testattavissa myös ilman paneelia paikan päällä
- Varsinainen protokollan selvitys vaatii fyysisen käynnin veneellä SusieQ-Net-verkossa
