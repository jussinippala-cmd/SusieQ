# SusieQ Dashboard — ESP32 firmware

## Rakenne

```
susieq_dashboard/
├── platformio.ini          # Projekti- ja kirjastomääritykset
├── include/
│   └── config.h            # Kaikki nastat, salasanat, vakiot — muokkaa tänne
├── src/
│   ├── main.cpp            # WiFi AP + WebSocket-palvelin + pääsilmukka
│   ├── wind.cpp / .h       # RS485 ultraäänituulianturi (Modbus RTU)
│   ├── tanks.cpp / .h      # HX711 vaaka (vesitankki)
│   ├── fuel.cpp / .h       # HX711 vaaka (polttoainekanisteri)
│   ├── victron.cpp / .h    # Victron SmartSolar BLE (akku + aurinkopaneeli)
│   ├── gps.cpp / .h        # GPS-moduuli (nopeus, suunta, aika, sijainti)
│   ├── weather.cpp / .h    # AHT20 + BMP280 + DS18B20 (ilma/vesi sää)
│   └── rum.cpp / .h        # HX711 vaaka (rommipullo)
└── data/
    └── index.html          # Dashboard (LittleFS:ään ladataan pio run -t uploadfs)
```

## Käyttöönotto

### 1. Asenna PlatformIO
```
pip install platformio
# tai VS Code → PlatformIO IDE -laajennus
```

### 2. Muokkaa config.h
- `WIFI_PASSWORD` — vaihda haluamaksesi
- `VICTRON_KEY` — syötä 32-merkkinen hex-avain VictronConnectista
  (Laite → Product Info → Encryption key)
  Jätä tyhjäksi `""` jos ei käytetä.
- Tarkista GPIO-nastat vastaamaan omaa kytkentääsi

### 3. Kääntäminen ja lataaminen
```bash
cd susieq_dashboard

# Lataa firmware ESP32:een
pio run -t upload

# Lataa HTML-dashboard LittleFS:ään (erillinen vaihe!)
pio run -t uploadfs
```

### 4. Yhdistä iPad
- WiFi: **SusieQ-Data** / salasana: `susieq123`
- Selain: `http://192.168.4.1`
- Lisää kotinäytölle: Safari → Jaa → Lisää kotivalikkoon

## Kytkentä (ESP32 DevKit V1)

### RS485 tuulianturi (MAX485-moduuli välissä)
```
MAX485 A/B → tuulianturin A/B johtimet
MAX485 RO  → ESP32 GPIO 16 (RX)
MAX485 DI  → ESP32 GPIO 17 (TX)
MAX485 DE+RE (yhdistetty) → ESP32 GPIO 4
MAX485 VCC → 5V, GND → GND
Tuulianturi: 12V suoraan venejohdosta
```

### HX711 vaaka (polttoaine)
```
DOUT → GPIO 13
SCK  → GPIO 14
VCC  → 3.3V (tai 5V jos moduuli vaatii)
GND  → GND
```

### GPS (GY-NEO6MV2)
```
RX → GPIO 34
TX → GPIO 27
VCC → 3.3V, GND → GND
Baudinopeus: 9600
```

### Sääasema (AHT20 + BMP280 + DS18B20)
```
AHT20 + BMP280 (I²C):
  SDA → GPIO 21 (oletus I²C)
  SCL → GPIO 22 (oletus I²C)
  VCC → 3.3V, GND → GND

DS18B20 (veden lämpötila, 1-Wire):
  DATA → GPIO 26 (4,7 kΩ pull-up 3.3V:een)
  VCC → 3.3V, GND → GND
```

### HX711 vaaka (rommi)
```
DOUT → GPIO 25
SCK  → GPIO 23
VCC  → 3.3V, GND → GND
```

### HX711 vaaka (vesitankki)
```
DOUT → GPIO 19
SCK  → GPIO 18
VCC  → 3.3V, GND → GND
```

### 12V → 5V muunnin (ESP32:n syöttö)
```
Sisään: veneen 12V
Ulos: ESP32 VIN-nastaan (5V) tai USB-liittimen kautta
```

## Taraus (vaa'at)

1. Aseta **tyhjä** kanisteri vaa'alle
2. Avaa `http://192.168.4.1` selaimessa
3. Paina **"Tara (tyhjä kanisteri)"** -nappia
4. Tara tallennetaan ESP32:n flash-muistiin (säilyy virrankatkojen yli)

## JSON API

Dashboard-datan voi hakea myös suoraan:
```
GET http://192.168.4.1/data
```

Esimerkki vastaus:
```json
{
  "wind":    { "speed": 5.2, "direction": 225.0, "valid": true },
  "battery": { "voltage": 12.6, "current": -1.2, "soc": 72, "valid": true },
  "solar":   { "pv_power": 48.0, "pv_voltage": 18.5, "state": 5, "valid": true },
  "water":   { "liters": 12.3, "level_pct": 75, "valid": true },
  "fuel":    { "liters": 12.3, "pct": 61, "valid": true },
  "rum":     { "liters": 0.65, "pct": 93, "valid": true },
  "gps":     { "sog_knots": 3.5, "cog_deg": 180, "fix": true, "valid": true },
  "weather": { "air_temp": 15.2, "humidity": 68, "pressure": 1013.4, "water_temp": 8.5, "valid": true },
  "bow":     { "online": true, "rssi": -45 },
  "uptime_s": 3842
}
```

## Kalibrointi

### Vaa'at (HX711)
Kalibrointikertoimet ovat tiedostossa `include/config.h`:
- `FUEL_CALIBRATION_FACTOR` — polttoainevaaka (oletus 420.0)
- `WATER_CALIBRATION_FACTOR` — vesitankkivaaka (oletus 420.0)
- `RUM_CALIBRATION_FACTOR` — rommivaaka

Kalibrointi:
1. Lataa firmware ja avaa serial monitor
2. Aseta tunnettu paino (esim. 1 kg) vaa'alle
3. Muuta kalibrointikerrointa kunnes lukema on oikein
4. Tee tara dashboardin kautta tyhjällä astialla

## Virheenetsintä

| Ongelma | Tarkista |
|---------|---------|
| Tuulianturi ei vastaa | Modbus slave ID (oletus 1), baudinopeus (4800), DE-nasta GPIO 4 |
| Victron/akku ei päivity | Encryption key config.h:ssa, BLE kantama, VICTRON_ENABLED=1 |
| Vaaka lukee väärin | Kalibrointikerroin config.h:ssa, tee tara uudelleen |
| GPS ei saa fixiä | Avoin taivas tarvitaan, tarkista RX/TX (GPIO 34/27), baud 9600 |
| Sääanturit ei toimi | I²C-osoitteet (AHT20: 0x38, BMP280: 0x76/0x77), pull-up-vastukset |
| DS18B20 näyttää -127 °C | 4,7 kΩ pull-up puuttuu, tarkista GPIO 26 |
| Keulayksikkö offline | Tarkista WiFi-yhteys SusieQ-Data-verkkoon, IP 192.168.4.10 |
| Dashboard ei avaudu | `pio run -t uploadfs` suoritettu? LittleFS partition oikein? |
