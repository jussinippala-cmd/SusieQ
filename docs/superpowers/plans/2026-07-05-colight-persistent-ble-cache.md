# CoLight Persistent BLE Cache Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the cockpit ESP32's connect-per-request CoLight BLE model (which can never reliably read panel state — see below) with a persistent background connection and a mutex-cached last-known state, so `/colight/state` and `/colight` respond instantly from cache instead of blocking on a notification that will basically never arrive within a request's lifetime.

**Architecture:** A new FreeRTOS task (`colight_task`) owns a single long-lived `NimBLEClient` connection to the panel for the firmware's whole lifetime, reconnecting automatically on drop. A mutex-protected cache (`ColightCache`) holds the last-seen 6-byte state frame, updated whenever a real `0xf9` notification arrives (from a physical switch touch or the panel's own echo of a write we sent). `main.cpp`'s HTTP handlers are unchanged in shape — they still call `colight_read_state()`/`colight_send_command()` — but those functions now only ever touch the cache, never BLE directly, so they return immediately instead of blocking for up to ~16s.

**Tech Stack:** PlatformIO / Arduino framework, NimBLE-Arduino (already a dependency), FreeRTOS tasks/semaphores (already used elsewhere in this firmware, e.g. `hx711_mutex`).

## Global Constraints

- Design spec (read before starting): `docs/superpowers/specs/2026-07-05-colight-persistent-ble-cache-design.md`.
- The panel (`MG-P1C-12P`) only sends a state notification (`0xf9` + 6 bytes) on a physical switch change — never on connect/subscribe, never on a poll. This is empirically confirmed (2026-07-05 live BLE monitor test), not a guess.
- Writes are full 6-byte state snapshots, not deltas — every write must be built from the cached frame with only the target channel's bit changed, never from a hardcoded baseline.
- `victron_pause_scan()`/`victron_resume_scan()` (already in `victron.h`/`victron.cpp`, unchanged) must bracket only the connect step, not every read/write — Victron's passive BLE scan must keep working continuously while the CoLight connection stays open.
- Cold start (`valid==false`, no frame seen yet) is an accepted, permanent-until-first-event state — do not attempt to synthesize or guess an initial frame.
- All cache mutations (`connected`, `valid`, `frame`, `client`, `chr`) must happen under the same mutex — this is what makes the design race-free between `colight_task` (reconnect/notify) and the AsyncTCP task (HTTP handlers calling `colight_send_command()`/`colight_read_state()`).
- Do not modify `susieq_dashboard/src/colight_protocol.h`/`.cpp` or its tests — the bit-manipulation logic is correct and unchanged.
- Do not modify `susieq_dashboard/src/victron.h`/`.cpp` — no functional change needed there.
- GL-XE300 relay daemon deployment is explicitly out of scope for this plan.

---

## File Structure

- Modify: `susieq_dashboard/src/colight.h` — new `ColightResult` fields (`connected`, `last_updated_ms`), updated doc comments; `colight_read_state()`/`colight_send_command()` signatures unchanged.
- Modify: `susieq_dashboard/src/colight.cpp` — full rewrite: persistent-connection task, mutex-guarded cache, notify/disconnect callbacks.
- Modify: `susieq_dashboard/src/main.cpp` — `colight_result_to_json()` gains `connected`/`last_updated_ms` fields (one place, used by both endpoints; the endpoint handlers themselves are unchanged).

---

### Task 1: Rewrite `colight.h`

**Files:**
- Modify: `susieq_dashboard/src/colight.h` (whole file — currently 23 lines)

**Interfaces:**
- Produces: `struct ColightResult { bool success; bool channels[COLIGHT_NUM_CHANNELS]; const char* error; bool connected; uint32_t last_updated_ms; }`, `void colight_init()`, `ColightResult colight_read_state()`, `ColightResult colight_send_command(int channel, bool turn_on)` — all consumed by `main.cpp` (Task 3) and implemented by `colight.cpp` (Task 2).

- [ ] **Step 1: Replace the file contents**

Replace the entire contents of `susieq_dashboard/src/colight.h` with:

```cpp
#pragma once
#include "colight_protocol.h"
#include <cstdint>

struct ColightResult {
    bool success = false;
    bool channels[COLIGHT_NUM_CHANNELS] = {};
    const char* error = "";        // set only when success == false
    bool connected = false;        // is the persistent BLE connection currently up?
    uint32_t last_updated_ms = 0;  // millis() of the last cached frame update
};

// Starts colight_task, the background FreeRTOS task that maintains a
// persistent BLE connection to the panel and keeps a cached copy of its
// last-reported state. Must be called after victron_init() (NimBLE is
// initialized there). The panel only ever reports state in response to a
// physical switch change — there is no way to request a fresh read on
// demand — so colight_read_state()/colight_send_command() below never talk
// to BLE directly; they only ever read/write the cache colight_task
// maintains. See docs/superpowers/specs/2026-07-05-colight-persistent-
// ble-cache-design.md for why.
void colight_init();

// Reads the cached state. Never blocks on BLE. success==false with
// error=="unknown_state" means no frame has been observed yet since boot
// (or since the last reconnect) — wait for a physical switch touch, or a
// colight_send_command() call, to populate it.
ColightResult colight_read_state();

// Builds a new 6-byte frame from the cached state with the given channel
// set/cleared (preserving every other channel), writes it over the
// persistent connection, and optimistically updates the cache with the new
// frame. channel is 1-indexed (1..12). Fails with error=="not_ready" if
// there is no cached baseline yet or the connection is currently down, or
// error=="write_failed" if the GATT write itself fails.
ColightResult colight_send_command(int channel, bool turn_on);
```

