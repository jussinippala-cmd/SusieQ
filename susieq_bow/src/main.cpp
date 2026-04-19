#include <Arduino.h>
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>
#include <ArduinoOTA.h>
#include <LittleFS.h>
#include "esp_camera.h"
#include "esp_wifi.h"
#include "../include/config.h"
#include "lidar.h"
#include "stream.h"

// ─── AI-Thinker ESP32-CAM pin definitions ────────────────────────────
#define PWDN_GPIO_NUM     32
#define RESET_GPIO_NUM    -1
#define XCLK_GPIO_NUM      0
#define SIOD_GPIO_NUM     26
#define SIOC_GPIO_NUM     27
#define Y9_GPIO_NUM       35
#define Y8_GPIO_NUM       34
#define Y7_GPIO_NUM       39
#define Y6_GPIO_NUM       36
#define Y5_GPIO_NUM       21
#define Y4_GPIO_NUM       19
#define Y3_GPIO_NUM       18
#define Y2_GPIO_NUM        5
#define VSYNC_GPIO_NUM    25
#define HREF_GPIO_NUM     23
#define PCLK_GPIO_NUM     22

// ─── Power state machine (F2: eliminates race window) ────────────────
#include "power_state.h"

// ─── Globals (some non-static for stream.cpp extern access) ──────────
AsyncWebServer server(80);
static unsigned long _bootTime              = 0;
volatile PowerState _powerState             = POWER_AWAKE;
volatile unsigned long _lastActivity        = 0;
SemaphoreHandle_t _camMutex                 = NULL;  // protects camera hw
static SemaphoreHandle_t _lidarMutex        = NULL;  // protects lidar UART reads
static volatile bool _cameraOk              = false; // R4: track camera init status

// ─── Camera init ──────────────────────────────────────────────────────
static bool init_camera() {
    esp_camera_deinit();  // F6: idempotent — safe if not initialized

    camera_config_t config;
    config.ledc_channel = LEDC_CHANNEL_0;
    config.ledc_timer   = LEDC_TIMER_0;
    config.pin_d0       = Y2_GPIO_NUM;
    config.pin_d1       = Y3_GPIO_NUM;
    config.pin_d2       = Y4_GPIO_NUM;
    config.pin_d3       = Y5_GPIO_NUM;
    config.pin_d4       = Y6_GPIO_NUM;
    config.pin_d5       = Y7_GPIO_NUM;
    config.pin_d6       = Y8_GPIO_NUM;
    config.pin_d7       = Y9_GPIO_NUM;
    config.pin_xclk     = XCLK_GPIO_NUM;
    config.pin_pclk     = PCLK_GPIO_NUM;
    config.pin_vsync    = VSYNC_GPIO_NUM;
    config.pin_href     = HREF_GPIO_NUM;
    config.pin_sccb_sda = SIOD_GPIO_NUM;
    config.pin_sccb_scl = SIOC_GPIO_NUM;
    config.pin_pwdn     = PWDN_GPIO_NUM;
    config.pin_reset    = RESET_GPIO_NUM;
    config.xclk_freq_hz = 20000000;
    config.pixel_format = PIXFORMAT_JPEG;
    config.grab_mode    = CAMERA_GRAB_LATEST;

    if (psramFound()) {
        config.frame_size   = CAM_FRAMESIZE;
        config.jpeg_quality = CAM_QUALITY;
        config.fb_count     = 2;
        config.fb_location  = CAMERA_FB_IN_PSRAM;
    } else {
        config.frame_size   = FRAMESIZE_QVGA;
        config.jpeg_quality = 15;
        config.fb_count     = 1;
        config.fb_location  = CAMERA_FB_IN_DRAM;
    }

    esp_err_t err = esp_camera_init(&config);
    if (err != ESP_OK) {
        Serial.printf("[cam] init failed: 0x%x\n", err);
        _cameraOk = false;
        return false;
    }
    sensor_t* s = esp_camera_sensor_get();
    if (s != NULL) {
        s->set_raw_gma(s, 1);
    }

    Serial.println("[cam] camera initialized");
    _cameraOk = true;
    return true;
}

// ─── Power management (F2: state machine, no race window) ───────────
static bool bow_sleep() {
    // R4: bounded timeout — don't block async web server task indefinitely
    if (!xSemaphoreTake(_camMutex, pdMS_TO_TICKS(3000))) {
        Serial.println("[power] sleep: mutex timeout");
        return false;
    }

    if (_powerState != POWER_AWAKE) {
        xSemaphoreGive(_camMutex);
        return false;
    }

    // F2: transition to SHUTTING_DOWN under mutex — wake will see this
    _powerState = POWER_SHUTTING_DOWN;
    xSemaphoreGive(_camMutex);

    // Wait for stream handler to notice SHUTTING_DOWN and exit
    vTaskDelay(pdMS_TO_TICKS(50));

    // Re-acquire and finalize sleep
    if (!xSemaphoreTake(_camMutex, pdMS_TO_TICKS(3000))) {
        Serial.println("[power] sleep finalize: mutex timeout — reverting to AWAKE");
        _powerState = POWER_AWAKE;  // don't leave stuck in SHUTTING_DOWN
        return false;
    }

    // F2: if wake was called during the window, it set state back to AWAKE
    if (_powerState != POWER_SHUTTING_DOWN) {
        xSemaphoreGive(_camMutex);
        return false;  // wake won the race — don't sleep
    }

    Serial.println("[power] sleeping — camera + LiDAR off, WiFi modem sleep");

    esp_camera_deinit();
    pinMode(PWDN_GPIO_NUM, OUTPUT);
    digitalWrite(PWDN_GPIO_NUM, HIGH);

    esp_wifi_set_ps(WIFI_PS_MIN_MODEM);

    _powerState = POWER_SLEEPING;
    xSemaphoreGive(_camMutex);
    return true;
}

