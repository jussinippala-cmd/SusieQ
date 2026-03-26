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

// VICTRON_ENABLED is set automatically based on VICTRON_KEY in config.h
#ifndef VICTRON_ENABLED
  #define VICTRON_ENABLED 0
#endif

#if VICTRON_ENABLED

#include <NimBLEDevice.h>
#include <mbedtls/ccm.h>

#define VICTRON_MANUFACTURER_ID  0x02E1   // Victron Energy BLE company ID
#define STALE_TIMEOUT_MS         30000    // data older than 30 s = stale

static VictronData latest;
static String target_name = VICTRON_NAME;

// ── AES-128-CCM decrypt helper ──────────────────────────────────────────
static bool decrypt_payload(const uint8_t* key_hex_str,
                             const uint8_t* nonce, size_t nonce_len,
                             const uint8_t* cipher, size_t cipher_len,
                             uint8_t* plain) {
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
        latest.valid       = true;
        latest.last_seen_ms = millis();
    }
};

static VictronAdvCB advCB;

void victron_init() {
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
    // Mark stale if we haven't seen an advert recently
    if (latest.valid && (millis() - latest.last_seen_ms > STALE_TIMEOUT_MS)) {
        latest.valid = false;
    }
    return latest;
}

#else  // VICTRON_KEY not set — stub implementation

void victron_init() {
    Serial.println("[victron] disabled (no VICTRON_KEY set)");
}

VictronData victron_get() {
    return VictronData{};
}

#endif
