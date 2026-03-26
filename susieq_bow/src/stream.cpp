// MJPEG stream server — separate compilation unit to avoid
// HTTP_GET enum clash between ESPAsyncWebServer and esp_http_server.h

#include "stream.h"
#include <Arduino.h>
#include "esp_camera.h"
#include "esp_http_server.h"

// Shared types/state with main.cpp
#include "power_state.h"

extern volatile PowerState _powerState;
extern volatile unsigned long _lastActivity;
extern SemaphoreHandle_t _camMutex;
extern bool bow_wake();

static httpd_handle_t stream_httpd = NULL;

// ─── MJPEG stream handler ─────────────────────────────────────────────
#define STREAM_BOUNDARY "frame"
static const char* STREAM_CONTENT_TYPE =
    "multipart/x-mixed-replace; boundary=" STREAM_BOUNDARY;
static const char* STREAM_PART_HEADER =
    "\r\n--" STREAM_BOUNDARY "\r\nContent-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n";

static esp_err_t stream_handler(httpd_req_t* req) {
    esp_err_t res = ESP_OK;

    if (_powerState != POWER_AWAKE && !bow_wake()) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Device busy");
        return ESP_FAIL;
    }
    _lastActivity = millis();

    httpd_resp_set_type(req, STREAM_CONTENT_TYPE);
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_set_hdr(req, "X-Framerate", "10");

    while (true) {
        if (_powerState != POWER_AWAKE) break;

        if (!xSemaphoreTake(_camMutex, pdMS_TO_TICKS(200))) {
            continue;
        }

        if (_powerState != POWER_AWAKE) {
            xSemaphoreGive(_camMutex);
            break;
        }

        camera_fb_t* fb = esp_camera_fb_get();
        if (!fb) {
            xSemaphoreGive(_camMutex);
            Serial.println("[stream] capture failed");
            res = ESP_FAIL;
            break;
        }

        char header[128];
        int headerLen = snprintf(header, sizeof(header), STREAM_PART_HEADER, fb->len);
        if (headerLen >= (int)sizeof(header)) headerLen = sizeof(header) - 1;

        res = httpd_resp_send_chunk(req, header, headerLen);
        if (res == ESP_OK) {
            res = httpd_resp_send_chunk(req, (const char*)fb->buf, fb->len);
        }
        esp_camera_fb_return(fb);
        xSemaphoreGive(_camMutex);

        if (res != ESP_OK) break;

        _lastActivity = millis();
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    return res;
}

void stream_server_init() {
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 81;
    config.ctrl_port = 32769;

    if (httpd_start(&stream_httpd, &config) == ESP_OK) {
        httpd_uri_t stream_uri = {};
        stream_uri.uri     = "/stream";
        stream_uri.method  = HTTP_GET;
        stream_uri.handler = stream_handler;
        httpd_register_uri_handler(stream_httpd, &stream_uri);
        Serial.println("[http] stream server started on port 81");
    } else {
        Serial.println("[http] FAILED to start stream server on port 81");
    }
}
