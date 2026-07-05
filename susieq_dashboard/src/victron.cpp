#include "victron.h"
#include "../include/config.h"

// ─── Victron SmartSolar MPPT BLE Advertisement Parser ─────────────────
// SmartSolar 75|15 uses AES-128-CTR encryption (NOT CCM).
//
// Packet layout (manufacturer data, company ID 0x02E1):
//   d[0-1]  company ID
//   d[2-3]  model ID
//   d[4]    record type (0x75 = SmartSolar via VE.Direct Bluetooth Dongle)
//   d[5-6]  constant (0xA0 0x01)
//   d[7-8]  IV counter (uint16 LE, changes every advertisement)
//   d[9]    key check byte — must equal key[0]
//   d[10:]  12-byte AES-CTR-encrypted payload
//
// CTR keystream: AES-ECB(key, {iv_lo, iv_hi, 0x00 × 14})
//
// Decrypted 12-byte binary struct:
//   [0]     device_state   (3=bulk 4=absorb 5=float 0=off 2=fault)
//   [1]     charger_error
//   [2:4]   battery_voltage  int16 LE, ÷100 → V
//   [4:6]   battery_current  int16 LE, ÷10  → A
//   [6:8]   yield_today      uint16 LE, ÷100 → Wh
//   [8:10]  pv_power         uint16 LE, W
//   [10:12] load_current     uint16 LE, ÷10  → A

// ─── 12V lead-acid/AGM voltage → SOC lookup table (21 points, 5% steps) ──
static const float soc_v_table[21] = {
    11.51f, 11.66f, 11.81f, 11.96f, 12.00f,  //  0– 20 %
    12.06f, 12.12f, 12.20f, 12.32f, 12.42f,  // 25– 45 %
    12.50f, 12.60f, 12.70f, 12.79f, 12.88f,  // 50– 70 %
    12.98f, 13.05f, 13.11f, 13.20f, 13.30f,  // 75– 95 %
    13.40f                                     // 100 %
};

float battery_voltage_to_soc(float v) {
    if (v <= soc_v_table[0])  return 0.0f;
    if (v >= soc_v_table[20]) return 100.0f;
    for (int i = 0; i < 20; i++) {
        if (v >= soc_v_table[i] && v < soc_v_table[i + 1]) {
            float frac = (v - soc_v_table[i]) / (soc_v_table[i + 1] - soc_v_table[i]);
            return (i + frac) * 5.0f;
        }
    }
    return 0.0f;
}

#ifndef VICTRON_ENABLED
  #define VICTRON_ENABLED 1
#endif

#if VICTRON_ENABLED

#include <NimBLEDevice.h>
#include <mbedtls/aes.h>

#define VICTRON_MANUFACTURER_ID  0x02E1
#define STALE_TIMEOUT_MS         30000

static VictronData  latest;
static VictronDebug dbg;
static portMUX_TYPE victron_mux = portMUX_INITIALIZER_UNLOCKED;

// ── Parse 32-char hex string → 16-byte key ─────────────────────────────
static bool parse_key(const char* hex, uint8_t key[16]) {
    if (strlen(hex) < 32) return false;
    for (int i = 0; i < 16; i++) {
        char b[3] = { hex[i*2], hex[i*2+1], 0 };
        key[i] = (uint8_t)strtol(b, nullptr, 16);
    }
    return true;
}

// ── AES-128-CTR decrypt (single 16-byte block keystream) ───────────────
// counter_block = { iv_lo, iv_hi, 0x00 × 14 }
static bool decrypt_ctr(const uint8_t key[16], uint16_t iv,
                         const uint8_t* cipher, size_t len,
                         uint8_t* plain) {
    uint8_t counter[16] = {};
    counter[0] = iv & 0xFF;
    counter[1] = (iv >> 8) & 0xFF;

    uint8_t keystream[16];
    mbedtls_aes_context ctx;
    mbedtls_aes_init(&ctx);
    int ret = mbedtls_aes_setkey_enc(&ctx, key, 128);
    if (ret == 0)
        ret = mbedtls_aes_crypt_ecb(&ctx, MBEDTLS_AES_ENCRYPT, counter, keystream);
    mbedtls_aes_free(&ctx);
    if (ret != 0) return false;

    for (size_t i = 0; i < len && i < 16; i++)
        plain[i] = cipher[i] ^ keystream[i];
    return true;
}