bool bow_wake() {
    // R4: bounded timeout — don't block async web server task indefinitely
    if (!xSemaphoreTake(_camMutex, pdMS_TO_TICKS(3000))) {
        Serial.println("[power] wake: mutex timeout");
        return false;
    }

    if (_powerState == POWER_AWAKE) {
        _lastActivity = millis();
        xSemaphoreGive(_camMutex);
        return true;
    }

    // F2: if SHUTTING_DOWN, abort the sleep by going back to AWAKE
    if (_powerState == POWER_SHUTTING_DOWN) {
        Serial.println("[power] wake interrupted shutdown");
        _powerState = POWER_AWAKE;
        _lastActivity = millis();
        xSemaphoreGive(_camMutex);
        return true;
    }

    // POWER_SLEEPING → full wake
    Serial.println("[power] waking — camera + LiDAR on, WiFi full power");

    esp_wifi_set_ps(WIFI_PS_NONE);

    digitalWrite(PWDN_GPIO_NUM, LOW);
    vTaskDelay(pdMS_TO_TICKS(10));

    if (!init_camera()) {
        Serial.println("[power] WARNING: camera reinit failed — reverting to sleep");
        digitalWrite(PWDN_GPIO_NUM, HIGH);     // power camera back down
        esp_wifi_set_ps(WIFI_PS_MIN_MODEM);    // revert WiFi to modem sleep
        xSemaphoreGive(_camMutex);
        return false;
    }

    lidar_flush();  // discard stale UART data

    _powerState   = POWER_AWAKE;
    _lastActivity = millis();
    xSemaphoreGive(_camMutex);
    return true;
}

// ─── Single JPEG snapshot ────────────────────────────────────────────
static void handle_capture(AsyncWebServerRequest* request) {
    _lastActivity = millis();
    if (_powerState != POWER_AWAKE && !bow_wake()) {
        request->send(503, "text/plain", "Device busy");
        return;
    }

    // Bounded timeout — don't stall async web server task indefinitely
    if (!xSemaphoreTake(_camMutex, pdMS_TO_TICKS(2000))) {
        request->send(503, "text/plain", "Camera busy");
        return;
    }
    // R4: re-check state after acquiring mutex (TOCTOU protection)
    if (_powerState != POWER_AWAKE) {
        xSemaphoreGive(_camMutex);
        request->send(503, "text/plain", "Camera sleeping");
        return;
    }
    camera_fb_t* fb = esp_camera_fb_get();
    if (!fb) {
        xSemaphoreGive(_camMutex);
        request->send(500, "text/plain", "Camera capture failed");
        return;
    }

    AsyncResponseStream* stream = request->beginResponseStream("image/jpeg");
    stream->write(fb->buf, fb->len);
    esp_camera_fb_return(fb);
    xSemaphoreGive(_camMutex);

    request->send(stream);
}

// ─── Distance endpoint ───────────────────────────────────────────────
static void handle_distance(AsyncWebServerRequest* request) {
    _lastActivity = millis();
    if (_powerState != POWER_AWAKE && !bow_wake()) {
        request->send(503, "application/json", "{\"valid\":false}");
        return;
    }

    if (!xSemaphoreTake(_lidarMutex, pdMS_TO_TICKS(200))) {
        request->send(503, "application/json", "{\"valid\":false}");
        return;
    }
    LidarData ld = lidar_read();
    xSemaphoreGive(_lidarMutex);

    JsonDocument doc;
    doc["valid"] = ld.valid;
    if (ld.valid) {
        doc["distance_cm"] = (int)ld.distance_cm;
        doc["strength"]    = (int)ld.strength;
    }

    String out;
    serializeJson(doc, out);

    request->send(200, "application/json", out);
}

// ─── Status endpoint ─────────────────────────────────────────────────
static void handle_status(AsyncWebServerRequest* request) {
    JsonDocument doc;
    doc["uptime_s"]   = (millis() - _bootTime) / 1000;
    doc["wifi_clients"] = WiFi.softAPgetStationNum();
    doc["free_heap"]  = ESP.getFreeHeap();
    doc["sleeping"]   = (_powerState != POWER_AWAKE);
    doc["camera_ok"]  = (bool)_cameraOk;

    String out;
    serializeJson(doc, out);

    request->send(200, "application/json", out);
}

