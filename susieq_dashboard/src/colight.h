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
