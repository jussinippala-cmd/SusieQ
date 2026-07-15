#include "colight.h"
#include "victron.h"
#include <Arduino.h>
#include <NimBLEDevice.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <cstring>
#include "colight_rtc.h"
#include <esp_attr.h>
#include <esp_system.h>

// colight_task_fn maintains a single persistent NimBLE connection to the
// panel for the whole firmware lifetime. Live BLE testing on the boat
// (2026-07-05, see docs/superpowers/specs/2026-07-05-colight-persistent-
// ble-cache-design.md) showed the panel only ever sends a state
// notification (0xf9 ...) in response to a physical switch change — never
// on connect/subscribe, and there is no known "query current state"
// command. A connect-per-request model can therefore never reliably read
// state. Instead this task stays connected (reconnecting automatically if
// dropped) and caches the last-seen frame in colight_cache; main.cpp's HTTP
// handlers only ever read/write that cache through colight_read_state()/
// colight_send_command() below — they never touch BLE directly and never
// block waiting for a notification.
#define COLIGHT_DEVICE_NAME          "MG-P1C-12P"
#define COLIGHT_SERVICE_UUID         "0003cbbb-0000-1000-8000-00805f9beff0"
#define COLIGHT_CHAR_UUID            "0003cbbb-0000-1000-8000-00805f9beffa"
#define COLIGHT_SCAN_S               3
#define COLIGHT_RECONNECT_DELAY_MS   5000
#define COLIGHT_RECONNECT_MAX_MS     60000
#define COLIGHT_IDLE_POLL_MS         1000
#define COLIGHT_MUTEX_TIMEOUT_MS     2000
#define COLIGHT_LOG_CAPACITY         40
#define COLIGHT_LOG_MSG_LEN          48
#define COLIGHT_LOG_MUTEX_TIMEOUT_MS 100

struct ColightCache {
    bool valid = false;              // has any real frame ever been seen?
    bool restored = false;           // frame came from RTC after a warm reboot
    uint8_t frame[6] = {};
    uint32_t last_updated_ms = 0;
    bool connected = false;
    NimBLEClient* client = nullptr;
    NimBLERemoteCharacteristic* chr = nullptr;
};

static SemaphoreHandle_t colight_mutex = nullptr;
static ColightCache colight_cache;
static NimBLEAdvertisedDevice* colight_found_device = nullptr;

// The last-seen state frame survives warm (sw/wdt/panic) reboots in RTC
// memory — same magic-sentinel pattern as main.cpp's boot_count/
// restart_note. Poweron/brownout leaves RTC undefined AND power-cycles the
// panel itself (shared main switch), so restore is gated on reset reason in
// colight_init(). Validation logic lives in colight_rtc.cpp (unit-tested).
RTC_NOINIT_ATTR static uint8_t  colight_rtc_frame[6];
RTC_NOINIT_ATTR static uint32_t colight_rtc_magic;
RTC_NOINIT_ATTR static uint32_t colight_rtc_check;

// Call with colight_mutex held, in the same critical section that updates
// colight_cache.frame — RTC can then never lag the cache.
static void colight_rtc_save(const uint8_t frame[6]) {
    memcpy(colight_rtc_frame, frame, 6);
    colight_rtc_check = colight_rtc_checksum(frame);
    colight_rtc_magic = COLIGHT_RTC_MAGIC;
}

// Small in-memory ring buffer of connect/disconnect/scan events, readable
// over HTTP (/colight-debug) so the reconnect behaviour can be inspected
// from a phone/laptop without a USB-serial connection to the ESP32. Guarded
// by its own mutex (never colight_mutex) so a slow log write can never
// contend with the state read/write hot path.
struct ColightLogEntry {
    uint32_t ms = 0;
    char msg[COLIGHT_LOG_MSG_LEN] = {};
};
static SemaphoreHandle_t colight_log_mutex = nullptr;
static ColightLogEntry colight_log_buf[COLIGHT_LOG_CAPACITY];
static int colight_log_head = 0;   // next slot to write
static int colight_log_count = 0;  // valid entries, caps at COLIGHT_LOG_CAPACITY

static void colight_log(const char* msg) {
    Serial.printf("[colight] %s\n", msg);
    if (!colight_log_mutex || !xSemaphoreTake(colight_log_mutex, pdMS_TO_TICKS(COLIGHT_LOG_MUTEX_TIMEOUT_MS))) {
        return;
    }
    ColightLogEntry& e = colight_log_buf[colight_log_head];
    e.ms = millis();
    strncpy(e.msg, msg, COLIGHT_LOG_MSG_LEN - 1);
    e.msg[COLIGHT_LOG_MSG_LEN - 1] = '\0';
    colight_log_head = (colight_log_head + 1) % COLIGHT_LOG_CAPACITY;
    if (colight_log_count < COLIGHT_LOG_CAPACITY) colight_log_count++;
    xSemaphoreGive(colight_log_mutex);
}