- [ ] **Step 2: Commit**

```bash
git add susieq_dashboard/src/colight.h
git commit -m "refactor(dashboard): redefine colight.h for persistent-connection cache model"
```

---

### Task 2: Rewrite `colight.cpp` — persistent connection task + cache

**Files:**
- Modify: `susieq_dashboard/src/colight.cpp` (whole file — currently 226 lines)

**Interfaces:**
- Consumes: `victron_pause_scan()`, `victron_resume_scan()` (from `victron.h`, unchanged signatures); `colight_decode(const uint8_t[6], bool[COLIGHT_NUM_CHANNELS])`, `colight_set_channel(uint8_t[6], int, bool)` (from `colight_protocol.h`, unchanged).
- Produces: implementations of `colight_init()`, `colight_read_state()`, `colight_send_command()` matching Task 1's `colight.h`.

This task can't be driven by a failing-test-first cycle — it depends on NimBLE and real BLE hardware, which don't run in the native test environment (same constraint the original CoLight bridge plan hit for this same file). Verification is: (a) the firmware builds, and (b) the manual on-boat checks in Task 5.

- [ ] **Step 1: Replace the file contents**

Replace the entire contents of `susieq_dashboard/src/colight.cpp` with:

```cpp
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
        xSemaphoreTake(colight_mutex, portMAX_DELAY);
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
        xSemaphoreTake(colight_mutex, portMAX_DELAY);
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
        xSemaphoreTake(colight_mutex, portMAX_DELAY);
        connected = colight_cache.connected;
        xSemaphoreGive(colight_mutex);

        if (connected) {
            vTaskDelay(pdMS_TO_TICKS(COLIGHT_IDLE_POLL_MS));
            continue;
        }

        xSemaphoreTake(colight_mutex, portMAX_DELAY);
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

        xSemaphoreTake(colight_mutex, portMAX_DELAY);
        colight_cache.connected = ok;
        if (ok) {
            colight_cache.client = new_client;
            colight_cache.chr = new_chr;
        }
        xSemaphoreGive(colight_mutex);

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
```

- [ ] **Step 2: Verify the firmware builds**

Run: `cd susieq_dashboard && pio run -e esp32dev-ota`
Expected: `[SUCCESS]`. Flash usage will be very close to the previous build (~1,185,477 bytes) — this change adds one task and a mutex, no new libraries.

- [ ] **Step 3: Commit**

```bash
git add susieq_dashboard/src/colight.cpp
git commit -m "refactor(dashboard): replace connect-per-request CoLight BLE with persistent connection + cache

The panel only ever sends a state notification on a physical switch
change, never on connect/subscribe or on a poll (confirmed live on the
boat 2026-07-05). The old connect-per-request model waited up to 3s for
a notification that would essentially never arrive within that window,
breaking both /colight/state reads and /colight writes. colight_task now
holds one persistent connection for the firmware's lifetime, reconnecting
automatically on drop, and caches the last-seen frame; the HTTP-facing
functions only ever touch that cache."
```

---

### Task 3: Update `main.cpp` — surface `connected`/`last_updated_ms` in JSON

**Files:**
- Modify: `susieq_dashboard/src/main.cpp:125-137` (the `colight_result_to_json` helper)

**Interfaces:**
- Consumes: `ColightResult` from Task 1 (`connected`, `last_updated_ms` fields).

The `/colight` and `/colight/state` route handlers (`main.cpp:283-302`) already call `colight_send_command()`/`colight_read_state()` and pass the result straight to this helper — no changes needed there, since `colight_read_state()`/`colight_send_command()` keep the exact same signatures.

- [ ] **Step 1: Update `colight_result_to_json`**

Find this function at `susieq_dashboard/src/main.cpp:125`:

```cpp
static String colight_result_to_json(const ColightResult& r) {
    JsonDocument doc;
    doc["success"] = r.success;
    if (r.success) {
        JsonArray arr = doc["state"].to<JsonArray>();
        for (int i = 0; i < COLIGHT_NUM_CHANNELS; i++) arr.add(r.channels[i]);
    } else {
        doc["error"] = r.error;
    }
    String out;
    serializeJson(doc, out);
    return out;
}
```

Replace it with:

```cpp
static String colight_result_to_json(const ColightResult& r) {
    JsonDocument doc;
    doc["success"] = r.success;
    doc["connected"] = r.connected;
    doc["last_updated_ms"] = r.last_updated_ms;
    if (r.success) {
        JsonArray arr = doc["state"].to<JsonArray>();
        for (int i = 0; i < COLIGHT_NUM_CHANNELS; i++) arr.add(r.channels[i]);
    } else {
        doc["error"] = r.error;
    }
    String out;
    serializeJson(doc, out);
    return out;
}
```

