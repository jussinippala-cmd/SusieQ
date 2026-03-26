# C&C 28 — Varustelu ja Lisälaitteet
*Omistajat: Jussi Nippala, Miikka Irri, Miikka Vasaramäki | Päivitetty: 2026-03-15*

---

## Veneen tunnistetiedot

- **Rekisterinumero:** L-7695
- **Luokka/tyyppi:** C&C 28
- **Vuosimalli:** 1975
- **Kotisatama:** Romsin satama
- **Veneseura:** Vene-71

---

## Moottori

### Suzuki Sail 9,9 hv ulkoperämoottori
- **Malli:** Suzuki Sail 9,9 hv
- **Tyyppi:** Ulkoperämoottori (perämoottori)
- **Teho:** 9,9 hv

---

## Sähköjärjestelmä

### Aurinkopaneeli
- **Malli:** PET puolijoustavat monikiteiset aurinkopaneelit
- **Koko:** 630 × 540 mm (24.8 × 21.3 tuumaa)
- **Paksuus:** 3 mm
- **Maksimiteho:** 150 W
- **Nimellisteho:** 50 W
- **Toimintavirta:** 2,7 A
- **Toimintajännite:** 18 V
- **Toimintalämpötila:** 20°C – 70°C
- **Materiaali:** PET + EVA + TPT

### Lataussäädin
- **Malli:** Victron SmartSolar MPPT 75/15 (Bluetooth)
- **Maks. latausvirta:** 15 A
- **Maks. aurinkopaneelijännite:** 75 V
- **Yhteys:** Bluetooth (Victron Connect -sovellus)
- **Protokolla:** MPPT (Maximum Power Point Tracking)

### Ohjauspaneeli
- **Malli:** COLIGHT 12/8 Gang Switch Panel
- **Ohjaus:** App Control (älypuhelinsovellus)
- **Kytkimet:** 12 kytkinpaikkaa

### Akkujen seurantalaite
- **Laite:** Battery Capacity Indicator (akun kapasiteettimittari)

### Akku
- **Kapasiteetti:** 100 Ah lyijyakku

### USB-laturipistoke
- **Liitännät:** USB-A + USB-C
- **Tulojännite:** 12–24 V
- **Lähtöjännite:** 5 V / 3,1 A yhteensä
- **Koko:** 50 × 37 × 29 mm

---

## Navigointi ja instrumentit

- **VHF-radio**
- **Autopilotti:** Raymarine ST1000+ (pinnapilotta)
- **Kallistusmittari**
- **Kompassi**
- **Kiikarit**
- **Etsintävalo**
- **Autoradiotyyppinen radio** (viihde)

---

## Turvallisuusvarusteet

- **Pelastusliivit:** 5 kpl
- **Jauhesammutin**
- **Pelastusrengas:** hevosenkenkämallinen
- **Pelastusrenkaan merkkivalo**
- **Lepuuttajat:** 6 kpl
- **Ankkuri:** Bruce-tyyppinen, 7,5 kg
- **Ankkuriliinat:** 2 kpl (1 keulassa, 1 perässä)
- **Peräportaat:** Kiinteästi veneen perään kiinnitetyt uimaan meno / veneeseen nousu -portaat

---

## Purjeet

- **Pääpurje:** North Sails
- **Etupurje:** rullapurje (Furlex-rullausjärjestelmä)
- **Spinnakkeri**
- **Puomipeite:** Sininen purjekangaspeite (boom cover / sail cover)

---

## Keittiö ja astiastot

### Muovinen astiasto
- 4× lautanen
- 4× leipälautanen
- 4× kulho
- 4× muki
- 4× kirkas muki

### Aterimet (musta ruostumaton teräs)
- 4× haarukka
- 4× veitsi
- 4× lusikka
- 4× teelusikka

---

## Vesijärjestelmä

### Järvivesipumppu
- **Tyyppi:** Kalvopumppu (Diaphragm Water Pump)
- **Materiaali:** Tekniset muovit
- **Jännite:** 12 V DC
- **Maks. virta:** 2 A
- **Maks. paine:** 0,48 MPa
- **Maks. virtaus:** 3,5 l/min
- **Koko:** 12,7 × 9,6 × 6,1 cm
- **Paino:** 489 g
- **Väri:** Musta
- **Toiminta:** Pumppaa järvivettä tankkisäiliöstä hanan kautta tiskialtaaseen

---

## Saniteetti

### WC
- **Tyyppi:** Porta Potti (kannettava kemiallinen WC)
- *Ei kiinteää septitankkia*

---

## Sisustus ja Mukavuudet

### Viskikaappi
- **Mekanismi:** Aktuaattorilla aukeava kaappi
- *Tyylipisteet täysillä — automaattisesti aukeava drinkkaristo*

### Spriikeitin
- **Polttimot:** 2 kpl
- **Käyttö:** Ruoanlaitto (ei valaistukseen)

---

## Sähköjärjestelmän Kaavio (karkea)

```
[Aurinkopaneeli 150W]
        |
[Victron SmartSolar MPPT 75/15]
        |
    [Akku]
        |
[COLIGHT 12/8 Gang Switch Panel]
        |
   +---------+---------+---------+
   |         |         |         |
[Pumppu] [Valot]  [Viskikaappi] [...]
```

---

## Tilattu — Ei vielä asennettu

**Navigointi ja instrumentit**
- **GPS-moduuli GY-NEO6MV2** — NEO-6M/7M/8M, Arduino-yhteensopiva, UART-liitäntä

**Sensorit**
- **AHT20+BMP280** — ilmanlämpötila, kosteus ja ilmanpaine, digitaalinen I²C
- **DS18B20** — vedenpitävä lämpötila-anturi (järvivesi), 1-Wire, XH2.54 3-pin liitin
- **Ultraäänianemometri** — RS485 Modbus, mittaa tuulennopeuden ja -suunnan

**Elektroniikka / kehitysalustat**
- **ESP32-WROOM-32 kehityskortti** — WiFi + Bluetooth, dual-core, USB Type-C
- **HX711 + Load Cell** — punnitusanturi, saatavilla eri kapasiteeteissa (1–50 kg)

---

## TODO / Seuraavat projektit

- [ ] Selvitä akun kapasiteetti ja lisätäänkö toinen akku
- [ ] Dokumentoi COLIGHT-paneelin kytkentäkartta (mihin kytkimeen mikäkin laite)
- [ ] Harkitse makean veden erillissäiliötä järviveden lisäksi

---

*Lisää varusteita listaan sitä mukaa kun asennetaan.*