// ─── WiFi AP (bow hosts its own network) ─────────────────────────────
static void start_wifi_ap() {
    WiFi.mode(WIFI_AP);
    IPAddress ip; ip.fromString(BOW_AP_IP);
    IPAddress subnet(255, 255, 255, 0);
    WiFi.softAPConfig(ip, ip, subnet);
    WiFi.softAP(BOW_AP_SSID, BOW_AP_PASSWORD, BOW_AP_CHANNEL);
    esp_wifi_set_ps(WIFI_PS_NONE);
    esp_wifi_set_max_tx_power(82);  // 20.5 dBm max
    esp_wifi_set_protocol(WIFI_IF_AP, WIFI_PROTOCOL_11B | WIFI_PROTOCOL_11G | WIFI_PROTOCOL_11N);
    Serial.printf("[wifi] AP started: %s  IP: %s  ch: %d\n",
                  BOW_AP_SSID, WiFi.softAPIP().toString().c_str(), BOW_AP_CHANNEL);
}

// ─── Setup ───────────────────────────────────────────────────────────
void setup() {
    Serial.begin(115200);
    delay(500);
    _bootTime = _lastActivity = millis();
    Serial.println("\n[SusieQ-Bow] starting up...");

    _camMutex = xSemaphoreCreateMutex();
    _lidarMutex = xSemaphoreCreateMutex();
    if (!_camMutex || !_lidarMutex) {
        Serial.println("[bow] FATAL: mutex creation failed — halting");
        while (true) { delay(1000); }
    }

    // Boot in sleep state — wake on first request to save power
    _powerState = POWER_SLEEPING;
    pinMode(PWDN_GPIO_NUM, OUTPUT);
    digitalWrite(PWDN_GPIO_NUM, HIGH);  // camera powered down

    // Filesystem (serves /index.html)
    if (!LittleFS.begin(true)) {
        Serial.println("[fs] LittleFS mount failed!");
    } else {
        Serial.println("[fs] LittleFS mounted");
    }

    lidar_init();
    start_wifi_ap();
    esp_wifi_set_ps(WIFI_PS_MIN_MODEM);  // modem sleep until first request

    // F8: CORS via DefaultHeaders — no need for per-handler addHeader
    DefaultHeaders::Instance().addHeader("Access-Control-Allow-Origin", "*");
    DefaultHeaders::Instance().addHeader("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
    DefaultHeaders::Instance().addHeader("Access-Control-Allow-Headers", "Content-Type");

    // Root: serve bow's own dashboard (camera + distance)
    server.on("/", HTTP_GET, [](AsyncWebServerRequest* req) {
        req->send(LittleFS, "/index.html", "text/html");
    });

    server.on("/capture",  HTTP_GET, handle_capture);
    server.on("/distance", HTTP_GET, handle_distance);
    server.on("/status",   HTTP_GET, handle_status);

    server.serveStatic("/", LittleFS, "/");

    server.on("/sleep", HTTP_POST, [](AsyncWebServerRequest* req) {
        if (bow_sleep()) {
            req->send(200, "application/json", "{\"sleeping\":true}");
        } else {
            req->send(503, "application/json", "{\"error\":\"busy\"}");
        }
    });

    server.on("/wake", HTTP_POST, [](AsyncWebServerRequest* req) {
        if (bow_wake()) {
            req->send(200, "application/json", "{\"sleeping\":false}");
        } else {
            req->send(503, "application/json", "{\"error\":\"busy\"}");
        }
    });

    server.onNotFound([](AsyncWebServerRequest* req) {
        if (req->method() == HTTP_OPTIONS) {
            req->send(204);
        } else {
            req->send(404, "text/plain", "Not found");
        }
    });

    server.begin();
    Serial.println("[http] bow server started on port 80");

    stream_server_init();

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

    Serial.printf("[SusieQ-Bow] ready — open http://%s on phone\n", BOW_AP_IP);
}

// ─── Loop ────────────────────────────────────────────────────────────
static unsigned long _lastLidarPoll = 0;
static const unsigned long LIDAR_POLL_MS = 1000 / LIDAR_READ_HZ;  // 50ms at 20Hz

void loop() {
    ArduinoOTA.handle();

    // Auto-sleep after idle timeout
    if (_powerState == POWER_AWAKE && (millis() - _lastActivity > SLEEP_TIMEOUT_MS)) {
        bow_sleep();
    }

    // Rate-limit lidar polling (4Hz) — uses own mutex, no camera contention
    if (_powerState == POWER_AWAKE && (millis() - _lastLidarPoll >= LIDAR_POLL_MS)) {
        if (xSemaphoreTake(_lidarMutex, pdMS_TO_TICKS(50))) {
            lidar_read();
            xSemaphoreGive(_lidarMutex);
            _lastLidarPoll = millis();
        }
    }

    delay(10);
}
