#include <unity.h>
#include <cstring>
#include "colight_rtc.h"

void setUp(void) {}
void tearDown(void) {}

// Sama syöte -> sama summa; eri syöte -> eri summa (FNV-1a on deterministinen).
void test_checksum_is_deterministic(void) {
    uint8_t frame[6] = {0x22, 0x00, 0x01, 0x00, 0x10, 0x00};
    TEST_ASSERT_EQUAL_UINT32(colight_rtc_checksum(frame), colight_rtc_checksum(frame));
}

void test_checksum_changes_when_frame_changes(void) {
    uint8_t a[6] = {0x22, 0x00, 0x00, 0x00, 0x00, 0x00};
    uint8_t b[6] = {0x22, 0x00, 0x00, 0x00, 0x01, 0x00};
    TEST_ASSERT_NOT_EQUAL(colight_rtc_checksum(a), colight_rtc_checksum(b));
}

void test_valid_frame_accepted(void) {
    uint8_t frame[6] = {0x22, 0x00, 0x01, 0x00, 0x10, 0x00};
    uint32_t check = colight_rtc_checksum(frame);
    TEST_ASSERT_TRUE(colight_rtc_frame_valid(COLIGHT_RTC_MAGIC, check, frame));
}

// Kaikki kanavat pois (baseline 0x22 + nollat) on laillinen tila — sen on
// kelvattava, kunhan magic + summa täsmäävät.
void test_all_off_frame_accepted(void) {
    uint8_t frame[6] = {0x22, 0x00, 0x00, 0x00, 0x00, 0x00};
    uint32_t check = colight_rtc_checksum(frame);
    TEST_ASSERT_TRUE(colight_rtc_frame_valid(COLIGHT_RTC_MAGIC, check, frame));
}

void test_wrong_magic_rejected(void) {
    uint8_t frame[6] = {0x22, 0x00, 0x01, 0x00, 0x10, 0x00};
    uint32_t check = colight_rtc_checksum(frame);
    TEST_ASSERT_FALSE(colight_rtc_frame_valid(0x00000000u, check, frame));
    TEST_ASSERT_FALSE(colight_rtc_frame_valid(COLIGHT_RTC_MAGIC + 1, check, frame));
}

void test_corrupted_frame_rejected(void) {
    uint8_t frame[6] = {0x22, 0x00, 0x01, 0x00, 0x10, 0x00};
    uint32_t check = colight_rtc_checksum(frame);
    frame[2] ^= 0x01;  // yhden bitin korruptio kehyksessä summan laskun jälkeen
    TEST_ASSERT_FALSE(colight_rtc_frame_valid(COLIGHT_RTC_MAGIC, check, frame));
}

void test_corrupted_check_rejected(void) {
    uint8_t frame[6] = {0x22, 0x00, 0x01, 0x00, 0x10, 0x00};
    uint32_t check = colight_rtc_checksum(frame) ^ 0x00000100u;
    TEST_ASSERT_FALSE(colight_rtc_frame_valid(COLIGHT_RTC_MAGIC, check, frame));
}

// Kylmäkäynnistyksen tyypillisin RTC-sisältö: kaikki nollaa. Magic 0 != oikea
// magic, joten hylkäys tulee jo siitä — varmistetaan silti eksplisiittisesti.
void test_all_zero_rtc_garbage_rejected(void) {
    uint8_t frame[6] = {0, 0, 0, 0, 0, 0};
    TEST_ASSERT_FALSE(colight_rtc_frame_valid(0, 0, frame));
}

int main(int argc, char **argv) {
    UNITY_BEGIN();
    RUN_TEST(test_checksum_is_deterministic);
    RUN_TEST(test_checksum_changes_when_frame_changes);
    RUN_TEST(test_valid_frame_accepted);
    RUN_TEST(test_all_off_frame_accepted);
    RUN_TEST(test_wrong_magic_rejected);
    RUN_TEST(test_corrupted_frame_rejected);
    RUN_TEST(test_corrupted_check_rejected);
    RUN_TEST(test_all_zero_rtc_garbage_rejected);
    return UNITY_END();
}
