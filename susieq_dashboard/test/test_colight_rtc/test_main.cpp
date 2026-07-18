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

// Boottipäätös: lämmin bootti + validi RTC-kehys -> palauta se.
void test_boot_warm_valid_restores(void) {
    TEST_ASSERT_EQUAL(COLIGHT_BOOT_RESTORE,
                      colight_boot_action(true, true, false));
}

// Lämmin bootti mutta RTC-roska (esim. ensimmäinen bootti OTA:n jälkeen kun
// RTC-muuttujia ei ole koskaan kirjoitettu): paneeli EI bootannut, sen tila
// voi olla mikä vain -> ei saa olettaa mitään.
void test_boot_warm_invalid_stays_unknown(void) {
    TEST_ASSERT_EQUAL(COLIGHT_BOOT_UNKNOWN,
                      colight_boot_action(true, false, false));
}

// Virtakierto (poweron/brownout): paneeli on saman pääkytkimen takana ja
// boottaa aina kaikki pois -tilaan -> oleta nollakehys.
void test_boot_power_cycle_assumes_off(void) {
    TEST_ASSERT_EQUAL(COLIGHT_BOOT_ASSUME_OFF,
                      colight_boot_action(false, false, true));
}

// Virtakierto voittaa vaikka RTC-data sattuisi validoitumaan — paneeli
// bootattiin, joten tallennettu kehys on joka tapauksessa vanhentunut.
void test_boot_power_cycle_wins_over_stale_rtc(void) {
    TEST_ASSERT_EQUAL(COLIGHT_BOOT_ASSUME_OFF,
                      colight_boot_action(false, true, true));
}

// Muut reset-syyt (esim. ulkoinen reset-nappi): ESP32 resetoitui yksinään,
// paneeli ei välttämättä -> ei palautusta eikä oletusta.
void test_boot_other_reset_stays_unknown(void) {
    TEST_ASSERT_EQUAL(COLIGHT_BOOT_UNKNOWN,
                      colight_boot_action(false, false, false));
    TEST_ASSERT_EQUAL(COLIGHT_BOOT_UNKNOWN,
                      colight_boot_action(false, true, false));
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
    RUN_TEST(test_boot_warm_valid_restores);
    RUN_TEST(test_boot_warm_invalid_stays_unknown);
    RUN_TEST(test_boot_power_cycle_assumes_off);
    RUN_TEST(test_boot_power_cycle_wins_over_stale_rtc);
    RUN_TEST(test_boot_other_reset_stays_unknown);
    return UNITY_END();
}
