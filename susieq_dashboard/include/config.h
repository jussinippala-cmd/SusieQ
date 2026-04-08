#pragma once

// ─── Debug logging ────────────────────────────────────────────────────
// 0=none, 1=error, 2=warn, 3=info (default), 4=debug
#ifndef SUSIEQ_LOG_LEVEL
  #define SUSIEQ_LOG_LEVEL 3
#endif
#define LOG_E(tag, fmt, ...) do { if (SUSIEQ_LOG_LEVEL >= 1) Serial.printf("[" tag "] ERROR: " fmt "\n", ##__VA_ARGS__); } while(0)
#define LOG_W(tag, fmt, ...) do { if (SUSIEQ_LOG_LEVEL >= 2) Serial.printf("[" tag "] WARN: "  fmt "\n", ##__VA_ARGS__); } while(0)
#define LOG_I(tag, fmt, ...) do { if (SUSIEQ_LOG_LEVEL >= 3) Serial.printf("[" tag "] "        fmt "\n", ##__VA_ARGS__); } while(0)
#define LOG_D(tag, fmt, ...) do { if (SUSIEQ_LOG_LEVEL >= 4) Serial.printf("[" tag "] DBG: "   fmt "\n", ##__VA_ARGS__); } while(0)

// ─── WiFi Access Point ────────────────────────────────────────────────
#define WIFI_SSID        "SusieQ-Data"
#define WIFI_PASSWORD    "susieq123"   // min 8 chars; change as needed
#define WIFI_CHANNEL     6
#define WIFI_IP          "192.168.4.1"

// ─── Update interval ──────────────────────────────────────────────────
#define SENSOR_INTERVAL_MS  2000       // 2 s between sensor polls

// ─── OTA (Over The Air) firmware update ───────────────────────────────
#define OTA_HOSTNAME   "susieq-cockpit"
#define OTA_PASSWORD   "susieq_ota"    // change before deployment

// ─── RS485 Wind sensor (UART2) ────────────────────────────────────────
#define WIND_RX_PIN      16
#define WIND_TX_PIN      17
#define WIND_DE_PIN      4             // MAX485 DE+RE tied together
#define WIND_BAUD        9600
#define WIND_MODBUS_ID   1             // Modbus slave address of sensor

// ─── HX711 water tank weight sensor ───────────────────────────────────
// Removable water tank sits on a 50 kg load cell platform.
// Tare = empty tank weight (stored in NVS flash via Preferences).
// Net weight / 1.0 kg/L = litres remaining.
//
// Calibration: after wiring, run water_tare() once with EMPTY tank,
// then place a known-weight object and adjust WATER_CALIBRATION_FACTOR
// until the reading matches.
#define WATER_HX711_DOUT_PIN      19
#define WATER_HX711_SCK_PIN       18
#define WATER_CALIBRATION_FACTOR  420.0f   // adjust during calibration
#define WATER_TANK_CAPACITY_L     15.0f    // 15 L tank

// ─── HX711 fuel weight sensor ─────────────────────────────────────────
#define HX711_DOUT_PIN   13
#define HX711_SCK_PIN    14
#define FUEL_CALIBRATION_FACTOR  420.0f   // adjust during calibration
#define FUEL_DENSITY     0.74f         // kg/L for petrol
#define FUEL_CANISTER_MAX_L  25.0f     // max capacity of your fuel canister (25L)

// ─── GPS (UART1) ──────────────────────────────────────────────────────
#define GPS_RX_PIN   34
#define GPS_TX_PIN   27
#define GPS_BAUD     9600

// ─── I2C bus (AHT20 + BMP280) ───────────────────────────────────────
#define I2C_SDA_PIN  21
#define I2C_SCL_PIN  22

// ─── DS18B20 water temperature (1-Wire) ───────────────────────────────
// Wiring: DATA → DS18B20_PIN, 4.7 kΩ pull-up resistor to 3.3 V
#define DS18B20_PIN  26

// ─── HX711 rum bottle weight sensor ─────────────────────────────────
#define RUM_HX711_DOUT_PIN        25
#define RUM_HX711_SCK_PIN         23
#define RUM_CALIBRATION_FACTOR    420.0f   // adjust during calibration
#define RUM_BOTTLE_FULL_KG        0.7f     // full 0.7L bottle net weight in kg
#define RUM_DENSITY               0.94f    // kg/L (approx for rum ~40% ABV)

// ─── Bow unit (ESP32-CAM + TF-Luna) ─────────────────────────────────
#define BOW_IP                  "192.168.4.10"
#define BOW_CHECK_INTERVAL_MS   10000    // health check every 10 s

// ─── Victron SmartSolar MPPT BLE ──────────────────────────────────────
// Find these in VictronConnect: Device → Product Info
// Leave VICTRON_KEY empty ("") to disable Victron BLE
#define VICTRON_NAME     "SmartSolar MPPT 75|15"
// 32-char hex key, no spaces, uppercase.  Example: "0102AABB..."
#define VICTRON_KEY      ""            // <-- FILL IN YOUR KEY HERE
// Set to 0 to disable BLE scanning (e.g. for bench testing without Victron)
#define VICTRON_ENABLED  1
