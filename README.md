# SusieQ — C&C 28 (1975)
*Omistajat: Jussi Nippala · Miikka Irri · Miikka Vasaramäki*

---

## Projektin rakenne

```
SusieQ/
├── susieq_dashboard/           # Cockpit-yksikön firmware (ESP32-WROOM-32)
├── susieq_bow/                 # Keulayksikön firmware (ESP32-CAM + LiDAR)
├── CC28_tekniset_tiedot.md     # Veneen mitat, runko, takila, moottori, historia
├── CC28_varustelu.md           # Asennetut laitteet ja varusteet
├── CC28_kunnostus_ja_projektit.md  # Projektiprioriteettilista ja kustannusarviot
├── susieq_manual.html          # Käyttöohje (selaimessa avattava)
├── susieq_preview.html         # Dashboard-esikatselu testidatalla
├── wiring_diagram.html         # Järjestelmän kytkentäkaavio
├── photos/                     # Valokuvat
├── files/                      # Tekniset dokumentit ja taulukot
└── guides/                     # Radioliikenneopas, VHF-kanavataulukko
```

---

## Vene lyhyesti

- **Malli:** C&C 28
- **Vuosimalli:** 1975
- **Valmistusmaa:** Ruotsi (todennäköisesti Göteborg)
- **Rekisterinumero:** L-7695
- **Kotisatama:** Romsin satama (Vene-71)
- **LOA:** 8,53 m · **Leveys:** 2,90 m · **Syväys:** ~1,37–1,52 m
- **Makuupaikat:** 4
- **Takila:** Bermuda-slooppi, alumiinimasto

---

## IoT-järjestelmä

Veneessä on kaksi ESP32-yksikköä, jotka muodostavat oman WiFi-verkon (`SusieQ-Data`). Dashboard avautuu iPadin selaimessa osoitteessa `http://192.168.4.1`.

### Cockpit-yksikkö (susieq_dashboard/)
ESP32-WROOM-32 DevKit V1 — WiFi Access Point + WebSocket-palvelin

| Anturi | Tyyppi |
|--------|--------|
| Tuuli | Ultraäänianemometri (RS485 Modbus) |
| Vesitankki | HX711 vaaka (15 l tankki) |
| Polttoaine | HX711 vaaka (25 l kanisteri) |
| GPS | GY-NEO6MV2 (nopeus, suunta, aika) |
| Sää | AHT20 (lämpö+kosteus) + BMP280 (paine) + DS18B20 (veden lämpö) |
| Akku & aurinko | Victron SmartSolar MPPT 75/15 (BLE) |
| Rommi | HX711 vaaka (0,7 l pullo) |

### Keulayksikkö (susieq_bow/)
ESP32-CAM AI-Thinker — pyörittää omaa itsenäistä WiFi-AP:tä `SusieQ-Bow` (IP: 192.168.5.1). Satamassa puhelin vaihdetaan tähän verkkoon ja avataan http://192.168.5.1 jossa näkyy kameran kuva ja etäisyys laituriin.

| Anturi | Tyyppi |
|--------|--------|
| Kamera | OV2640 (640×480 MJPEG-striimi) |
| Etäisyys | TF-Luna LiDAR (20–800 cm) |

Keulayksikkö siirtyy virransäästötilaan 30 s käyttämättömyyden jälkeen.

---

## Asennetut laitteet (yhteenveto)

**Moottori:**
- Suzuki Sail 9,9 hv ulkoperämoottori

**Sähköjärjestelmä:**
- Victron SmartSolar MPPT 75/15 (Bluetooth)
- 150 W puolijoustava aurinkopaneeli (630×540 mm)
- COLIGHT 12/8 Gang Switch Panel (Bluetooth-ohjaus)
- 100 Ah lyijyakku
- USB-laturipistoke (USB-A + USB-C, 12–24V)

**Navigointi ja instrumentit:**
- VHF-radio
- Raymarine ST1000+ autopilotti (pinnapilotta)
- Kallistusmittari, kompassi, kiikarit, etsintävalo

**Turvallisuus:**
- Pelastusliivit (5 kpl), jauhesammutin
- Hevosenkenkämallinen pelastusrengas + merkkivalo
- Lepuuttajat (6 kpl)
- Bruce-ankkuri 7,5 kg + 2 ankkuriliinat

**Purjeet:**
- Pääpurje: North Sails
- Etupurje: rullapurje (Furlex)
- Spinnakkeri

**Keittiö ja saniteetti:**
- Kalvopumppu järvivedelle (12V, 3,5 l/min)
- Porta Potti
- Spriikeitin (2 polttimoa, ruoanlaitto)

**Sisustus:**
- Aktuaattorilla aukeava viskikaappi
