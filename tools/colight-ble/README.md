# colight-ble

Kertaluonteinen selvitystyökalu **COLIGHT 12/8 Gang Switch Panel** -kytkinpaneelin
BLE (Bluetooth Low Energy) GATT-protokollan reverse-engineeraukseen. Tarkoitus on
selvittää, mitkä BLE-palvelut/characteristicsit raportoivat kytkinten tilan ja mitkä
ottavat vastaan ohjauskomentoja — tämä tieto on lähtökohta tulevalle ESP32-bridgelle
ja susieq-remote-dashboardin kytkinpaneelinäkymälle.

Tausta, taustatutkimus ja koko selvitysprosessi (kytkentäkartta, GATT-discovery,
tilanlukuformaatin selvitys, yhteysrajoitustesti, write-komentojen tunnistus) on
kuvattu suunnitteludokumentissa:
[`docs/superpowers/specs/2026-06-14-colight-ble-selvitys-design.md`](../../docs/superpowers/specs/2026-06-14-colight-ble-selvitys-design.md)

## Asennus

Vaatii Python 3.10+ ja macOS:n (käyttää `bleak`-kirjastoa BLE-yhteyksiin).

```bash
pip3 install -r requirements.txt
```

## Käyttö

Kaikki komennot ajetaan hakemistosta `tools/colight-ble/`.

### `scan` — etsi paneelin BLE-osoite

```bash
python3 colight_ble.py scan
```

Skannaa lähistön BLE-laitteet 10 sekunnin ajan ja tulostaa taulukon
(nimi, osoite, RSSI) signaalin voimakkuuden mukaan järjestettynä. Käytä tätä
CoLight-paneelin BLE-osoitteen (`AA:BB:CC:DD:EE:FF`) löytämiseen ennen
`discover`- ja `monitor`-komentojen ajamista.

### `discover` — GATT-rakenteen kartoitus

```bash
python3 colight_ble.py discover AA:BB:CC:DD:EE:FF
```

Yhdistää annettuun osoitteeseen, käy läpi kaikki GATT-palvelut ja
characteristicsit, lukee read-kykyisten characteristicsien arvot ja
tallentaa koko rakenteen JSON-tiedostoon `output/discover_<aikaleima>.json`.
Tulostaa lisäksi `[HUOM]`-rivit, jos löytyy tunnettuja candidate-UUID-perheitä
(FFE0/FFE1 — HM-10-tyylinen serial-over-BLE, FFD5/FFD9 — Triones-tyylinen
LED-ohjausprotokolla). Täysin turvallinen — ei kirjoita mitään paneeliin.

### `monitor` — tilanotifikaatioiden lokitus

```bash
python3 colight_ble.py monitor AA:BB:CC:DD:EE:FF
```

Yhdistää annettuun osoitteeseen, tilaa kaikki notify/indicate-kykyiset
characteristicsit ja kirjoittaa joka arvonmuutoksesta rivin
(`timestamp, characteristic, value_hex`) sekä stdouttiin että CSV-tiedostoon
`output/monitor_<aikaleima>.csv`. Jos yhteys katkeaa, yritetään
automaattisesti uudelleenyhdistää 5 sekunnin välein. Lopeta Ctrl+C:llä.

Käytetään esimerkiksi siten, että paneelin kytkimiä käännetään fyysisesti
päälle/pois yksi kerrallaan ja katsotaan, mikä characteristic ja
arvonmuutos vastaa mitä kytkintä.

## Testien ajaminen

```bash
python3 -m pytest test_colight_ble.py -v
```

Testit kattavat puhtaat apufunktiot (UUID-muotoilu, candidate-UUID-tunnistus,
raportin/CSV-rivin muodostus) — eivät vaadi BLE-yhteyttä tai paneelin
fyysistä läsnäoloa.

## Lisätietoa

Koko selvitysprosessi veneellä (kytkentäkartan kerääminen, GATT-discoveryn
tulkinta, yhteysrajoitustesti, write-komentojen varovainen tunnistus) sekä
candidate-UUID-taustatutkimus on kuvattu suunnitteludokumentissa:
[`docs/superpowers/specs/2026-06-14-colight-ble-selvitys-design.md`](../../docs/superpowers/specs/2026-06-14-colight-ble-selvitys-design.md)
