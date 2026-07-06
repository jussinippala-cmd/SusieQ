#include <Arduino.h>
#include <cmath>
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>
#include <LittleFS.h>
#include <ArduinoOTA.h>
#include "esp_timer.h"
#include "esp_wifi.h"
#include "esp_task_wdt.h"
#include "esp_system.h"

#include "../include/config.h"
#include "wind.h"
#include "tanks.h"
#include "fuel.h"
#include "victron.h"
#include "gps.h"
#include "weather.h"
#include "rum.h"
#include "colight.h"

// ─── Globals ──────────────────────────────────────────────────────────
AsyncWebServer  server(80);

// loop() jumiutuu -> WDT ei saa reset-kutsua -> ESP buuttaa itsensä
#define WDT_TIMEOUT_S 70

// Sovellustason varmistus loop()-tehtävän ULKOPUOLISILLE hangeille (esim.
// AsyncTCP/colight_mutex-tyyppinen deadlock, jota edellä oleva hardware WDT
// ei näe koska loop() jatkaa tikitystä normaalisti). Jos /data ei ole
// vastannut tähän aikaan WiFi:n ollessa yhä yhdistettynä eikä OTA käynnissä,
// laite bootataan itse.
#define HTTP_LIVENESS_TIMEOUT_MS 180000
static volatile uint32_t last_data_request_ms = 0;
static volatile bool ota_active = false;

static unsigned long last_sensor_read = 0;
static String cached_json;             // cached for /data
static SemaphoreHandle_t hx711_mutex = nullptr;  // protects HX711 access

// ─── JSON builder ─────────────────────────────────────────────────────
static String build_json() {
    JsonDocument doc;
    WindData    wind    = wind_read();
    TankData    water   = tanks_read();
    FuelData    fuel    = fuel_read();
    VictronData victron = victron_get();

    doc.clear();

    // Wind
    doc["wind"]["valid"] = wind.valid;
    if (wind.valid) {
        doc["wind"]["speed"]     = round(wind.speed_ms * 10) / 10.0;
        doc["wind"]["direction"] = round(wind.direction);
    }

    // Battery (Victron BLE only)
    doc["battery"]["valid"] = victron.valid;
    if (victron.valid) {
        doc["battery"]["voltage"] = round(victron.battery_voltage * 100) / 100.0;
        doc["battery"]["current"] = round(victron.battery_current * 100) / 100.0;
        doc["battery"]["soc"]     = round(battery_voltage_to_soc(victron.battery_voltage));
    }

    // Solar (Victron only)
    doc["solar"]["valid"] = victron.valid;
    if (victron.valid) {
        doc["solar"]["pv_power"]   = victron.pv_power_w;
        doc["solar"]["pv_voltage"] = victron.pv_voltage;
        doc["solar"]["state"]      = victron.charge_state;
    }

    // Water tank
    doc["water"]["valid"] = water.valid;
    if (water.valid) {
        doc["water"]["liters"]    = round(water.liters * 10) / 10.0;
        doc["water"]["level_pct"] = water.level_pct;
    }

    // Fuel
    doc["fuel"]["valid"] = fuel.valid;
    if (fuel.valid) {
        doc["fuel"]["liters"] = round(fuel.liters * 10) / 10.0;
        doc["fuel"]["pct"]    = round(fuel.pct);
    }

    // Rum
    RumData rum = rum_read();
    doc["rum"]["valid"] = rum.valid;
    if (rum.valid) {
        doc["rum"]["liters"] = round(rum.liters * 100) / 100.0;
        doc["rum"]["pct"]    = round(rum.pct);
    }

    // GPS
    GpsData gps = gps_read();
    doc["gps"]["fix"]   = gps.fix;
    doc["gps"]["valid"] = gps.valid;
    if (gps.fix) {
        doc["gps"]["sog_knots"] = round(gps.sog_knots * 10) / 10.0;
        doc["gps"]["cog_deg"]   = (int)round(gps.cog_deg);
        doc["gps"]["lat"]       = gps.lat;
        doc["gps"]["lon"]       = gps.lon;
    }
    if (gps.time_valid) {
        doc["gps"]["utc_h"] = gps.hour;
        doc["gps"]["utc_m"] = gps.minute;
        doc["gps"]["utc_s"] = gps.second;
    }

    // Weather (AHT20 + BMP280 + DS18B20)
    WeatherData weather = weather_read();
    doc["weather"]["valid"] = weather.valid;
    if (weather.aht_valid) {
        doc["weather"]["air_temp"]   = round(weather.air_temp   * 10) / 10.0;
        doc["weather"]["humidity"]   = round(weather.humidity);
    }
    if (weather.bmp_valid) {
        doc["weather"]["pressure"]   = round(weather.pressure   * 10) / 10.0;
    }
    if (weather.ds_valid) {
        doc["weather"]["water_temp"] = round(weather.water_temp * 10) / 10.0;
    }

    // System uptime (64-bit to avoid 49-day millis() overflow)
    doc["uptime_s"] = (unsigned long)(esp_timer_get_time() / 1000000ULL);

    String out;
    serializeJson(doc, out);
    return out;
}

