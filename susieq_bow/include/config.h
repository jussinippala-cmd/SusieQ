#pragma once

// ─── WiFi STA — yhdistetään GL-XE300:n SusieQ-Net-verkkoon ──────────
#define STA_SSID      "SusieQ-Net"
#define STA_PASSWORD  "susieq123"
#define STATIC_IP     "192.168.8.103"
#define GATEWAY_IP    "192.168.8.1"
#define SUBNET_MASK   "255.255.255.0"

// ─── TF-Luna LiDAR (UART) ───────────────────────────────────────────
// ESP32-CAM: GPIO 14/15 are free when SD card is not used
#define LIDAR_RX_PIN     14    // ESP32 RX ← TF-Luna TX
#define LIDAR_TX_PIN     15    // ESP32 TX → TF-Luna RX
#define LIDAR_BAUD       115200
#define LIDAR_READ_HZ    4     // 4Hz is plenty for distance display; reduces CPU/interrupt load
#define LIDAR_OFFSET_CM  0     // mounting offset from bow tip (cm) — adjust after installation

// ─── OTA (Over The Air) firmware update ──────────────────────────────
#define OTA_HOSTNAME   "susieq-bow"
#define OTA_PASSWORD   "susieq_ota"    // change before deployment

// ─── Camera ─────────────────────────────────────────────────────────
// OV2640 on ESP32-CAM AI-Thinker board — pin mapping is handled
// by esp_camera with CAMERA_MODEL_AI_THINKER defines.
#define CAM_FRAMESIZE    FRAMESIZE_QVGA   // 320x240 — voidaan nostaa VGA:han jos stream toimii hyvin
#define CAM_QUALITY      25               // JPEG quality 0-63 (lower = better, ~4-6KB/frame at QVGA)

// ─── Power management ────────────────────────────────────────────────
// Auto-sleep after this many ms without any /stream or /distance request.
// WiFi stays up; only camera and LiDAR are powered down.
#define SLEEP_TIMEOUT_MS  30000           // 30 s idle → sleep
