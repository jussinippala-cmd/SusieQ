#include <Arduino.h>
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>
#include <ArduinoOTA.h>
#include <LittleFS.h>
#include "esp_camera.h"
#include "esp_wifi.h"
#include "../include/config.h"
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

#include "power_state.h"

// ─── Globals ──────────────────────────────────────────────────────────
AsyncWebServer server(80);
static unsigned long _bootTime       = 0;
volatile PowerState _powerState      = POWER_AWAKE;
volatile unsigned long _lastActivity = 0;
SemaphoreHandle_t _camMutex          = NULL;
static volatile bool _cameraOk       = false;

// ─── Camera init ──────────────────────────────────────────────────────
static bool init_camera() {
    esp_camera_deinit();

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
    if (s != NULL) s->set_raw_gma(s, 1);

    _cameraOk = true;
    return true;
}

// ─── Pehmeä unitila (kamera pois, WiFi STA pysyy) ────────────────────
static bool masto_sleep() {
    if (!xSemaphoreTake(_camMutex, pdMS_TO_TICKS(3000))) return false;

    if (_powerState != POWER_AWAKE) {
        xSemaphoreGive(_camMutex);
        return false;
    }
    _powerState = POWER_SHUTTING_DOWN;
    xSemaphoreGive(_camMutex);

    vTaskDelay(pdMS_TO_TICKS(50));

    if (!xSemaphoreTake(_camMutex, pdMS_TO_TICKS(3000))) {
        _powerState = POWER_AWAKE;
        return false;
    }
    if (_powerState != POWER_SHUTTING_DOWN) {
        xSemaphoreGive(_camMutex);
        return false;
    }

    esp_camera_deinit();
    pinMode(PWDN_GPIO_NUM, OUTPUT);
    digitalWrite(PWDN_GPIO_NUM, HIGH);

    _powerState = POWER_SLEEPING;
    xSemaphoreGive(_camMutex);
    Serial.println("[power] kamera pois — odotetaan pyyntoa");
    return true;
}

bool masto_wake() {
    if (!xSemaphoreTake(_camMutex, pdMS_TO_TICKS(3000))) return false;

    if (_powerState == POWER_AWAKE) {
        _lastActivity = millis();
        xSemaphoreGive(_camMutex);
        return true;
    }
    if (_powerState == POWER_SHUTTING_DOWN) {
        _powerState   = POWER_AWAKE;
        _lastActivity = millis();
        xSemaphoreGive(_camMutex);
        return true;
    }

    // POWER_SLEEPING → kamera takaisin päälle
    digitalWrite(PWDN_GPIO_NUM, LOW);
    vTaskDelay(pdMS_TO_TICKS(10));

    if (!init_camera()) {
        digitalWrite(PWDN_GPIO_NUM, HIGH);
        xSemaphoreGive(_camMutex);
        return false;
    }

    _powerState   = POWER_AWAKE;
    _lastActivity = millis();
    xSemaphoreGive(_camMutex);
    Serial.println("[power] kamera päälle");
    return true;
}

// ─── HTTP: yksittäinen JPEG-kuva ─────────────────────────────────────
static void handle_capture(AsyncWebServerRequest* request) {
    _lastActivity = millis();
    if (_powerState != POWER_AWAKE && !masto_wake()) {
        request->send(503, "text/plain", "Camera busy");
        return;
    }
    if (!xSemaphoreTake(_camMutex, pdMS_TO_TICKS(2000))) {
        request->send(503, "text/plain", "Camera busy");
        return;
    }
    if (_powerState != POWER_AWAKE) {
        xSemaphoreGive(_camMutex);
        request->send(503, "text/plain", "Camera sleeping");
        return;
    }
    camera_fb_t* fb = esp_camera_fb_get();
    if (!fb) {
        xSemaphoreGive(_camMutex);
        request->send(500, "text/plain", "Capture failed");
        return;
    }
    AsyncResponseStream* resp = request->beginResponseStream("image/jpeg");
    resp->write(fb->buf, fb->len);
    esp_camera_fb_return(fb);
    xSemaphoreGive(_camMutex);
    request->send(resp);
}

