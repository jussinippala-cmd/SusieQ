#pragma once
#include <Arduino.h>

// ─── Weather sensor data ────────────────────────────────────────────────
// AHT20  → air temperature (°C) + relative humidity (%)
// BMP280 → barometric pressure (hPa)
// DS18B20 → water temperature (°C, 1-Wire)

struct WeatherData {
    float air_temp   = 0.0f;   // °C
    float humidity   = 0.0f;   // %
    float pressure   = 0.0f;   // hPa
    float water_temp = 0.0f;   // °C
    bool  valid      = false;
};

void weather_init();
WeatherData weather_read();
