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
│   ├── battery.cpp / .h    # INA219 akku-monitori (I2C)
│   ├── tanks.cpp / .h      # XKC-Y25 kapasitanssi-pintaanturit
│   ├── fuel.cpp / .h       # HX711 vaaka (polttoainekanisteri)
│   └── victron.cpp / .h    # Victron SmartSolar BLE (valinnainen)
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

### INA219 (akku)
```
SDA → GPIO 21
SCL → GPIO 22
VCC → 3.3V
GND → GND
Katko akkujohtoon: + johto MAX485:n VIN+ ja VIN- nastaan sarjaan
```

### XKC-Y25 pintaanturit (vesitankki)
```
HUOM: XKC-Y25-T12V ulostulo on 12V!
Käytä jännitteenjakajaa (10kΩ + 20kΩ) tai NPN-versiota jossa pull-up 3.3V:een.

Anturi 0 (pohja)  → GPIO 32
Anturi 25 %       → GPIO 33
Anturi 50 %       → GPIO 25
Anturi 75 %       → GPIO 26
Anturi 100 %      → GPIO 27
```

### HX711 vaaka (polttoaine)
```
DOUT → GPIO 13
SCK  → GPIO 14
VCC  → 3.3V (tai 5V jos moduuli vaatii)
GND  → GND
```

### 12V → 5V muunnin (ESP32:n syöttö)
```
Sisään: veneen 12V
Ulos: ESP32 VIN-nastaan (5V) tai USB-liittimen kautta
```

## Polttoaineen taraus

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
  "battery": { "voltage": 12.6, "current": -1.2, "soc": 72, "source": "ina219", "valid": true },
  "solar":   { "pv_power": 48.0, "pv_voltage": 18.5, "state": 5, "valid": true },
  "water":   { "level_pct": 75, "valid": true },
  "fuel":    { "liters": 12.3, "pct": 61, "valid": true },
  "uptime_s": 3842
}
```

## Kalibrointi

### Vaaka (HX711)
Muokkaa `CALIBRATION_FACTOR` tiedostossa `fuel.cpp`:
1. Lataa ja avaa serial monitor
2. Aseta tunnettu paino (esim. 1 kg) vaa'alle
3. Muuta `CALIBRATION_FACTOR` kunnes lukema on oikein
4. Aloita `420.0f` ja säädä

### Virtamittari (INA219)
Jos mitattu virta on väärä, tarkista shunt-vastus moduulissa.
Useimmissa moduuleissa on 0.1 Ω → `INA219_SHUNT_OHM 0.1`.
Suuren virran moduuleissa voi olla 0.01 Ω.

## Virheenetsintä

| Ongelma | Tarkista |
|---------|---------|
| Tuulianturi ei vastaa | Modbus slave ID (oletus 1), baudinopeus (4800), DE-nasta GPIO 4 |
| INA219 ei löydy | I2C-osoite 0x40, johtimien kytkentä SDA/SCL |
| Victron ei päivity | Encryption key config.h:ssa, BLE kantama |
| Vaaka lukee väärin | Kalibrointikerroin fuel.cpp:ssä, tee tara uudelleen |
| Dashboard ei avaudu | `pio run -t uploadfs` suoritettu? LittleFS partition oikein? |
