#pragma once

// ─── WiFi Access Point (bow hosts its own AP) ────────────────────────
// Phone connects directly to SusieQ-Bow when in harbor to view camera
// and distance sensor. Different subnet + channel from cockpit AP to
// avoid collisions.
#define BOW_AP_SSID      "SusieQ-Bow"
#define BOW_AP_PASSWORD  "susieq123"   // min 8 chars
#define BOW_AP_CHANNEL   11            // cockpit uses ch 6 — keep separated
#define BOW_AP_IP        "192.168.5.1" // different subnet from cockpit (4.x)

// ─── TF-Luna LiDAR (UART) ───────────────────────────────────────────
// ESP32-CAM: GPIO 14/15 are free when SD card is not used
#define LIDAR_RX_PIN     14    // ESP32 RX ← TF-Luna TX
#define LIDAR_TX_PIN     15    // ESP32 TX → TF-Luna RX
#define LIDAR_BAUD       115200
#define LIDAR_READ_HZ    20    // 20Hz — matches TF-Luna native output rate
#define LIDAR_OFFSET_CM  0     // mounting offset from bow tip (cm) — adjust after installation

// ─── OTA (Over The Air) firmware update ──────────────────────────────
#define OTA_HOSTNAME   "susieq-bow"
#define OTA_PASSWORD   "susieq_ota"    // change before deployment

// ─── Camera ─────────────────────────────────────────────────────────
// OV2640 on ESP32-CAM AI-Thinker board — pin mapping is handled
// by esp_camera with CAMERA_MODEL_AI_THINKER defines.
#define CAM_FRAMESIZE    FRAMESIZE_QVGA   // 320x240 — optimized for WiFi AP relay
#define CAM_QUALITY      25               // JPEG quality 0-63 (lower = better, ~4-6KB/frame at QVGA)

// ─── Power management ────────────────────────────────────────────────
// Auto-sleep after this many ms without any /stream or /distance request.
// WiFi stays up; only camera and LiDAR are powered down.
#define SLEEP_TIMEOUT_MS  30000           // 30 s idle → sleep
