#include "colight.h"
#include "victron.h"
#include <Arduino.h>
#include <NimBLEDevice.h>
#include <cstring>

#define COLIGHT_DEVICE_NAME    "MG-P1C-12P"
#define COLIGHT_SERVICE_UUID   "0003cbbb-0000-1000-8000-00805f9beff0"
#define COLIGHT_CHAR_UUID      "0003cbbb-0000-1000-8000-00805f9beffa"
#define COLIGHT_SCAN_S         3
#define COLIGHT_NOTIFY_WAIT_MS 3000
#define COLIGHT_OP_TIMEOUT_MS  8000

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

    NimBLERemoteService* svc = client->getService(COLIGHT_SERVICE_UUID);
    NimBLERemoteCharacteristic* chr = svc ? svc->getCharacteristic(COLIGHT_CHAR_UUID) : nullptr;

    if (!chr) {
        client->disconnect();
        NimBLEDevice::deleteClient(client);
        result.error = "characteristic_not_found";
        victron_resume_scan();
        return result;
    }

    colight_notify_ok = false;
    if (!chr->subscribe(true, colight_notify_handler)) {
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