static String colight_result_to_json(const ColightResult& r) {
    JsonDocument doc;
    doc["success"] = r.success;
    doc["connected"] = r.connected;
    doc["last_updated_ms"] = r.last_updated_ms;
    if (r.success) {
        JsonArray arr = doc["state"].to<JsonArray>();
        for (int i = 0; i < COLIGHT_NUM_CHANNELS; i++) arr.add(r.channels[i]);
    } else {
        doc["error"] = r.error;
    }
    String out;
    serializeJson(doc, out);
    return out;
}

// ─── HTTP routes ──────────────────────────────────────────────────────
static void setup_routes() {
    // Dashboard siirretty modeemille — ks. susieq_glxe300/susieq-dashboard-server.py
    server.on("/", HTTP_GET, [](AsyncWebServerRequest* req) {
        req->send(200, "text/plain", "Dashboard: http://192.168.8.1:8081/");
    });

    // JSON snapshot endpoint (for debugging without WebSocket)
    server.on("/data", HTTP_GET, [](AsyncWebServerRequest* req) {
        last_data_request_ms = millis();
        // Take hx711_mutex to avoid racing loop()'s cached_json reassignment.
        String snapshot;
        if (xSemaphoreTake(hx711_mutex, pdMS_TO_TICKS(50))) {
            snapshot = cached_json;
            xSemaphoreGive(hx711_mutex);
        } else {
            snapshot = cached_json;
        }
        req->send(200, "application/json", snapshot.length() > 0 ? snapshot : "{}");
    });

    // Tare fuel scale (call from browser or curl)
    // Uses hx711_mutex to avoid concurrent HX711 access with loop()
    server.on("/tare", HTTP_POST, [](AsyncWebServerRequest* req) {
        if (xSemaphoreTake(hx711_mutex, pdMS_TO_TICKS(2000))) {
            fuel_tare();
            xSemaphoreGive(hx711_mutex);
            req->send(200, "text/plain", "Tare saved");
        } else {
            req->send(503, "text/plain", "Sensor busy");
        }
    });

    // Tare water scale
    server.on("/tare_water", HTTP_POST, [](AsyncWebServerRequest* req) {
        if (xSemaphoreTake(hx711_mutex, pdMS_TO_TICKS(2000))) {
            water_tare();
            xSemaphoreGive(hx711_mutex);
            req->send(200, "text/plain", "Water tare saved");
        } else {
            req->send(503, "text/plain", "Sensor busy");
        }
    });

    // Tare rum scale
    server.on("/tare_rum", HTTP_POST, [](AsyncWebServerRequest* req) {
        if (xSemaphoreTake(hx711_mutex, pdMS_TO_TICKS(2000))) {
            rum_tare();
            xSemaphoreGive(hx711_mutex);
            req->send(200, "text/plain", "Rum tare saved");
        } else {
            req->send(503, "text/plain", "Sensor busy");
        }
    });

    // Calibrate rum scale — POST /calibrate_rum?g=<grams>
    // e.g. curl -X POST "http://192.168.4.1/calibrate_rum?g=500"
    server.on("/calibrate_rum", HTTP_POST, [](AsyncWebServerRequest* req) {
        if (!req->hasParam("g")) {
            req->send(400, "text/plain", "Missing ?g=<grams>");
            return;
        }
        float grams = req->getParam("g")->value().toFloat();
        if (grams <= 0.0f) {
            req->send(400, "text/plain", "Invalid weight");
            return;
        }
        float known_kg = grams / 1000.0f;
        if (xSemaphoreTake(hx711_mutex, pdMS_TO_TICKS(5000))) {
            bool ok = rum_calibrate(known_kg);
            xSemaphoreGive(hx711_mutex);
            if (ok) {
                char buf[64];
                snprintf(buf, sizeof(buf), "Calibrated with %.0f g — OK", grams);
                req->send(200, "text/plain", buf);
            } else {
                req->send(500, "text/plain", "Calibration failed — scale not ready?");
            }
        } else {
            req->send(503, "text/plain", "Sensor busy");
        }
    });

    // Debug: raw rum reading in kg — GET /rum_raw
    server.on("/rum_raw", HTTP_GET, [](AsyncWebServerRequest* req) {
        if (xSemaphoreTake(hx711_mutex, pdMS_TO_TICKS(2000))) {
            float kg = rum_get_raw_units();
            xSemaphoreGive(hx711_mutex);
            char buf[32];
            snprintf(buf, sizeof(buf), "%.4f kg", kg);
            req->send(200, "text/plain", buf);
        } else {
            req->send(503, "text/plain", "Sensor busy");
        }
    });

    // CORS-preflight /records:lle — selain lähettää Content-Type: application/json,
    // joka laukaisee OPTIONS-esipyynnön kun dashboard pyörii eri originissa (modeemi).
    server.on("/records", HTTP_OPTIONS, [](AsyncWebServerRequest* req) {
        req->send(200);
    });

    // GET /records → lue /records.json LittleFS:stä
    server.on("/records", HTTP_GET, [](AsyncWebServerRequest* req) {
        if (!LittleFS.exists("/records.json")) {
            req->send(200, "application/json", "{}");
            return;
        }
        File f = LittleFS.open("/records.json", "r");
        String body = f.readString();
        f.close();
        req->send(200, "application/json", body);
    });

    // POST /records → kirjoita /records.json LittleFS:ään
    // index==0: avaa kirjoitukseen; index>0: lisää perään (multi-chunk body)
    server.on("/records", HTTP_POST,
        [](AsyncWebServerRequest* req) {
            req->send(200, "text/plain", "OK");
        },
        nullptr,
        [](AsyncWebServerRequest* req, uint8_t* data, size_t len,
           size_t index, size_t total) {
            File f = LittleFS.open("/records.json", index == 0 ? "w" : "a");
            if (f) { f.write(data, len); f.close(); }
        }
    );

    server.on("/victron-debug", HTTP_GET, [](AsyncWebServerRequest* req) {
        VictronDebug d = victron_debug_get();
        String hex = "";
        for (int i = 0; i < d.last_pkt_len; i++) {
            char h[3]; snprintf(h, sizeof(h), "%02X", d.last_pkt[i]);
            hex += h;
        }
        char buf[256];
        snprintf(buf, sizeof(buf),
            "{\"ble_seen\":%lu,\"victron_seen\":%lu,\"decrypt_fail\":%lu,\"decrypt_ok\":%lu,\"last_mac\":\"%s\",\"pkt_len\":%u,\"pkt\":\"%s\"}",
            (unsigned long)d.ble_seen, (unsigned long)d.victron_seen,
            (unsigned long)d.decrypt_fail, (unsigned long)d.decrypt_ok,
            d.last_mac, d.last_pkt_len, hex.c_str());
        req->send(200, "application/json", buf);
    });

    // POST /colight?channel=<1..12>&action=on|off
    server.on("/colight", HTTP_POST, [](AsyncWebServerRequest* req) {
        if (!req->hasParam("channel") || !req->hasParam("action")) {
            req->send(400, "application/json", "{\"success\":false,\"error\":\"missing_params\"}");
            return;
        }
        int channel = req->getParam("channel")->value().toInt();
        String action = req->getParam("action")->value();
        if (channel < 1 || channel > 12 || (action != "on" && action != "off")) {
            req->send(400, "application/json", "{\"success\":false,\"error\":\"invalid_params\"}");
            return;
        }
        ColightResult r = colight_send_command(channel, action == "on");
        req->send(200, "application/json", colight_result_to_json(r));
    });

    // GET /colight/state — read-only refresh, no panel side effects
    server.on("/colight/state", HTTP_GET, [](AsyncWebServerRequest* req) {
        ColightResult r = colight_read_state();
        req->send(200, "application/json", colight_result_to_json(r));
    });

    server.onNotFound([](AsyncWebServerRequest* req) {
        req->send(404, "text/plain", "Not found");
    });
}

