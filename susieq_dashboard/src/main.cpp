#include <Arduino.h>
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>
#include <LittleFS.h>
#include <ArduinoOTA.h>
#include "esp_timer.h"
#include "esp_wifi.h"

#include "../include/config.h"
#include "wind.h"
#include "tanks.h"
#include "fuel.h"
#include "victron.h"
#include "gps.h"
#include "weather.h"
#include "rum.h"

// ─── Globals ──────────────────────────────────────────────────────────
AsyncWebServer  server(80);
AsyncWebSocket  ws("/ws");

static unsigned long last_sensor_read = 0;

// ─── JSON builder ─────────────────────────────────────────────────────
static JsonDocument doc;  // pre-allocated to reduce heap fragmentation

static String build_json() {
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
        doc["rum"]["liters"] = round(rum.liters * 10) / 10.0;
        doc["rum"]["pct"]    = round(rum.pct);
    }

    // GPS
    GpsData gps = gps_read();
    doc["gps"]["fix"]   = gps.fix;
    doc["gps"]["valid"] = gps.valid;
    if (gps.fix) {
        doc["gps"]["sog_knots"] = round(gps.sog_knots * 10) / 10.0;
        doc["gps"]["cog_deg"]   = (int)round(gps.cog_deg);
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

// ─── WebSocket event handler ──────────────────────────────────────────
void on_ws_event(AsyncWebSocket* server, AsyncWebSocketClient* client,
                 AwsEventType type, void* arg, uint8_t* data, size_t len) {
    if (type == WS_EVT_CONNECT) {
        Serial.printf("[ws] client #%u connected from %s\n",
                      client->id(), client->remoteIP().toString().c_str());
        // Send current readings immediately on connect
        client->text(build_json());
    } else if (type == WS_EVT_DISCONNECT) {
        Serial.printf("[ws] client #%u disconnected\n", client->id());
    }
}

// ─── HTTP routes ──────────────────────────────────────────────────────
static void setup_routes() {
    // Serve HTML dashboard from LittleFS
    server.on("/", HTTP_GET, [](AsyncWebServerRequest* req) {
        req->send(LittleFS, "/index.html", "text/html");
    });

    // JSON snapshot endpoint (for debugging without WebSocket)
    server.on("/data", HTTP_GET, [](AsyncWebServerRequest* req) {
        req->send(200, "application/json", build_json());
    });

    // Tare fuel scale (call from browser or curl)
    server.on("/tare", HTTP_POST, [](AsyncWebServerRequest* req) {
        fuel_tare();
        req->send(200, "text/plain", "Tare saved");
    });

    // Tare water scale (call from browser or curl)
    server.on("/tare_water", HTTP_POST, [](AsyncWebServerRequest* req) {
        water_tare();
        req->send(200, "text/plain", "Water tare saved");
    });

    // Tare rum scale (call from browser or curl)
    server.on("/tare_rum", HTTP_POST, [](AsyncWebServerRequest* req) {
        rum_tare();
        req->send(200, "text/plain", "Rum tare saved");
    });

    server.serveStatic("/", LittleFS, "/");
    server.onNotFound([](AsyncWebServerRequest* req) {
        req->send(404, "text/plain", "Not found");
    });
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

    // Sensors
    wind_init();
    tanks_init();
    fuel_init();
    victron_init();
    gps_init();
    weather_init();    // AHT20 + BMP280 (I2C) + DS18B20 (1-Wire)
    rum_init();

    // WiFi AP
    WiFi.mode(WIFI_AP);
    WiFi.softAP(WIFI_SSID, WIFI_PASSWORD, WIFI_CHANNEL);
    esp_wifi_set_ps(WIFI_PS_NONE);
    esp_wifi_set_max_tx_power(82);  // 20.5 dBm max
    esp_wifi_set_protocol(WIFI_IF_AP, WIFI_PROTOCOL_11B | WIFI_PROTOCOL_11G | WIFI_PROTOCOL_11N);
    Serial.printf("[wifi] AP started: %s  IP: %s\n",
                  WIFI_SSID, WiFi.softAPIP().toString().c_str());

    // OTA
    ArduinoOTA.setHostname(OTA_HOSTNAME);
    ArduinoOTA.setPassword(OTA_PASSWORD);
    ArduinoOTA.onStart([]() {
        Serial.println("[ota] update starting...");
    });
    ArduinoOTA.onEnd([]() {
        Serial.println("\n[ota] update complete — rebooting");
    });
    ArduinoOTA.onError([](ota_error_t err) {
        Serial.printf("[ota] error %u\n", err);
    });
    ArduinoOTA.begin();
    Serial.printf("[ota] ready — hostname: %s\n", OTA_HOSTNAME);

    // WebSocket + web server
    ws.onEvent(on_ws_event);
    server.addHandler(&ws);
    setup_routes();
    server.begin();
    Serial.println("[http] server started on port 80");

    Serial.println("[SusieQ] ready — open http://192.168.4.1 on iPad");
}

// ─── Loop ─────────────────────────────────────────────────────────────
void loop() {
    ArduinoOTA.handle();

    unsigned long now = millis();
    if (now - last_sensor_read >= SENSOR_INTERVAL_MS) {
        last_sensor_read = now;
        if (ws.count() > 0) {
            String json = build_json();
            ws.textAll(json);
        }
    }

    ws.cleanupClients();
}
