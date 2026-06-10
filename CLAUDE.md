# SusieQ — C&C 28 Sailboat IoT System

## Session Start — kysy aina

Kysy heti session alussa: **"Oletko nyt SusieQ-Net-verkossa (192.168.8.x)?"**

Vastaus määrittää työtavan modeemimuutoksiin:

| Tilanne | Työtapa |
|---|---|
| SusieQ-Net käytössä | `scp` + `ssh root@192.168.8.1` suoraan |
| Etänä | GoodCloud RTTY + Playwright: goodcloud.xyz → device zg6d216 → Remote SSH |

Muistuta SSH-avaimen lisäämisestä (`ssh-copy-id root@192.168.8.1`) jos sitä ei ole vielä tehty.

## Project Overview

IoT-järjestelmä 1975 C&C 28 -purjeveneeseen. Kaikki yksiköt ovat samassa **SusieQ-Net**-WiFi-verkossa (GL-XE300 reititin/modeemi). Sensordata tallennetaan Supabaseen ja näkyy etädashboardissa susieq.net:issä.

## Verkkoarkkitehtuuri

| Laite | IP | Kuvaus |
|---|---|---|
| GL-XE300 (modeemi/reititin) | 192.168.8.1 | Gateway, 4G-yhteys, SusieQ-Net AP |
| Cockpit ESP32-WROOM-32 | 192.168.8.100 | Sensorit, dashboard, WebSocket |
| Mastokamera keula (ESP32-CAM) | 192.168.8.101 | Kamera, aurinkopaneeli |
| Mastokamera perä (ESP32-CAM) | 192.168.8.102 | Kamera, aurinkopaneeli |
| Bow ESP32-CAM | 192.168.8.103 | Kamera + LiDAR |

WiFi: SSID `SusieQ-Net`, channel automaattinen (salasana: katso config.h)
OTA-salasana: katso config.h

## Directory Structure

- `susieq_dashboard/` — Cockpit-firmware (PlatformIO, ESP32-WROOM-32)
  - `src/` — C++ sensormoduulit (wind, tanks, fuel, victron, gps, weather, rum)
  - `include/config.h` — GPIO-pinnit, WiFi-tunnukset, kalibrointivakiot
  - `data/index.html` — Dashboard-UI (HTML5/CSS/JS, ~2000 riviä)
- `susieq_bow/` — Bow-firmware (PlatformIO, ESP32-CAM)
  - `src/` — Kamerastreami, TF-Luna LiDAR, power management
  - `data/index.html` — Bow-sivu (kamera + etäisyys)
- `susieq_masto/` — Mastokamera-firmware (PlatformIO, ESP32-CAM × 2)
  - `src/` — HTTP-palvelin, MJPEG-stream, WiFi reconnect
  - `include/config.h` — Staattiset IP:t, kamera-asetukset
  - `platformio.ini` — 4 ympäristöä: keula, keula-ota, pera, pera-ota
- `susieq_glxe300/` — GL-XE300-moodemin skriptit
  - `susieq-sensors.sh` — Sensordata Supabaseen + ntfy-hälytykset (cron 1/min)
  - `susieq-camera.sh` — Kamerapyyntöjen pollaus Supabasesta (cron 6/min)
  - `susieq-chart-server.py` — Karttaplotteri port 8080
  - `susieq-temp-log` — Modeemilämpötila lokiin 15 min välein
  - `susieq-logrotate` — 7 päivän lokirotaatio SD-kortilla (cron 03:00)
  - `susieq.env.example` — Ympäristömuuttujat (malli)
- `CC28_tekniset_tiedot.md` — Veneen tekniset tiedot (suomi)
- `CC28_varustelu.md` — Varusteluettelo (suomi)
- `CC28_kunnostus_ja_projektit.md` — Kunnostusprojektit (suomi)
- `susieq_manual.html` — Käyttöohje
- `susieq_preview.html` — Dashboard-demo mock-datalla
- `wiring_diagram.html` — Sähkökaavio

## Tech Stack

- **Build:** PlatformIO (EI Arduino IDE)
- **Cockpit MCU:** ESP32 DevKit V1
- **Bow/Masto MCU:** ESP32-CAM AI-Thinker
- **Backend:** Supabase (sensordata, kamerapyynnöt, auth)
- **Etädashboard:** susieq.net (erillinen repo: susieq-remote, Cloudflare Pages)
- **Etähallinta:** GoodCloud (CGNAT-verkon yli), device ID: zg6d216
- **Karttaplotteri:** Leaflet, port 8080, offline-tiilet SD-kortilla
- **Push-ilmoitukset:** ntfy.sh, kanava: SusieQpurjevene
- **Key libs:** ESPAsyncWebServer, ArduinoJson 7, NimBLE, ModbusMaster, HX711, TinyGPSPlus

## GL-XE300 Modeemi

- Admin: http://192.168.8.1 tai GoodCloud etänä
- Skriptit: `/usr/bin/` ja `/usr/sbin/`
- Ympäristömuuttujat: `/etc/susieq.env` (chmod 600)
- Loki: `/mnt/sda1/logs/system.log` (SD-kortti, 7 päivän historia)
- Karttaplotteri: `http://192.168.8.1:8080/`, tiilet `/mnt/sda1/susieq-tiles/`
- 4G-lepotila: 22:00–06:00 (AT+CFUN=4/1)
- SIM: Moi Laitenetti, 4€/kk

## OTA-päivitykset

Yhdistä kannettava **SusieQ-Net**-verkkoon, sitten:

```bash
# Cockpit
pio run -e susieq-ota -t upload        # susieq_dashboard/

# Bow
pio run -e bow-ota -t upload           # susieq_bow/

# Mastokamerat
pio run -e keula-ota -t upload         # susieq_masto/
pio run -e pera-ota -t upload          # susieq_masto/
```

## Conventions

- Dokumentaatio ja UI-tekstit **suomeksi**
- Jokaisella sensorilla oma `.h/.cpp`-pari `src/`-hakemistossa
- Konfiguraatiovakiot `include/config.h`:ssa
- Dashboard käyttää tummaa GitHub-väriteemaa (CSS-muuttujat index.html:ssä)
- Sensordata JSON-muodossa ArduinoJsonilla

## Sensors (Cockpit, 192.168.8.100)

- Tuuli: RS485 Modbus ultraäänianemometri (GPIO 16/17/4)
- Vesitankki: HX711 paino (GPIO 19/18)
- Polttoaine: HX711 paino (GPIO 13/14)
- GPS: UART (GPIO 34/27, 9600 baud)
- Sää: AHT20+BMP280 (I²C) + DS18B20 veden lämpötila (GPIO 26)
- Victron: SmartSolar MPPT 75/15 BLE-yhteys
- Rommi: HX711 paino (GPIO 25/23)
