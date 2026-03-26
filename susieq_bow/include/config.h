#pragma once

// ─── WiFi (connect to cockpit AP as STA) ─────────────────────────────
#define WIFI_SSID        "SusieQ-Data"
#define WIFI_PASSWORD    "susieq123"

// ─── Static IP for bow unit ──────────────────────────────────────────
#define BOW_IP           "192.168.4.10"
#define BOW_GATEWAY      "192.168.4.1"
#define BOW_SUBNET       "255.255.255.0"

// ─── TF-Luna LiDAR (UART) ───────────────────────────────────────────
// ESP32-CAM: GPIO 14/15 are free when SD card is not used
#define LIDAR_RX_PIN     14    // ESP32 RX ← TF-Luna TX
#define LIDAR_TX_PIN     15    // ESP32 TX → TF-Luna RX
#define LIDAR_BAUD       115200
#define LIDAR_READ_HZ    20    // TF-Luna default output rate

// ─── OTA (Over The Air) firmware update ──────────────────────────────
#define OTA_HOSTNAME   "susieq-bow"
#define OTA_PASSWORD   "susieq_ota"    // change before deployment

// ─── Camera ─────────────────────────────────────────────────────────
// OV2640 on ESP32-CAM AI-Thinker board — pin mapping is handled
// by esp_camera with CAMERA_MODEL_AI_THINKER defines.
#define CAM_FRAMESIZE    FRAMESIZE_VGA    // 640x480
#define CAM_QUALITY      12               // JPEG quality 0-63 (lower = better)

// ─── Power management ────────────────────────────────────────────────
// Auto-sleep after this many ms without any /stream or /distance request.
// WiFi stays up; only camera and LiDAR are powered down.
#define SLEEP_TIMEOUT_MS  30000           // 30 s idle → sleep
