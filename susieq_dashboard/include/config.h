#pragma once

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
#define WIND_BAUD        4800
#define WIND_MODBUS_ID   1             // Modbus slave address of sensor

// ─── INA219 battery monitor (I2C) ─────────────────────────────────────
#define INA219_SDA       21
#define INA219_SCL       22
#define INA219_SHUNT_OHM 0.1f          // default shunt on most modules (100 mΩ)
#define BATTERY_CAPACITY_AH  100.0f    // your battery bank size

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
#define FUEL_DENSITY     0.74f         // kg/L for petrol
#define FUEL_CANISTER_MAX_L  25.0f     // max capacity of your fuel canister (25L)

// ─── GPS (UART1) ──────────────────────────────────────────────────────
#define GPS_RX_PIN   34
#define GPS_TX_PIN   27
#define GPS_BAUD     9600

// ─── DS18B20 water temperature (1-Wire) ───────────────────────────────
// Wiring: DATA → DS18B20_PIN, 4.7 kΩ pull-up resistor to 3.3 V
#define DS18B20_PIN  26

// ─── Bow unit (ESP32-CAM + TF-Luna) ─────────────────────────────────
#define BOW_IP                  "192.168.4.10"
#define BOW_CHECK_INTERVAL_MS   10000    // health check every 10 s

// ─── Victron SmartSolar MPPT BLE ──────────────────────────────────────
// Find these in VictronConnect: Device → Product Info
// Leave VICTRON_KEY empty ("") to disable Victron BLE
#define VICTRON_NAME     "SmartSolar MPPT 75|15"
// 32-char hex key, no spaces, uppercase.  Example: "0102AABB..."
#define VICTRON_KEY      ""            // <-- FILL IN YOUR KEY HERE
// Set to 1 when VICTRON_KEY is filled in, 0 to disable BLE scanning
#define VICTRON_ENABLED  0