// ─── HTTP: tila ───────────────────────────────────────────────────────
static void handle_status(AsyncWebServerRequest* request) {
    JsonDocument doc;
    doc["ip"]       = WiFi.localIP().toString();
    doc["rssi"]     = WiFi.RSSI();
    doc["uptime_s"] = (millis() - _bootTime) / 1000;
    doc["free_heap"]= ESP.getFreeHeap();
    doc["sleeping"] = (_powerState != POWER_AWAKE);
    doc["camera_ok"]= (bool)_cameraOk;

    String out;
    serializeJson(doc, out);
    request->send(200, "application/json", out);
}

// ─── WiFi STA — yhdistyminen GL-XE300:n SusieQ-Net-verkkoon ─────────
static void start_wifi_sta() {
    IPAddress ip, gw, sn;
    ip.fromString(STATIC_IP);
    gw.fromString(GATEWAY_IP);
    sn.fromString(SUBNET_MASK);

    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(true);  // ESP32:n sisäinen automaattinen uudelleenyhdistys
    WiFi.config(ip, gw, sn);
    WiFi.begin(STA_SSID, STA_PASSWORD);

    unsigned long t = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - t < 15000) {
        delay(200);
    }

    if (WiFi.status() == WL_CONNECTED) {
        esp_wifi_set_ps(WIFI_PS_MIN_MODEM);  // virransäästö välissä
        Serial.printf("[wifi] yhdistetty: %s  RSSI: %d dBm\n",
                      WiFi.localIP().toString().c_str(), WiFi.RSSI());
    } else {
        Serial.println("[wifi] yhdistyminen epaonnistui — jatketaan silti");
    }
}

// ─── Setup ───────────────────────────────────────────────────────────
void setup() {
    Serial.begin(115200);
    delay(300);
    _bootTime = _lastActivity = millis();
    Serial.printf("\n[masto] boot — IP: %s\n", STATIC_IP);

    _camMutex = xSemaphoreCreateMutex();
    if (!_camMutex) {
        Serial.println("[masto] FATAL: mutex creation failed");
        while (true) delay(1000);
    }

    // Kamera sammutettu käynnistyksessä — herää ensimmäisestä pyynnöstä
    _powerState = POWER_SLEEPING;
    pinMode(PWDN_GPIO_NUM, OUTPUT);
    digitalWrite(PWDN_GPIO_NUM, HIGH);

    if (!LittleFS.begin(true)) {
        Serial.println("[fs] LittleFS mount failed");
    }

    start_wifi_sta();

    DefaultHeaders::Instance().addHeader("Access-Control-Allow-Origin", "*");
    DefaultHeaders::Instance().addHeader("Access-Control-Allow-Methods", "GET, OPTIONS");
    DefaultHeaders::Instance().addHeader("Access-Control-Allow-Headers", "Content-Type");

    server.on("/", HTTP_GET, [](AsyncWebServerRequest* req) {
        req->send(LittleFS, "/index.html", "text/html");
    });
    server.on("/capture", HTTP_GET, handle_capture);
    server.on("/status",  HTTP_GET, handle_status);
    server.serveStatic("/", LittleFS, "/");
    server.onNotFound([](AsyncWebServerRequest* req) {
        if (req->method() == HTTP_OPTIONS) req->send(204);
        else req->send(404, "text/plain", "Not found");
    });

    server.begin();
    stream_server_init();

    ArduinoOTA.setHostname(OTA_HOSTNAME);
    ArduinoOTA.setPassword(OTA_PASSWORD);
    ArduinoOTA.onEnd([]() { Serial.println("[ota] valmis — reboot"); });
    ArduinoOTA.begin();

    Serial.printf("[masto] valmis — http://%s/capture\n", STATIC_IP);
}

// ─── Loop ────────────────────────────────────────────────────────────
static unsigned long _lastWifiCheck = 0;

void loop() {
    ArduinoOTA.handle();

    // WiFi-uudelleenyhdistys jos yhteys katkennut (esim. modeemi lepotilassa)
    if (millis() - _lastWifiCheck > 30000) {
        _lastWifiCheck = millis();
        if (WiFi.status() != WL_CONNECTED) {
            Serial.println("[wifi] yhteys poikki — yritetaan uudelleen");
            WiFi.reconnect();
        } else {
            // Varmistaa modem sleepin — voi nollautua reconnectin jälkeen
            esp_wifi_set_ps(WIFI_PS_MIN_MODEM);
        }
    }

    // Kamera pois jos 30s ilman pyyntöjä
    if (_powerState == POWER_AWAKE && millis() - _lastActivity > SLEEP_TIMEOUT_MS) {
        masto_sleep();
    }

    delay(10);
}