// ─── WiFi STA ─────────────────────────────────────────────────────────
static void start_wifi_sta() {
    IPAddress ip, gw, sn;
    ip.fromString(STATIC_IP);
    gw.fromString(GATEWAY_IP);
    sn.fromString(SUBNET_MASK);

    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(true);
    WiFi.config(ip, gw, sn);
    WiFi.begin(STA_SSID, STA_PASSWORD);

    unsigned long t = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - t < 15000) {
        delay(200);
    }

    if (WiFi.status() == WL_CONNECTED) {
        esp_wifi_set_ps(WIFI_PS_MIN_MODEM);
        Serial.printf("[wifi] yhdistetty: %s  RSSI: %d dBm\n",
                      WiFi.localIP().toString().c_str(), WiFi.RSSI());
    } else {
        Serial.println("[wifi] yhdistyminen epäonnistui — jatketaan silti");
    }
}

// ─── Setup ────────────────────────────────────────────────────────────
void setup() {
    Serial.begin(115200);
    delay(500);
    Serial.println("\n[SusieQ] starting up...");

    // Filesystem
    if (!LittleFS.begin(true)) {
        Serial.println("[fs] LittleFS mount failed!");
    } else {
        Serial.println("[fs] LittleFS mounted");
    }

    // HX711 mutex (protects concurrent tare vs read)
    hx711_mutex = xSemaphoreCreateMutex();

    // Sensors
    wind_init();
    tanks_init();
    fuel_init();
    victron_init();
    colight_init();
    gps_init();
    weather_init();    // AHT20 + BMP280 (I2C) + DS18B20 (1-Wire)
    rum_init();

    start_wifi_sta();

    // Hardware watchdog — buuttaa ESP:n automaattisesti jos loop() jumiutuu
    esp_task_wdt_init(WDT_TIMEOUT_S, true);
    esp_task_wdt_add(NULL);
    Serial.printf("[wdt] watchdog käynnissä, timeout %d s\n", WDT_TIMEOUT_S);

    // OTA
    ArduinoOTA.setHostname(OTA_HOSTNAME);
    ArduinoOTA.setPassword(OTA_PASSWORD);
    ArduinoOTA.onStart([]() {
        ota_active = true;
        Serial.println("[ota] update starting...");
    });
    ArduinoOTA.onEnd([]() {
        ota_active = false;
        Serial.println("\n[ota] update complete — rebooting");
    });
    ArduinoOTA.onError([](ota_error_t err) {
        ota_active = false;
        Serial.printf("[ota] error %u\n", err);
    });
    // Feed the hardware watchdog on every OTA chunk, not just once per loop()
    // iteration — a slow/weak WiFi link can make a single ArduinoOTA.handle()
    // call block long enough on its own to trip the 70s watchdog mid-transfer.
    ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
        esp_task_wdt_reset();
    });
    ArduinoOTA.begin();
    Serial.printf("[ota] ready — hostname: %s\n", OTA_HOSTNAME);

    // CORS — dashboard pyörii nyt modeemilla (eri origin), kalibrointikutsut
    // (/tare, /tare_water, /tare_rum, /records) tulevat siis cross-origin.
    DefaultHeaders::Instance().addHeader("Access-Control-Allow-Origin", "*");
    DefaultHeaders::Instance().addHeader("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
    DefaultHeaders::Instance().addHeader("Access-Control-Allow-Headers", "Content-Type");

    // Web server (ei enää WebSocketia — /data riittää modeemin pollaukseen)
    setup_routes();
    server.begin();
    Serial.println("[http] server started on port 80");
    last_data_request_ms = millis();  // baseline for HTTP-liveness watchdog

    Serial.printf("[SusieQ] ready — open http://%s on device\n", STATIC_IP);
}

