#include "weather.h"
#include "../include/config.h"
#include <Wire.h>
#include <Adafruit_AHTX0.h>
#include <Adafruit_BMP280.h>
#include <OneWire.h>
#include <DallasTemperature.h>

// ─── AHT20 + BMP280 (I2C, GPIO 21/22) ──────────────────────────────────
// AHT20 default I2C address: 0x38 (fixed)
// BMP280 default I2C address: 0x76 (SDO low); 0x77 if SDO pulled high

static Adafruit_AHTX0  aht;
static Adafruit_BMP280 bmp;
static bool aht_ok = false;
static bool bmp_ok = false;
static int  aht_err_count = 0;
#define AHT_MAX_ERRORS  3   // disable after 3 consecutive bad readings

// ─── DS18B20 (1-Wire) ──────────────────────────────────────────────────
// 9-bit resolution → max conversion time ~94 ms (blocking, safe in 2 s loop)
// Wiring: DATA → DS18B20_PIN (GPIO 26), 4.7 kΩ pull-up to 3.3 V
static OneWire          oneWire(DS18B20_PIN);
static DallasTemperature ds18b20(&oneWire);
static bool ds_ok = false;

void weather_init() {
    Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);  // I2C bus for AHT20 + BMP280

    // AHT20
    if (aht.begin()) {
        aht_ok = true;
        Serial.println("[weather] AHT20 OK");
    } else {
        Serial.println("[weather] AHT20 not found — check wiring/address");
    }

    // BMP280 — try 0x76 first, then 0x77
    if (bmp.begin(0x76) || bmp.begin(0x77)) {
        bmp.setSampling(Adafruit_BMP280::MODE_NORMAL,
                        Adafruit_BMP280::SAMPLING_X2,   // temperature oversampling
                        Adafruit_BMP280::SAMPLING_X16,  // pressure oversampling
                        Adafruit_BMP280::FILTER_X16,
                        Adafruit_BMP280::STANDBY_MS_500);
        bmp_ok = true;
        Serial.println("[weather] BMP280 OK");
    } else {
        Serial.println("[weather] BMP280 not found — check wiring/address");
    }

    // DS18B20
    ds18b20.begin();
    ds18b20.setResolution(9);           // 9-bit: ~94 ms conversion, ±0.5 °C
    ds18b20.setWaitForConversion(true); // blocking; acceptable in 2 s poll loop
    if (ds18b20.getDeviceCount() > 0) {
        ds_ok = true;
        Serial.printf("[weather] DS18B20: %d device(s) found\n",
                      ds18b20.getDeviceCount());
    } else {
        Serial.println("[weather] DS18B20 not found — check wiring & pull-up");
    }
}

WeatherData weather_read() {
    WeatherData d;

    // AHT20: air temperature + humidity
    if (aht_ok) {
        sensors_event_t hum_evt, temp_evt;
        if (aht.getEvent(&hum_evt, &temp_evt)) {
            d.air_temp = temp_evt.temperature;
            d.humidity = hum_evt.relative_humidity;

            // Sanity bounds — use error counter, not permanent disable
            if (d.air_temp < -40.0f || d.air_temp > 85.0f ||
                d.humidity < 0.0f   || d.humidity > 100.0f) {
                aht_err_count++;
                if (aht_err_count >= AHT_MAX_ERRORS) {
                    aht_ok = false;
                    Serial.println("[weather] AHT20 disabled after repeated bad readings");
                }
            } else {
                aht_err_count = 0;  // reset on good reading
                d.aht_valid = true;
                d.valid = true;
            }
        }
    }

    // BMP280: barometric pressure
    if (bmp_ok) {
        float pres = bmp.readPressure() / 100.0f; // Pa → hPa
        if (pres > 800.0f && pres < 1200.0f) {
            d.pressure = pres;
            d.bmp_valid = true;
            d.valid = true;
        }
    }

    // DS18B20: water temperature
    if (ds_ok) {
        ds18b20.requestTemperatures();
        float wt = ds18b20.getTempCByIndex(0);
        // DS18B20 returns DEVICE_DISCONNECTED_C (-127) or 85 °C (power-on default)
        if (wt != DEVICE_DISCONNECTED_C && wt != 85.0f
                && wt > -10.0f && wt < 40.0f) {
            d.water_temp = wt;
            d.ds_valid = true;
            d.valid = true;
        }
    }

    return d;
}