- [ ] **Step 2: Verify the firmware builds**

Run: `cd susieq_dashboard && pio run -e esp32dev-ota`
Expected: `[SUCCESS]`.

- [ ] **Step 3: Commit**

```bash
git add susieq_dashboard/src/main.cpp
git commit -m "feat(dashboard): surface colight connection/staleness fields in JSON"
```

---

### Task 4: Flash to the cockpit ESP32 over OTA

**Files:** none (deployment step)

Requires being on SusieQ-Net (per `CLAUDE.md`). The currently-installed firmware already includes the watchdog-OTA-feed fix (`b6ea679`), so a direct one-stage OTA is expected to work without needing the `tools/ota_bootstrap` two-stage dance from earlier this session — but if it fails with a ~20s watchdog reset again, fall back to that tool (`susieq_dashboard/tools/ota_bootstrap/`).

- [ ] **Step 1: Flash**

Run: `cd susieq_dashboard && pio run -e esp32dev-ota -t upload`
Expected: `[SUCCESS]`, completing in well under 20s (the fixed `fast_espota.py` from `susieq_dashboard/tools/ota_bootstrap/fast_espota.py` must still be installed at `~/.platformio/packages/framework-arduinoespressif32/tools/espota.py` — re-copy it if `pio pkg update` has run since and overwritten it).

- [ ] **Step 2: Confirm the new firmware is up**

Run: `curl -s -m 5 http://192.168.8.100/data | head -c 200`
Expected: sensor JSON (wind/battery/water/etc.), confirming the dashboard rebooted successfully.

- [ ] **Step 3: Confirm `colight_task` starts and (re)connects**

Run (wait ~10-15s after flashing first, to give the task time to scan+connect):
```bash
curl -s -m 5 http://192.168.8.100/colight/state
```
Expected: `{"success":false,"error":"unknown_state","connected":true,"last_updated_ms":0}` — `connected:true` confirms the persistent connection came up; `unknown_state`/`last_updated_ms:0` is expected and correct until a switch is touched (cold start, per design).

---

### Task 5: End-to-end verification on the boat

**Files:** none (manual verification)

- [ ] **Step 1: Physical switch → cache**

Toggle any physical switch on the panel (e.g. INTERIOR), then immediately run:
```bash
curl -s -m 5 http://192.168.8.100/colight/state
```
Expected: `success:true`, `state` array reflecting the new channel value, within ~1-2s of the toggle (no more multi-second wait or `no_state_notification` errors).

- [ ] **Step 2: Write path — flip a channel from the ESP32 directly (bypassing Supabase/GL-XE300)**

```bash
curl -s -m 5 -X POST "http://192.168.8.100/colight?channel=10&action=on"
curl -s -m 5 http://192.168.8.100/colight/state
curl -s -m 5 -X POST "http://192.168.8.100/colight?channel=10&action=off"
```
Expected: each call returns `success:true` immediately (no multi-second wait); the physical INTERIOR light visibly turns on then off; `/colight/state` between the two calls shows channel 10 (index 9 in the `state` array, 0-indexed) as `true`.

- [ ] **Step 3: Two-channel non-interference (repeats the original plan's Task 6 check, now through the persistent connection)**

```bash
curl -s -m 5 -X POST "http://192.168.8.100/colight?channel=5&action=on"
curl -s -m 5 -X POST "http://192.168.8.100/colight?channel=6&action=on"
curl -s -m 5 http://192.168.8.100/colight/state
```
Expected: both channel 5 and channel 6 show `true` in the final `/colight/state` call — turning channel 6 on must not clear channel 5 (this was the original full-snapshot-vs-delta bug; confirms the cached-frame-based write logic still preserves other channels correctly).

- [ ] **Step 4: Victron coexistence over time**

```bash
curl -s -m 5 http://192.168.8.100/data | python3 -c "import sys,json; print(json.load(sys.stdin)['battery'])"
```
Run this once now, then again after waiting at least 3 minutes with the CoLight connection held open the whole time (don't touch any switches — this checks the *idle* coexistence case, not just right after a reconnect). Expected: both calls return `valid:true` with a plausible voltage — confirms Victron's passive scan keeps working continuously alongside the now-persistent CoLight GATT connection, not just immediately after boot.

- [ ] **Step 5: Reconnect behavior**

Temporarily power off the CoLight panel (or move the ESP32/panel out of BLE range) for at least 15 seconds, then restore it. While it's off, run `curl -s -m 5 http://192.168.8.100/colight/state` and expect `connected:false` (with the last-known `state` still served if `valid` was already `true` from Step 1-3). After restoring power, wait ~10s and confirm `connected` returns to `true` again without requiring a reboot or a fresh OTA flash.

- [ ] **Step 6: Update memory**

Record the outcome (pass/fail per step, and the final measured behavior) as a project memory entry, following the conventions already used for prior CoLight findings (`project_colight_ble_findings`-style entry) — note this is a memory-system action for the assistant, not a code change.