// Takes colight_mutex with the standard bounded wait. Returns false on
// timeout — or if the mutex was never created (allocation failure in
// colight_init) — and logs the failing site to both Serial and the debug
// ring buffer, so a missed acquisition is visible in /colight-debug, not
// just on USB-serial. Logging goes through colight_log_mutex, never
// colight_mutex, so this can never deadlock with the state hot path.
static bool colight_lock(const char* where) {
    if (colight_mutex && xSemaphoreTake(colight_mutex, pdMS_TO_TICKS(COLIGHT_MUTEX_TIMEOUT_MS))) {
        return true;
    }
    char msg[COLIGHT_LOG_MSG_LEN];
    snprintf(msg, sizeof(msg), "%s: mutex timeout", where);
    colight_log(msg);
    return false;
}

class ColightScanCB : public NimBLEAdvertisedDeviceCallbacks {
    void onResult(NimBLEAdvertisedDevice* dev) override {
        if (dev->haveName() && dev->getName() == COLIGHT_DEVICE_NAME) {
            colight_found_device = dev;
            NimBLEDevice::getScan()->stop();
        }
    }
};
static ColightScanCB colight_scan_cb;

// Runs on NimBLE's host task when the panel drops the connection (out of
// range, power cycle, etc). Only flips the flag under the mutex — the
// actual client cleanup and reconnect happen in colight_task_fn, not here.
class ColightClientCB : public NimBLEClientCallbacks {
    void onDisconnect(NimBLEClient* client) override {
        // On lock timeout the flag update is skipped; colight_task_fn's
        // idle-branch reconciliation (isConnected() check) is the backstop
        // that eventually notices the dead link and reconnects.
        if (!colight_lock("onDisconnect")) {
            return;
        }
        colight_cache.connected = false;
        xSemaphoreGive(colight_mutex);
        colight_log("disconnected");
    }
};
static ColightClientCB colight_client_cb;

// Runs on NimBLE's host task for every notification/indication on the
// subscribed characteristic. Only f9 frames (state reports) are real state
// — f1-f4 are other frame types the panel also sends in the same burst,
// ignored here (see tools/colight-ble/FINDINGS.md).
static void colight_notify_handler(NimBLERemoteCharacteristic* c, uint8_t* pData,
                                    size_t length, bool isNotify) {
    if (length >= 7 && pData[0] == 0xf9) {
        if (!colight_lock("notify_handler")) {
            return;  // frame dropped; panel resends on the next state change
        }
        memcpy(colight_cache.frame, pData + 1, 6);
        colight_cache.valid = true;
        colight_cache.restored = false;  // a real frame supersedes a restored one
        colight_cache.last_updated_ms = millis();
        colight_rtc_save(colight_cache.frame);
        xSemaphoreGive(colight_mutex);
    }
}

