#include <unity.h>
#include "colight_protocol.h"

void setUp(void) {}
void tearDown(void) {}

static void assert_only_channel_on(bool channels[COLIGHT_NUM_CHANNELS], int channel) {
    for (int i = 0; i < COLIGHT_NUM_CHANNELS; i++) {
        bool expected = (i == channel - 1);
        TEST_ASSERT_EQUAL(expected, channels[i]);
    }
}

void test_baseline_decodes_all_off(void) {
    uint8_t frame[6];
    colight_baseline_frame(frame);
    bool channels[COLIGHT_NUM_CHANNELS];
    colight_decode(frame, channels);
    for (int i = 0; i < COLIGHT_NUM_CHANNELS; i++) {
        TEST_ASSERT_FALSE(channels[i]);
    }
}

void test_set_channel_1_matches_captured_lower_command(void) {
    uint8_t frame[6];
    colight_baseline_frame(frame);
    colight_set_channel(frame, 1, true);
    uint8_t expected[6] = {0x32, 0x00, 0x00, 0x00, 0x00, 0x00};
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, frame, 6);
}

void test_set_channel_2_matches_captured_raise_command(void) {
    uint8_t frame[6];
    colight_baseline_frame(frame);
    colight_set_channel(frame, 2, true);
    uint8_t expected[6] = {0x23, 0x00, 0x00, 0x00, 0x00, 0x00};
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, frame, 6);
}

void test_set_channel_10_matches_captured_interior_command(void) {
    uint8_t frame[6];
    colight_baseline_frame(frame);
    colight_set_channel(frame, 10, true);
    uint8_t expected[6] = {0x22, 0x00, 0x00, 0x00, 0x01, 0x00};
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, frame, 6);
}

void test_set_channel_11_matches_captured_nav_lights_command(void) {
    uint8_t frame[6];
    colight_baseline_frame(frame);
    colight_set_channel(frame, 11, true);
    uint8_t expected[6] = {0x22, 0x00, 0x00, 0x00, 0x00, 0x10};
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, frame, 6);
}

void test_decode_single_channel_10(void) {
    uint8_t frame[6] = {0x22, 0x00, 0x00, 0x00, 0x01, 0x00};
    bool channels[COLIGHT_NUM_CHANNELS];
    colight_decode(frame, channels);
    assert_only_channel_on(channels, 10);
}

void test_two_channels_in_same_group_both_report_on(void) {
    // Reproduces the live-verified behavior: DECK LIGHTS (ch5) and STEREO
    // (ch6) share byte-group 3 (frame[2]) — both nibbles must be able to
    // be set at once, proving the encoding is a bitmask, not an enum.
    uint8_t frame[6];
    colight_baseline_frame(frame);
    colight_set_channel(frame, 5, true);
    colight_set_channel(frame, 6, true);
    bool channels[COLIGHT_NUM_CHANNELS];
    colight_decode(frame, channels);
    TEST_ASSERT_TRUE(channels[4]);
    TEST_ASSERT_TRUE(channels[5]);
}

void test_turning_off_one_channel_preserves_other_in_same_group(void) {
    uint8_t frame[6];
    colight_baseline_frame(frame);
    colight_set_channel(frame, 5, true);
    colight_set_channel(frame, 6, true);
    colight_set_channel(frame, 6, false);
    bool channels[COLIGHT_NUM_CHANNELS];
    colight_decode(frame, channels);
    TEST_ASSERT_TRUE(channels[4]);
    TEST_ASSERT_FALSE(channels[5]);
}

void test_turning_off_last_channel_returns_to_baseline(void) {
    uint8_t frame[6];
    colight_baseline_frame(frame);
    colight_set_channel(frame, 10, true);
    colight_set_channel(frame, 10, false);
    uint8_t expected[6];
    colight_baseline_frame(expected);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, frame, 6);
}

int main(int argc, char **argv) {
    UNITY_BEGIN();
    RUN_TEST(test_baseline_decodes_all_off);
    RUN_TEST(test_set_channel_1_matches_captured_lower_command);
    RUN_TEST(test_set_channel_2_matches_captured_raise_command);
    RUN_TEST(test_set_channel_10_matches_captured_interior_command);
    RUN_TEST(test_set_channel_11_matches_captured_nav_lights_command);
    RUN_TEST(test_decode_single_channel_10);
    RUN_TEST(test_two_channels_in_same_group_both_report_on);
    RUN_TEST(test_turning_off_one_channel_preserves_other_in_same_group);
    RUN_TEST(test_turning_off_last_channel_returns_to_baseline);
    return UNITY_END();
}