// ── BLE scan callback ────────────────────────────────────────────────────
class VictronAdvCB : public NimBLEAdvertisedDeviceCallbacks {
    void onResult(NimBLEAdvertisedDevice* dev) override {
        if (!dev->haveManufacturerData()) return;
        std::string mfr = dev->getManufacturerData();
        if (mfr.size() < 8) return;

        const uint8_t* d = (const uint8_t*)mfr.data();
        uint16_t mfr_id = d[0] | (d[1] << 8);

        portENTER_CRITICAL(&victron_mux); dbg.ble_seen++; portEXIT_CRITICAL(&victron_mux);
        if (mfr_id != VICTRON_MANUFACTURER_ID) return;

        // MAC filter
        std::string mac = dev->getAddress().toString();
        std::string mac_flat;
        for (char c : mac) if (c != ':') mac_flat += tolower(c);

        portENTER_CRITICAL(&victron_mux);
        dbg.victron_seen++;
        uint8_t copy_len = mfr.size() < 32 ? mfr.size() : 32;
        memcpy(dbg.last_pkt, d, copy_len);
        dbg.last_pkt_len = copy_len;
        strncpy(dbg.last_mac, mac.c_str(), sizeof(dbg.last_mac) - 1);
        portEXIT_CRITICAL(&victron_mux);

        std::string target_mac = VICTRON_MAC;
        if (target_mac.length() == 12 && mac_flat != target_mac) return;

        // Need at least: 7 header + 1 key_check + 12 ciphertext = 20 bytes
        if (mfr.size() < 20) return;

        // Parse key
        uint8_t key[16];
        if (!parse_key(VICTRON_KEY, key)) return;

        // Key check: d[9] must match key[0]
        if (d[9] != key[0]) {
            portENTER_CRITICAL(&victron_mux); dbg.decrypt_fail++; portEXIT_CRITICAL(&victron_mux);
            return;
        }

        // IV from d[7:9]
        uint16_t iv = d[7] | (d[8] << 8);

        // Ciphertext starts at d[10], length = (mfr.size() - 10), max 12
        size_t cipher_len = mfr.size() - 10;
        if (cipher_len > 12) cipher_len = 12;

        uint8_t plain[12] = {};
        if (!decrypt_ctr(key, iv, d + 10, cipher_len, plain)) {
            portENTER_CRITICAL(&victron_mux); dbg.decrypt_fail++; portEXIT_CRITICAL(&victron_mux);
            return;
        }
        portENTER_CRITICAL(&victron_mux); dbg.decrypt_ok++; portEXIT_CRITICAL(&victron_mux);

        // Parse fixed binary struct
        int16_t raw_batt_v = (int16_t)(plain[2] | (plain[3] << 8));
        int16_t raw_batt_i = (int16_t)(plain[4] | (plain[5] << 8));
        uint16_t raw_pv_p  = plain[8] | (plain[9] << 8);

        portENTER_CRITICAL(&victron_mux);
        latest.charge_state    = plain[0];
        latest.battery_voltage = raw_batt_v / 100.0f;
        latest.battery_current = raw_batt_i / 10.0f;
        latest.pv_power_w      = (float)raw_pv_p;
        latest.pv_voltage      = 0.0f;  // not in this packet format
        latest.valid           = true;
        latest.last_seen_ms    = millis();
        portEXIT_CRITICAL(&victron_mux);
    }
};

static VictronAdvCB advCB;

void victron_init() {
    if (strlen(VICTRON_KEY) < 32) {
        Serial.println("[victron] WARNING: VICTRON_KEY not set — BLE scan disabled");
        return;
    }
    NimBLEDevice::init("");
    NimBLEScan* scan = NimBLEDevice::getScan();
    scan->setAdvertisedDeviceCallbacks(&advCB, true);
    scan->setActiveScan(false);
    scan->setInterval(100);
    scan->setWindow(99);
    scan->start(0, nullptr, false);
    Serial.println("[victron] BLE scan started (AES-CTR)");
}

void victron_pause_scan() {
    NimBLEDevice::getScan()->stop();
}

void victron_resume_scan() {
    NimBLEScan* scan = NimBLEDevice::getScan();
    scan->setAdvertisedDeviceCallbacks(&advCB, true);
    scan->setActiveScan(false);
    scan->setInterval(100);
    scan->setWindow(99);
    scan->start(0, nullptr, false);
}

VictronData victron_get() {
    portENTER_CRITICAL(&victron_mux);
    VictronData copy = latest;
    portEXIT_CRITICAL(&victron_mux);

    if (copy.valid && (millis() - copy.last_seen_ms > STALE_TIMEOUT_MS)) {
        copy.valid = false;
        portENTER_CRITICAL(&victron_mux);
        latest.valid = false;
        portEXIT_CRITICAL(&victron_mux);
    }
    return copy;
}

VictronDebug victron_debug_get() {
    portENTER_CRITICAL(&victron_mux);
    VictronDebug copy = dbg;
    portEXIT_CRITICAL(&victron_mux);
    return copy;
}

#else

void victron_init() {
    Serial.println("[victron] disabled (no VICTRON_KEY set)");
}
VictronData victron_get() { return VictronData{}; }
VictronDebug victron_debug_get() { return VictronDebug{}; }
void victron_pause_scan() {}
void victron_resume_scan() {}

#endif
