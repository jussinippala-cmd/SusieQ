# SusieQ — C&C 28 Sailboat IoT System

## Project Overview
Dual-ESP32 IoT system for a 1975 C&C 28 sailboat. Cockpit unit (ESP32-WROOM-32) runs WiFi AP + WebSocket dashboard. Bow unit (ESP32-CAM) provides camera + LiDAR.

## Directory Structure
- `susieq_dashboard/` — Cockpit firmware (PlatformIO, ESP32-WROOM-32)
  - `src/` — C++ sensor modules (wind, tanks, fuel, victron, gps, weather, rum)
  - `include/config.h` — All GPIO pins, WiFi creds, calibration constants
  - `data/index.html` — Full dashboard UI (HTML5/CSS/JS, ~2000 lines)
- `susieq_bow/` — Bow unit firmware (PlatformIO, ESP32-CAM)
  - `src/` — Camera streaming, TF-Luna LiDAR, power management
- `CC28_tekniset_tiedot.md` — Boat specs (Finnish)
- `CC28_varustelu.md` — Equipment inventory (Finnish)
- `CC28_kunnostus_ja_projektit.md` — Restoration projects & priorities (Finnish)
- `susieq_manual.html` — User manual
- `susieq_preview.html` — Dashboard demo with mock data
- `wiring_diagram.html` — System electrical schematic

## Tech Stack
- **Build:** PlatformIO (NOT Arduino IDE)
- **Cockpit MCU:** ESP32 DevKit V1 — `pio run -t upload` / `pio run -t uploadfs`
- **Bow MCU:** ESP32-CAM AI-Thinker
- **Comms:** WiFi AP "SusieQ-Data" (192.168.4.1), WebSocket `/ws`, REST `/data`
- **Key libs:** ESPAsyncWebServer, ArduinoJson 7, NimBLE, ModbusMaster, HX711, TinyGPSPlus

## Conventions
- Documentation and UI text in **Finnish**
- Each sensor has its own `.h/.cpp` pair in `src/`
- Config constants centralized in `include/config.h`
- Dashboard uses dark GitHub color palette (CSS variables in index.html)
- Sensor data serialized as flat JSON via ArduinoJson

## WiFi & Network
- Cockpit AP: SSID `SusieQ-Data`, password `susieq123`, IP `192.168.4.1`
- Bow unit STA: static IP `192.168.4.10`
- OTA hostnames: `susieq-cockpit.local`, `susieq-bow.local`

## Sensors (Cockpit)
- Wind: RS485 Modbus ultrasonic anemometer (GPIO 16/17/4)
- Water tank: HX711 weight (GPIO 19/18)
- Fuel: HX711 weight (GPIO 13/14)
- GPS: UART (GPIO 34/27, 9600 baud)
- Weather: AHT20+BMP280 (I²C) + DS18B20 water temp (GPIO 26)
- Victron: SmartSolar MPPT 75/15 via BLE
- Rum: HX711 weight (GPIO 25/23)