// ─── Serial command handler ───────────────────────────────────────────
// Commands (send via Serial Monitor, line ending = newline):
//   tare_rum              — tare with empty scale
//   calibrate_rum <grams> — calibrate with known weight on scale
//   rum_raw               — print current reading in kg
static String serial_buf;

static void handle_serial() {
    while (Serial.available()) {
        char c = Serial.read();
        if (c == '\n' || c == '\r') {
            serial_buf.trim();
            if (serial_buf.length() == 0) { serial_buf = ""; return; }

            if (serial_buf == "tare_rum") {
                if (xSemaphoreTake(hx711_mutex, pdMS_TO_TICKS(2000))) {
                    rum_tare();
                    xSemaphoreGive(hx711_mutex);
                    Serial.println("[serial] tare_rum done");
                }
            } else if (serial_buf.startsWith("calibrate_rum ")) {
                float grams = serial_buf.substring(14).toFloat();
                if (grams > 0) {
                    if (xSemaphoreTake(hx711_mutex, pdMS_TO_TICKS(8000))) {
                        bool ok = rum_calibrate(grams / 1000.0f);
                        xSemaphoreGive(hx711_mutex);
                        Serial.printf("[serial] calibrate_rum %s\n", ok ? "OK" : "FAILED");
                    }
                } else {
                    Serial.println("[serial] usage: calibrate_rum <grams>");
                }
            } else if (serial_buf == "rum_raw") {
                if (xSemaphoreTake(hx711_mutex, pdMS_TO_TICKS(2000))) {
                    float kg = rum_get_raw_units();
                    xSemaphoreGive(hx711_mutex);
                    Serial.printf("[serial] rum_raw = %.4f kg\n", kg);
                }
            } else {
                Serial.printf("[serial] unknown: %s\n", serial_buf.c_str());
            }
            serial_buf = "";
        } else {
            serial_buf += c;
        }
    }
}

