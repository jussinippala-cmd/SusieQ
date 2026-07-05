#include "colight.h"
#include "victron.h"
#include <Arduino.h>
#include <NimBLEDevice.h>
#include <freertos/FreeRTOS.h>
#include <freertos/timers.h>
#include <cstring>

// colight_run() is called synchronously from an HTTP request handler, so it
// must never block indefinitely — the ESP32 has a 20s hardware watchdog and
// a stuck handler will trip a reboot.
//
// Worst-case bound with every step maxed out:
//   scan (~3.5s) + connect (~6s, setConnectTimeout(5) + NimBLE's internal
//   +1000ms grace) + GATT discovery (~3s max, force-aborted by the watchdog
//   timer below) + notify-wait (~3s) + write delay (~0.3s)
//   ≈ ~15.8s worst case — finite and bounded, under the 20s watchdog.
//
// This is higher than the original design target of ~6-8s. Further
// tightening (shorter scan/connect timeouts) is a possible follow-up if
// 15.8s proves too slow in practice on the boat, but a bounded ~16s is the
// priority fix here, not hitting 6-8s exactly.
#define COLIGHT_DEVICE_NAME    "MG-P1C-12P"
#define COLIGHT_SERVICE_UUID   "0003cbbb-0000-1000-8000-00805f9beff0"
#define COLIGHT_CHAR_UUID      "0003cbbb-0000-1000-8000-00805f9beffa"
#define COLIGHT_SCAN_S         3
#define COLIGHT_NOTIFY_WAIT_MS 3000

// getService()/getCharacteristic()/subscribe() internally wait on NimBLE
// task notifications with portMAX_DELAY and are NOT bounded by any
// existing timeout. A forced-disconnect watchdog timer (armed just before
// these calls) is the only thing that guarantees they return within this
// window if the panel connects but then stops answering GATT requests.
#define COLIGHT_DISCOVERY_TIMEOUT_MS 3000

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

static volatile bool colight_notify_ok = false;
static uint8_t colight_notify_frame[6];

static void colight_notify_handler(NimBLERemoteCharacteristic* c, uint8_t* pData,
                                    size_t length, bool isNotify) {
    if (length >= 7 && pData[0] == 0xf9) {
        memcpy(colight_notify_frame, pData + 1, 6);
        colight_notify_ok = true;
    }
}

void colight_init() {
    // NimBLE stack is already initialized by victron_init() in setup().
}

// Forced-disconnect watchdog for the unbounded GATT discovery calls
// (getService/getCharacteristic/subscribe). If discovery doesn't complete
// within COLIGHT_DISCOVERY_TIMEOUT_MS, the timer callback calls
// client->disconnect(), which unblocks NimBLE's internal
// ulTaskNotifyTake(pdTRUE, portMAX_DELAY) waits (with a failure result),
// letting colight_run()'s normal error paths take over instead of hanging
// forever.
static portMUX_TYPE colight_watchdog_mux = portMUX_INITIALIZER_UNLOCKED;
static NimBLEClient* colight_watchdog_client = nullptr;

static void colight_arm_watchdog(NimBLEClient* client) {
    portENTER_CRITICAL(&colight_watchdog_mux);
    colight_watchdog_client = client;
    portEXIT_CRITICAL(&colight_watchdog_mux);
}

// Reads and clears the armed client atomically. Returns it (or nullptr if
// already disarmed/never armed) so the caller can act on it outside the lock.
static NimBLEClient* colight_disarm_watchdog() {
    portENTER_CRITICAL(&colight_watchdog_mux);
    NimBLEClient* c = colight_watchdog_client;
    colight_watchdog_client = nullptr;
    portEXIT_CRITICAL(&colight_watchdog_mux);
    return c;
}

static void colight_watchdog_cb(TimerHandle_t timer) {
    NimBLEClient* c = colight_disarm_watchdog();
    if (c) {
        c->disconnect();  // called outside the critical section — never call NimBLE APIs while holding portENTER_CRITICAL
    }
}

// channel == 0 means "read only, do not write anything".
static ColightResult colight_run(int channel, bool turn_on) {
    ColightResult result;
    uint32_t started = millis();

    victron_pause_scan();

    colight_found_device = nullptr;
    NimBLEScan* scan = NimBLEDevice::getScan();
    scan->setAdvertisedDeviceCallbacks(&colight_scan_cb, true);
    scan->setActiveScan(true);
    scan->start(COLIGHT_SCAN_S, nullptr, false);

    while (!colight_found_device && millis() - started < (uint32_t)(COLIGHT_SCAN_S * 1000 + 500)) {
        delay(20);
    }

    if (!colight_found_device) {
        result.error = "scan_timeout";
        victron_resume_scan();
        return result;
    }

    NimBLEClient* client = NimBLEDevice::createClient();
    client->setConnectTimeout(5);

    if (!client->connect(colight_found_device)) {
        NimBLEDevice::deleteClient(client);
        result.error = "connect_failed";
        victron_resume_scan();
        return result;
    }

    TimerHandle_t discovery_timer = xTimerCreate("colight_wd", pdMS_TO_TICKS(COLIGHT_DISCOVERY_TIMEOUT_MS),
                                                  pdFALSE, nullptr, colight_watchdog_cb);
    colight_arm_watchdog(client);
    xTimerStart(discovery_timer, 0);

    NimBLERemoteService* svc = client->getService(COLIGHT_SERVICE_UUID);
    NimBLERemoteCharacteristic* chr = svc ? svc->getCharacteristic(COLIGHT_CHAR_UUID) : nullptr;
    bool subscribed = false;
    if (chr) {
        colight_notify_ok = false;
        subscribed = chr->subscribe(true, colight_notify_handler);
    }

    // The risky window is over one way or another — disarm before anything
    // else, so a late-firing timer never races a later deleteClient() call.
    xTimerStop(discovery_timer, pdMS_TO_TICKS(200));
    xTimerDelete(discovery_timer, pdMS_TO_TICKS(200));
    colight_disarm_watchdog();  // no-op if the callback already fired and cleared it

    if (!chr) {
        client->disconnect();
        NimBLEDevice::deleteClient(client);
        result.error = "characteristic_not_found";
        victron_resume_scan();
        return result;
    }

    if (!subscribed) {
        client->disconnect();
        NimBLEDevice::deleteClient(client);
        result.error = "subscribe_failed";
        victron_resume_scan();
        return result;
    }

    uint32_t wait_start = millis();
    while (!colight_notify_ok && millis() - wait_start < COLIGHT_NOTIFY_WAIT_MS) {
        delay(20);
    }

    if (!colight_notify_ok) {
        client->disconnect();
        NimBLEDevice::deleteClient(client);
        result.error = "no_state_notification";
        victron_resume_scan();
        return result;
    }

    uint8_t frame[6];
    memcpy(frame, colight_notify_frame, 6);

    if (channel != 0) {
        colight_set_channel(frame, channel, turn_on);
        if (!chr->writeValue(frame, 6, false)) {
            client->disconnect();
            NimBLEDevice::deleteClient(client);
            result.error = "write_failed";
            victron_resume_scan();
            return result;
        }
        delay(300);  // let the panel process the command
    }

    client->disconnect();
    NimBLEDevice::deleteClient(client);
    victron_resume_scan();

    colight_decode(frame, result.channels);
    result.success = true;
    return result;
}

ColightResult colight_read_state() {
    return colight_run(0, false);
}

ColightResult colight_send_command(int channel, bool turn_on) {
    return colight_run(channel, turn_on);
}
