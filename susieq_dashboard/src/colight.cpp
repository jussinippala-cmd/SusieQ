#include "colight.h"
#include "victron.h"
#include <Arduino.h>
#include <NimBLEDevice.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <cstring>

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
#define COLIGHT_IDLE_POLL_MS         1000
#define COLIGHT_MUTEX_TIMEOUT_MS     2000

struct ColightCache {
    bool valid = false;              // has any real frame ever been seen?
    uint8_t frame[6] = {};
    uint32_t last_updated_ms = 0;
    bool connected = false;
    NimBLEClient* client = nullptr;
    NimBLERemoteCharacteristic* chr = nullptr;
};

static SemaphoreHandle_t colight_mutex = nullptr;
static ColightCache colight_cache;
static NimBLEAdvertisedDevice* colight_found_device = nullptr;

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
        if (!xSemaphoreTake(colight_mutex, pdMS_TO_TICKS(COLIGHT_MUTEX_TIMEOUT_MS))) {
            Serial.println("[colight] onDisconnect: mutex timeout, skipping");
            return;
        }
        colight_cache.connected = false;
        xSemaphoreGive(colight_mutex);
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
        if (!xSemaphoreTake(colight_mutex, pdMS_TO_TICKS(COLIGHT_MUTEX_TIMEOUT_MS))) {
            Serial.println("[colight] notify_handler: mutex timeout, dropping frame");
            return;
        }
        memcpy(colight_cache.frame, pData + 1, 6);
        colight_cache.valid = true;
        colight_cache.last_updated_ms = millis();
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
    if (!colight_found_device) return false;

    NimBLEClient* client = NimBLEDevice::createClient();
    client->setClientCallbacks(&colight_client_cb, false);
    client->setConnectTimeout(5);

    if (!client->connect(colight_found_device)) {
        NimBLEDevice::deleteClient(client);
        return false;
    }

    NimBLERemoteService* svc = client->getService(COLIGHT_SERVICE_UUID);
    NimBLERemoteCharacteristic* chr = svc ? svc->getCharacteristic(COLIGHT_CHAR_UUID) : nullptr;
    if (!chr || !chr->subscribe(true, colight_notify_handler)) {
        client->disconnect();
        NimBLEDevice::deleteClient(client);
        return false;
    }

    *out_client = client;
    *out_chr = chr;
    return true;
}

// Background task body: while connected, idles (all real work happens in
// the notify/disconnect callbacks above, which run on NimBLE's own host
// task). While disconnected, cleans up any stale client, pauses Victron's
// scan for the duration of one (re)connect attempt, and retries every
// COLIGHT_RECONNECT_DELAY_MS on failure.
static void colight_task_fn(void* pvParameters) {
    for (;;) {
        bool connected;
        if (!xSemaphoreTake(colight_mutex, pdMS_TO_TICKS(COLIGHT_MUTEX_TIMEOUT_MS))) {
            Serial.println("[colight] task: mutex timeout reading connected, retrying");
            vTaskDelay(pdMS_TO_TICKS(COLIGHT_RECONNECT_DELAY_MS));
            continue;
        }
        connected = colight_cache.connected;
        xSemaphoreGive(colight_mutex);

        if (connected) {
            vTaskDelay(pdMS_TO_TICKS(COLIGHT_IDLE_POLL_MS));
            continue;
        }

        if (!xSemaphoreTake(colight_mutex, pdMS_TO_TICKS(COLIGHT_MUTEX_TIMEOUT_MS))) {
            Serial.println("[colight] task: mutex timeout cleaning stale client, retrying");
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
        if (!xSemaphoreTake(colight_mutex, pdMS_TO_TICKS(COLIGHT_MUTEX_TIMEOUT_MS))) {
            Serial.println("[colight] task: mutex timeout publishing result, discarding connection");
            if (ok) {
                new_client->disconnect();
                NimBLEDevice::deleteClient(new_client);
            }
            vTaskDelay(pdMS_TO_TICKS(COLIGHT_RECONNECT_DELAY_MS));
            continue;
        }
        colight_cache.connected = ok && new_client->isConnected();
        NimBLEClient* client_to_discard = nullptr;
        if (colight_cache.connected) {
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

        if (!ok) {
            vTaskDelay(pdMS_TO_TICKS(COLIGHT_RECONNECT_DELAY_MS));
        }
    }
}

void colight_init() {
    colight_mutex = xSemaphoreCreateMutex();
    xTaskCreate(colight_task_fn, "colight_task", 8192, nullptr, 1, nullptr);
}

ColightResult colight_read_state() {
    ColightResult result;
    xSemaphoreTake(colight_mutex, portMAX_DELAY);
    result.connected = colight_cache.connected;
    result.last_updated_ms = colight_cache.last_updated_ms;
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
    xSemaphoreTake(colight_mutex, portMAX_DELAY);
    result.connected = colight_cache.connected;
    result.last_updated_ms = colight_cache.last_updated_ms;

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
    result.last_updated_ms = colight_cache.last_updated_ms;
    xSemaphoreGive(colight_mutex);

    colight_decode(frame, result.channels);
    result.success = true;
    return result;
}
