#pragma once
#include <cstdint>

// See tools/colight-ble/FINDINGS.md for the full protocol writeup.
// G1's baseline (all channels off) is 0x22; G2..G6's baseline is 0x00.
constexpr uint8_t COLIGHT_G1_BASELINE = 0x22;
constexpr int COLIGHT_NUM_CHANNELS = 12;

// Decodes a 6-byte state frame (G1..G6, the payload after the leading
// 0xf9 in a beffa notification, or the payload after the leading 0xf5 in
// a write command) into 12 channel booleans.
// channels_out[0] = channel 1 ... channels_out[11] = channel 12.
void colight_decode(const uint8_t frame[6], bool channels_out[COLIGHT_NUM_CHANNELS]);

// Sets or clears one channel's bit in an existing 6-byte frame, preserving
// every other channel's bit untouched. channel is 1-indexed (1..12).
// The frame must already hold the current full state — writing without
// first reading the current state will clobber whichever other channels
// are not represented in the frame you started from.
void colight_set_channel(uint8_t frame[6], int channel, bool on);

// Fills frame with the baseline (all channels off).
void colight_baseline_frame(uint8_t frame[6]);
