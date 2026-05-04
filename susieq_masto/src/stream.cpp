// MJPEG stream server — separate compilation unit to avoid
// HTTP_GET enum clash between ESPAsyncWebServer and esp_http_server.h

#include "stream.h"
#include <Arduino.h>
#include "esp_camera.h"
#include "esp_http_server.h"
#include <sys/socket.h>
#include <lwip/tcp.h>

// Shared types/state with main.cpp
#include "power_state.h"

extern volatile PowerState _powerState;
extern volatile unsigned long _lastActivity;
extern SemaphoreHandle_t _camMutex;
extern bool masto_wake();

static httpd_handle_t stream_httpd = NULL;

// ─── MJPEG stream handler ─────────────────────────────────────────────
#define STREAM_BOUNDARY "frame"
static const char* STREAM_CONTENT_TYPE =
    "multipart/x-mixed-replace; boundary=" STREAM_BOUNDARY;
static const char* STREAM_PART_FMT =
    "\r\n--" STREAM_BOUNDARY "\r\nContent-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n";

// PSRAM send buffer — header + JPEG combined into single TCP write
#define STREAM_BUF_SIZE 32768
static uint8_t* _streamBuf = NULL;

static esp_err_t stream_handler(httpd_req_t* req) {
    esp_err_t res = ESP_OK;

    if (_powerState != POWER_AWAKE && !masto_wake()) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Device busy");
        return ESP_FAIL;
    }
    _lastActivity = millis();

    if (!_streamBuf) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "No stream buffer");
        return ESP_FAIL;
    }

    httpd_resp_set_type(req, STREAM_CONTENT_TYPE);
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_set_hdr(req, "X-Framerate", "10");

    // TCP tuning for streaming
    int fd = httpd_req_to_sockfd(req);
    struct timeval tv = { .tv_sec = 3, .tv_usec = 0 };
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
    int nodelay = 1;
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &nodelay, sizeof(nodelay));

    int cam_fail_count = 0;
    while (true) {
        if (_powerState != POWER_AWAKE) break;

        if (!xSemaphoreTake(_camMutex, pdMS_TO_TICKS(50))) {
            taskYIELD();
            continue;
        }

        if (_powerState != POWER_AWAKE) {
            xSemaphoreGive(_camMutex);
            break;
        }

        camera_fb_t* fb = esp_camera_fb_get();
        if (!fb) {
            xSemaphoreGive(_camMutex);
            cam_fail_count++;
            if (cam_fail_count >= 20) break;  // 1s of failures — give up
            vTaskDelay(pdMS_TO_TICKS(50));
            continue;
        }
        cam_fail_count = 0;

        // Build header + JPEG into single buffer for one TCP write
        int headerLen = snprintf((char*)_streamBuf, 128, STREAM_PART_FMT, fb->len);
        size_t totalLen = headerLen + fb->len;

        if (totalLen <= STREAM_BUF_SIZE) {
            memcpy(_streamBuf + headerLen, fb->buf, fb->len);
            esp_camera_fb_return(fb);
            xSemaphoreGive(_camMutex);

            res = httpd_resp_send_chunk(req, (const char*)_streamBuf, totalLen);

            // Fixed 10fps pacing — gives WiFi AP enough headroom for smooth stream
            vTaskDelay(pdMS_TO_TICKS(100));
        } else {
            // Frame too large for buffer — release mutex first, then send in two chunks
            xSemaphoreGive(_camMutex);
            res = httpd_resp_send_chunk(req, (const char*)_streamBuf, headerLen);
            if (res == ESP_OK) {
                res = httpd_resp_send_chunk(req, (const char*)fb->buf, fb->len);
            }
            esp_camera_fb_return(fb);
            vTaskDelay(pdMS_TO_TICKS(100));
        }

        if (res != ESP_OK) break;

        _lastActivity = millis();
    }

    // Send zero-length chunk to properly terminate chunked response
    httpd_resp_send_chunk(req, NULL, 0);

    return res;
}

void stream_server_init() {
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 81;
    config.ctrl_port = 32769;
    config.stack_size = 8192;              // double default for chunked streaming
    config.core_id = 1;                    // Core 0 = WiFi stack, Core 1 = streaming
    config.task_priority = tskIDLE_PRIORITY + 3;  // above background tasks

    // Allocate combined send buffer in PSRAM
    if (!_streamBuf) {
        _streamBuf = (uint8_t*)ps_malloc(STREAM_BUF_SIZE);
        if (!_streamBuf) {
            _streamBuf = (uint8_t*)malloc(STREAM_BUF_SIZE);
        }
    }

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
