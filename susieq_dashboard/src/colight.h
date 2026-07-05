#pragma once
#include "colight_protocol.h"

struct ColightResult {
    bool success = false;
    bool channels[COLIGHT_NUM_CHANNELS] = {};
    const char* error = "";  // set only when success == false
};

// NimBLE is already initialized by victron_init() — this is currently a
// no-op, kept for symmetry with the other sensor modules and as a place
// to hang future setup if the panel's BLE address needs to be cached.
void colight_init();

// Connects to the panel, reads the current 6-byte state frame, disconnects.
// Does not change anything on the panel.
ColightResult colight_read_state();

// Connects to the panel, reads the current state, sets/clears the given
// channel while preserving every other channel's bit, writes the new
// frame, disconnects. channel is 1-indexed (1..12).
ColightResult colight_send_command(int channel, bool turn_on);
