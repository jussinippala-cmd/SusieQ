#include <Arduino.h>
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>
#include <LittleFS.h>
#include <HTTPClient.h>
#include <ArduinoOTA.h>

#include "../include/config.h"
#include "wind.h"
#include "battery.h"
#include "tanks.h"
#include "fuel.h"
#include "victron.h"
#include "gps.h"
#include "weather.h"

// ─── Globals ──────────────────────────────────────────────────────────
AsyncWebServer  server(80);
AsyncWebSocket  ws("/ws");

static unsigned long last_sensor_read = 0;

// ─── Bow health check (runs on separate core to avoid blocking main loop) ──
static volatile bool bow_online = false;
static volatile int  bow_rssi   = 0;
static void check_bow_task(void* param);  // forward declaration

// ─── JSON builder ─────────────────────────────────────────────────────
static String build_json() {
    WindData    wind    = wind_read();
    BatteryData batt    = battery_read();
    TankData    water   = tanks_read();
    FuelData    fuel    = fuel_read();
    VictronData victron = victron_get();

    JsonDocument doc;

    // Wind
    doc["wind"]["valid"] = wind.valid;
    if (wind.valid) {
        doc["wind"]["speed"]     = round(wind.speed_ms * 10) / 10.0;
        doc["wind"]["direction"] = round(wind.direction);
    }

    // Battery (prefer Victron if available, fall back to INA219)
    if (victron.valid) {
        doc["battery"]["voltage"] = round(victron.battery_voltage * 100) / 100.0;
        doc["battery"]["current"] = round(victron.battery_current * 100) / 100.0;
        doc["battery"]["soc"]     = round(battery_voltage_to_soc(victron.battery_voltage));
        doc["battery"]["source"]  = "victron";
    } else if (batt.valid) {
        doc["battery"]["voltage"] = round(batt.voltage * 100) / 100.0;
        doc["battery"]["current"] = round(batt.current * 100) / 100.0;
        doc["battery"]["soc"]     = round(batt.soc_pct);
        doc["battery"]["source"]  = "ina219";
    }
    doc["battery"]["valid"] = batt.valid || victron.valid;

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
    if (weather.valid) {
        doc["weather"]["air_temp"]   = round(weather.air_temp   * 10) / 10.0;
        doc["weather"]["humidity"]   = round(weather.humidity);
        doc["weather"]["pressure"]   = round(weather.pressure   * 10) / 10.0;
        doc["weather"]["water_temp"] = round(weather.water_temp * 10) / 10.0;
    }

    // Bow unit status
    doc["bow"]["online"] = bow_online;
    if (bow_online) {
        doc["bow"]["rssi"] = (int)bow_rssi;
    }

    // System uptime
    doc["uptime_s"] = millis() / 1000;

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
    battery_init();    // also calls Wire.begin() for I2C bus
    tanks_init();
    fuel_init();
    victron_init();
    gps_init();
    weather_init();    // AHT20 + BMP280 (I2C) + DS18B20 (1-Wire)

    // WiFi AP
    WiFi.mode(WIFI_AP);
    WiFi.softAP(WIFI_SSID, WIFI_PASSWORD, WIFI_CHANNEL);
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

    // Bow health check on core 0 (main loop runs on core 1)
    xTaskCreatePinnedToCore(check_bow_task, "bow_chk", 4096, NULL, 1, NULL, 0);

    Serial.println("[SusieQ] ready — open http://192.168.4.1 on iPad");
}

// ─── Bow health check (FreeRTOS task, non-blocking) ──────────────────
static void check_bow_task(void* param) {
    for (;;) {
        HTTPClient http;
        http.setTimeout(2000);
        http.begin("http://" BOW_IP "/status");
        int code = http.GET();
        if (code == 200) {
            JsonDocument doc;
            if (!deserializeJson(doc, http.getString())) {
                bow_online = true;
                bow_rssi   = doc["wifi_rssi"] | 0;
            }
        } else {
            bow_online = false;
        }
        http.end();
        vTaskDelay(pdMS_TO_TICKS(BOW_CHECK_INTERVAL_MS));
    }
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