// Scans for the panel, connects, discovers the service/characteristic, and
// subscribes to notifications. On success, returns true with *out_client/
// *out_chr set. On any failure, cleans up its own partial state (deletes
// the client it created, if any) and returns false with both outputs left
// untouched — caller retries after a delay.
static bool colight_connect_once(NimBLEClient** out_client, NimBLERemoteCharacteristic** out_chr) {
    uint32_t started = millis();
    colight_found_device = nullptr;
    NimBLEScan* scan = NimBLEDevice::getScan();
    scan->setAdvertisedDeviceCallbacks(&colight_scan_cb, true);
    scan->setActiveScan(true);
    scan->start(COLIGHT_SCAN_S, nullptr, false);

    while (!colight_found_device && millis() - started < (uint32_t)(COLIGHT_SCAN_S * 1000 + 500)) {
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    if (!colight_found_device) {
        colight_log("scan: panel not found (not advertising?)");
        return false;
    }
    char found_msg[COLIGHT_LOG_MSG_LEN];
    snprintf(found_msg, sizeof(found_msg), "scan: found panel, rssi=%d", colight_found_device->getRSSI());
    colight_log(found_msg);

    NimBLEClient* client = NimBLEDevice::createClient();
    client->setClientCallbacks(&colight_client_cb, false);
    client->setConnectTimeout(5);

    if (!client->connect(colight_found_device)) {
        colight_log("connect: gatt connect failed");
        NimBLEDevice::deleteClient(client);
        return false;
    }

    NimBLERemoteService* svc = client->getService(COLIGHT_SERVICE_UUID);
    NimBLERemoteCharacteristic* chr = svc ? svc->getCharacteristic(COLIGHT_CHAR_UUID) : nullptr;
    if (!chr || !chr->subscribe(true, colight_notify_handler)) {
        colight_log("connect: service/characteristic/subscribe failed");
        client->disconnect();
        NimBLEDevice::deleteClient(client);
        return false;
    }

    colight_log("connect: connected and subscribed");
    *out_client = client;
    *out_chr = chr;
    return true;
}

// Background task body: while connected, idles and reconciles the cached
// connected flag against the real link state (all real work happens in
// the notify/disconnect callbacks above, which run on NimBLE's own host
// task). While disconnected, cleans up any stale client, pauses Victron's
// scan for the duration of one (re)connect attempt, and retries with
// exponential backoff (COLIGHT_RECONNECT_DELAY_MS doubling up to
// COLIGHT_RECONNECT_MAX_MS) so an absent panel doesn't keep interrupting
// Victron's scan every few seconds indefinitely.
static void colight_task_fn(void* pvParameters) {
    uint32_t reconnect_delay_ms = COLIGHT_RECONNECT_DELAY_MS;
    for (;;) {
        bool connected;
        if (!colight_lock("task read")) {
            vTaskDelay(pdMS_TO_TICKS(COLIGHT_RECONNECT_DELAY_MS));
            continue;
        }
        connected = colight_cache.connected;
        xSemaphoreGive(colight_mutex);

        if (connected) {
            vTaskDelay(pdMS_TO_TICKS(COLIGHT_IDLE_POLL_MS));
            // Reconciliation backstop: if onDisconnect ever misses its
            // bounded mutex wait, connected stays true with no reconnect —
            // permanently, since NimBLE fires onDisconnect only once. Verify
            // the flag against the real link state on every idle poll.
            // isConnected() only reads the connection handle, no BLE I/O.
            bool desynced = false;
            if (colight_lock("task reconcile")) {
                if (colight_cache.connected && colight_cache.client &&
                    !colight_cache.client->isConnected()) {
                    colight_cache.connected = false;
                    desynced = true;
                }
                xSemaphoreGive(colight_mutex);
            }
            if (desynced) {
                colight_log("reconcile: link down, cache said connected");
            }
            continue;
        }

        if (!colight_lock("task clean stale")) {
            vTaskDelay(pdMS_TO_TICKS(COLIGHT_RECONNECT_DELAY_MS));
            continue;
        }
        NimBLEClient* stale_client = colight_cache.client;
        colight_cache.client = nullptr;
        colight_cache.chr = nullptr;
        xSemaphoreGive(colight_mutex);
        if (stale_client) {
            NimBLEDevice::deleteClient(stale_client);
        }

        victron_pause_scan();
        NimBLEClient* new_client = nullptr;
        NimBLERemoteCharacteristic* new_chr = nullptr;
        bool ok = colight_connect_once(&new_client, &new_chr);
        victron_resume_scan();

        // colight_connect_once() can succeed and then the panel can drop the
        // link before we get here (e.g. right after subscribe). If that
        // happens, ColightClientCB::onDisconnect() already ran and set
        // connected=false, but since it ran *before* this task published
        // new_client/new_chr into colight_cache, no further onDisconnect will
        // ever fire for this client — publishing it as "connected" here would
        // leave the cache permanently stuck on a dead connection. Re-check
        // liveness under the same mutex acquisition that publishes the
        // result so the two can never race.
        if (!colight_lock("task publish")) {
            if (ok) {
                new_client->disconnect();
                NimBLEDevice::deleteClient(new_client);
            }
            vTaskDelay(pdMS_TO_TICKS(COLIGHT_RECONNECT_DELAY_MS));
            continue;
        }
        bool published = ok && new_client->isConnected();
        colight_cache.connected = published;
        NimBLEClient* client_to_discard = nullptr;
        if (published) {
            colight_cache.client = new_client;
            colight_cache.chr = new_chr;
        } else if (ok) {
            // Connected during colight_connect_once() but already dropped by
            // the time we got the mutex — discard it rather than leaking it.
            client_to_discard = new_client;
        }
        xSemaphoreGive(colight_mutex);
        if (client_to_discard) {
            NimBLEDevice::deleteClient(client_to_discard);
        }

        if (published) {
            reconnect_delay_ms = COLIGHT_RECONNECT_DELAY_MS;
        } else {
            vTaskDelay(pdMS_TO_TICKS(reconnect_delay_ms));
            reconnect_delay_ms *= 2;
            if (reconnect_delay_ms > COLIGHT_RECONNECT_MAX_MS) {
                reconnect_delay_ms = COLIGHT_RECONNECT_MAX_MS;
            }
        }
    }
}

void colight_init() {
    colight_mutex = xSemaphoreCreateMutex();
    colight_log_mutex = xSemaphoreCreateMutex();
    if (!colight_mutex || !colight_log_mutex) {
        // Without the mutexes every cache access would pass a NULL handle to
        // xSemaphoreTake and crash — degrade to "colight disabled" instead.
        // colight_lock() and colight_log() both tolerate a null handle, and
        // read_state/send_command then report "busy"/"not_ready".
        Serial.println("[colight] FATAL: mutex allocation failed, colight disabled");
        return;
    }

    // Warm reboot (sw/wdt/panic) preserves RTC memory: restore the last
    // frame so the panel state (and remote control) survives the reboot.
    // Poweron/brownout resets the panel too (shared main switch) — start
    // from unknown_state exactly as before this feature.
    esp_reset_reason_t rr = esp_reset_reason();
    bool rtc_preserved = (rr == ESP_RST_SW || rr == ESP_RST_TASK_WDT ||
                          rr == ESP_RST_INT_WDT || rr == ESP_RST_WDT ||
                          rr == ESP_RST_PANIC);
    if (rtc_preserved &&
        colight_rtc_frame_valid(colight_rtc_magic, colight_rtc_check, colight_rtc_frame)) {
        memcpy(colight_cache.frame, colight_rtc_frame, 6);
        colight_cache.valid = true;
        colight_cache.restored = true;
        colight_log("boot: state restored from RTC");
    } else {
        colight_rtc_magic = 0;  // cold boot / garbage: never trust it later
    }

    xTaskCreate(colight_task_fn, "colight_task", 8192, nullptr, 1, nullptr);
}

String colight_get_log() {
    if (!colight_log_mutex || !xSemaphoreTake(colight_log_mutex, pdMS_TO_TICKS(COLIGHT_LOG_MUTEX_TIMEOUT_MS))) {
        return "busy";
    }
    // Formats directly from the ring buffer while holding the mutex — the
    // work is bounded (≤40 short lines, well under colight_log's 100 ms
    // timeout) and it avoids both a ~2 KB stack copy on the AsyncTCP task
    // and repeated String reallocations (reserve() makes it one allocation).
    String out;
    out.reserve(colight_log_count * (COLIGHT_LOG_MSG_LEN + 16));
    int start = (colight_log_head - colight_log_count + COLIGHT_LOG_CAPACITY) % COLIGHT_LOG_CAPACITY;
    char line[COLIGHT_LOG_MSG_LEN + 16];
    for (int i = 0; i < colight_log_count; i++) {
        const ColightLogEntry& e = colight_log_buf[(start + i) % COLIGHT_LOG_CAPACITY];
        snprintf(line, sizeof(line), "%lu %s\n", (unsigned long)e.ms, e.msg);
        out += line;
    }
    xSemaphoreGive(colight_log_mutex);
    return out;
}

ColightResult colight_read_state() {
    ColightResult result;
    if (!colight_lock("read_state")) {
        result.error = "busy";
        return result;
    }
    result.connected = colight_cache.connected;
    result.last_updated_ms = colight_cache.last_updated_ms;
    result.restored = colight_cache.restored;
    bool valid = colight_cache.valid;
    uint8_t frame[6];
    memcpy(frame, colight_cache.frame, 6);
    xSemaphoreGive(colight_mutex);

    if (!valid) {
        result.error = "unknown_state";
        return result;
    }

    colight_decode(frame, result.channels);
    result.success = true;
    return result;
}

ColightResult colight_send_command(int channel, bool turn_on) {
    ColightResult result;
    if (!colight_lock("send_command")) {
        result.error = "busy";
        return result;
    }
    result.connected = colight_cache.connected;
    result.last_updated_ms = colight_cache.last_updated_ms;
    result.restored = colight_cache.restored;

    if (!colight_cache.valid || !colight_cache.connected || !colight_cache.chr) {
        xSemaphoreGive(colight_mutex);
        result.error = "not_ready";
        return result;
    }

    uint8_t frame[6];
    memcpy(frame, colight_cache.frame, 6);
    colight_set_channel(frame, channel, turn_on);

    uint8_t out[7];
    out[0] = 0xf5;
    memcpy(out + 1, frame, 6);

    // write-without-response (last arg false): returns as soon as the local
    // stack accepts the write, no host-task round-trip, so it's safe to hold
    // colight_mutex across this call. If this is ever changed to request a
    // response, this becomes a blocking round-trip and would stall any
    // concurrent /colight/state read for the duration.
    bool written = colight_cache.chr->writeValue(out, 7, false);

    if (!written) {
        xSemaphoreGive(colight_mutex);
        result.error = "write_failed";
        return result;
    }

    memcpy(colight_cache.frame, frame, 6);
    colight_cache.last_updated_ms = millis();
    colight_rtc_save(frame);
    result.last_updated_ms = colight_cache.last_updated_ms;
    xSemaphoreGive(colight_mutex);

    colight_decode(frame, result.channels);
    result.success = true;
    return result;
}
