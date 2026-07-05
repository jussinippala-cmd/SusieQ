#include "colight_protocol.h"

static int colight_group_index(int channel) {
    return (channel - 1) / 2;
}

static uint8_t colight_nibble(int channel) {
    return ((channel - 1) % 2 == 0) ? 0x10 : 0x01;
}

void colight_baseline_frame(uint8_t frame[6]) {
    frame[0] = COLIGHT_G1_BASELINE;
    frame[1] = 0x00;
    frame[2] = 0x00;
    frame[3] = 0x00;
    frame[4] = 0x00;
    frame[5] = 0x00;
}

void colight_decode(const uint8_t frame[6], bool channels_out[COLIGHT_NUM_CHANNELS]) {
    for (int ch = 1; ch <= COLIGHT_NUM_CHANNELS; ch++) {
        uint8_t byte_val = frame[colight_group_index(ch)];
        channels_out[ch - 1] = (byte_val & colight_nibble(ch)) != 0;
    }
}

void colight_set_channel(uint8_t frame[6], int channel, bool on) {
    int group = colight_group_index(channel);
    uint8_t nibble = colight_nibble(channel);
    if (on) {
        frame[group] |= nibble;
    } else {
        frame[group] &= (uint8_t)~nibble;
    }
}