// ─── Loop ─────────────────────────────────────────────────────────────
static unsigned long _lastWifiCheck = 0;

void loop() {
    esp_task_wdt_reset();

    ArduinoOTA.handle();

    if (millis() - _lastWifiCheck > 30000) {
        _lastWifiCheck = millis();
        if (WiFi.status() != WL_CONNECTED) {
            Serial.println("[wifi] yhteys poikki — yritetaan uudelleen");
            WiFi.reconnect();
        } else {
            esp_wifi_set_ps(WIFI_PS_MIN_MODEM);
        }
    }

    // HTTP-vasteaika-watchdog: kattaa hangit loop()-tehtävän ulkopuolella
    // (esim. AsyncTCP/colight_mutex-deadlock), joita hardware WDT ei näe
    // koska tämä silmukka jatkaa tikitystä normaalisti sellaisen aikana.
    // Huom: tämä nojaa siihen että jokin ulkopuolinen taho (susieq-sensors.sh
    // cron / modeemin dashboard-palvelin) pollaa /data:a säännöllisesti — jos
    // WiFi pysyy yhdistettynä mutta pollaus itsessään pysähtyy modeemin
    // puolella, tämä ESP32 restartoi itseään toistuvasti ~180s välein vaikka
    // laite olisi täysin terve. Hyväksytty kompromissi koska pollaus on
    // luotettava eikä hyökkääjä SusieQ-Net-verkosta voi laukaista tätä
    // pelkällä /colight-liikenteellä (/data ei koske colight_mutex:iin).
    if (WiFi.status() == WL_CONNECTED && !ota_active &&
        millis() - last_data_request_ms > HTTP_LIVENESS_TIMEOUT_MS) {
        Serial.println("[watchdog] ei /data-vastausta 180s WiFi:n ollessa yhdistettynä — restart");
        Serial.flush();
        esp_restart();
    }

    handle_serial();

    unsigned long now = millis();
    if (now - last_sensor_read >= SENSOR_INTERVAL_MS) {
        last_sensor_read = now;
        if (xSemaphoreTake(hx711_mutex, pdMS_TO_TICKS(50))) {
            cached_json = build_json();
            xSemaphoreGive(hx711_mutex);
        }
    }
}
