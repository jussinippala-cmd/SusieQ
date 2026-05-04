#pragma once

// ─── Yksikön valinta (platformio.ini build_flags) ────────────────────
// -DMASTO_KEULA  →  192.168.8.101, susieq-masto-keula
// -DMASTO_PERA   →  192.168.8.102, susieq-masto-pera
#if defined(MASTO_KEULA)
  #define STATIC_IP     "192.168.8.101"
  #define OTA_HOSTNAME  "susieq-masto-keula"
#elif defined(MASTO_PERA)
  #define STATIC_IP     "192.168.8.102"
  #define OTA_HOSTNAME  "susieq-masto-pera"
#else
  #error "Maarittele -DMASTO_KEULA tai -DMASTO_PERA platformio.ini:ssa"
#endif

// ─── WiFi STA — yhdistetään GL-XE300:n verkkoon ─────────────────────
// Mastokamerat eivät luo omaa AP:ta — GL-XE300 pollaa /capture on-demand
#define STA_SSID      "SusieQ-Net"
#define STA_PASSWORD  "susieq123"
#define GATEWAY_IP    "192.168.8.1"
#define SUBNET_MASK   "255.255.255.0"

#define OTA_PASSWORD  "susieq_ota"

// ─── Kamera ──────────────────────────────────────────────────────────
#define CAM_FRAMESIZE  FRAMESIZE_QVGA
#define CAM_QUALITY    25

// ─── Virrankulutus: pehmeä unitila ───────────────────────────────────
// Kamera sammutetaan 30s idle-ajan jälkeen; WiFi STA pysyy päällä.
// GL-XE300:n /capture-pyyntö herättää kameran automaattisesti.
#define SLEEP_TIMEOUT_MS  30000
