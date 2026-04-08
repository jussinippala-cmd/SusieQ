#include "victron.h"
#include "../include/config.h"

// ─── Victron SmartSolar MPPT BLE Advertisement Parser ─────────────────
// Victron devices broadcast encrypted "Extra Manufacturer Data" BLE adverts.
// We use NimBLE-Arduino to scan passively and decode the packets.
//
// Protocol reference:
//   https://github.com/keshavdv/victron-ble  (Python reference)
//   https://github.com/Fabian-Schmidt/esphome-victron_ble (ESPHome port)
//
// IMPORTANT: You must set VICTRON_KEY in config.h with the 32-char hex
// encryption key from VictronConnect → Device → Product Info.
//
// Record IDs we care about (MPPT):
//   0x01 = Device state (charge state)
//   0x0B = Solar power (W × 1)
//   0x0D = Battery voltage (mV)
//   0x0E = Battery current (mA, signed)
//   0x15 = Panel voltage (mV)

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

// VICTRON_ENABLED is set in config.h (default 1)
#ifndef VICTRON_ENABLED
  #define VICTRON_ENABLED 1
#endif

#if VICTRON_ENABLED

#include <NimBLEDevice.h>
#include <mbedtls/ccm.h>

#define VICTRON_MANUFACTURER_ID  0x02E1   // Victron Energy BLE company ID
#define STALE_TIMEOUT_MS         30000    // data older than 30 s = stale

static VictronData latest;
static portMUX_TYPE victron_mux = portMUX_INITIALIZER_UNLOCKED;
static String target_name = VICTRON_NAME;

// ── AES-128-CCM decrypt helper ──────────────────────────────────────────
static bool decrypt_payload(const uint8_t* key_hex_str,
                             const uint8_t* nonce, size_t nonce_len,
                             const uint8_t* cipher, size_t cipher_len,
                             uint8_t* plain) {
    // Validate key length before accessing bytes
    if (strlen((const char*)key_hex_str) < 32) return false;

    // Convert 32-char hex key string to 16-byte binary
    uint8_t key[16];
    for (int i = 0; i < 16; i++) {
        char byte_str[3] = { (char)key_hex_str[i*2], (char)key_hex_str[i*2+1], 0 };
        key[i] = (uint8_t)strtol(byte_str, nullptr, 16);
    }

    mbedtls_ccm_context ctx;
    mbedtls_ccm_init(&ctx);
    int ret = mbedtls_ccm_setkey(&ctx, MBEDTLS_CIPHER_ID_AES, key, 128);
    if (ret == 0) {
        ret = mbedtls_ccm_auth_decrypt(&ctx,
            cipher_len - 4,          // ciphertext length (last 4 bytes = tag)
            nonce, nonce_len,
            nullptr, 0,              // no AAD
            cipher, plain,
            cipher + cipher_len - 4, 4); // tag
    }
    mbedtls_ccm_free(&ctx);
    return (ret == 0);
}

// ── BLE scan callback ────────────────────────────────────────────────────
class VictronAdvCB : public NimBLEAdvertisedDeviceCallbacks {
    void onResult(NimBLEAdvertisedDevice* dev) override {
        if (!dev->haveManufacturerData()) return;
        std::string mfr = dev->getManufacturerData();
        if (mfr.size() < 8) return;  // minimum: 2 mfr_id + 2 product + 1 rec_type + 2 nonce_ctr + 1+ data

        const uint8_t* d = (const uint8_t*)mfr.data();
        // Bytes 0-1: manufacturer ID (little-endian)
        uint16_t mfr_id = d[0] | (d[1] << 8);
        if (mfr_id != VICTRON_MANUFACTURER_ID) return;

        // Victron Extra Manufacturer Data layout:
        //   [0-1] manufacturer ID  [2] record type  [3-4] nonce/counter (LE)
        //   [5..N-4] ciphertext    [N-4..N] 4-byte AES-CCM tag
        //
        // Nonce (13 bytes for AES-128-CCM): counter_lo, counter_hi + 11 zero padding
        uint8_t rec_type = d[2];
        uint8_t nonce[13] = {};
        nonce[0] = d[3];   // counter low byte
        nonce[1] = d[4];   // counter high byte
        // bytes 2-12 remain zero (protocol spec)

        size_t   payload_len = mfr.size() - 5;   // ciphertext + tag start at offset 5
        if (payload_len < 5) return;  // need at least 1 byte data + 4 byte tag
        uint8_t  plain[32] = {};

        if (!decrypt_payload((const uint8_t*)VICTRON_KEY,
                             nonce, sizeof(nonce),
                             d + 5, payload_len,
                             plain)) return;
        (void)rec_type;  // used implicitly — payload format depends on device type

        // Parse plain-text records (TLV-style, 1-byte id, 1-byte len, N-byte val)
        size_t plain_len = payload_len - 4;  // subtract 4-byte tag
        size_t pos = 0;
        while (pos + 2 <= plain_len) {
            uint8_t id  = plain[pos++];
            uint8_t len = plain[pos++];
            if (id == 0xFF || pos + len > plain_len) break;
            uint32_t val = 0;
            for (int i = 0; i < len && i < 4; i++) val |= plain[pos+i] << (8*i);
            pos += len;

            switch (id) {
                case 0x01: latest.charge_state    = val & 0xFF; break;
                case 0x0B: latest.pv_power_w      = (float)val; break;
                case 0x0D: latest.battery_voltage = val / 1000.0f; break;
                case 0x0E: latest.battery_current = (int16_t)val / 1000.0f; break;
                case 0x15: latest.pv_voltage      = val / 1000.0f; break;
            }
        }
        portENTER_CRITICAL(&victron_mux);
        latest.valid       = true;
        latest.last_seen_ms = millis();
        portEXIT_CRITICAL(&victron_mux);
    }
};

static VictronAdvCB advCB;

void victron_init() {
    if (strlen(VICTRON_KEY) < 32) {
        Serial.println("[victron] WARNING: VICTRON_KEY not set or too short — BLE scan disabled");
        return;
    }
    NimBLEDevice::init("");
    NimBLEScan* scan = NimBLEDevice::getScan();
    scan->setAdvertisedDeviceCallbacks(&advCB, true);
    scan->setActiveScan(false);
    scan->setInterval(100);
    scan->setWindow(99);
    scan->start(0, nullptr, false);   // scan indefinitely (non-blocking)
    Serial.println("[victron] BLE scan started");
}

VictronData victron_get() {
    portENTER_CRITICAL(&victron_mux);
    VictronData copy = latest;
    portEXIT_CRITICAL(&victron_mux);

    // Mark stale if we haven't seen an advert recently
    if (copy.valid && (millis() - copy.last_seen_ms > STALE_TIMEOUT_MS)) {
        copy.valid = false;
        portENTER_CRITICAL(&victron_mux);
        latest.valid = false;
        portEXIT_CRITICAL(&victron_mux);
    }
    return copy;
}

#else  // VICTRON_KEY not set — stub implementation

void victron_init() {
    Serial.println("[victron] disabled (no VICTRON_KEY set)");
}

VictronData victron_get() {
    return VictronData{};
}

#endif
