#include <unity.h>
#include "sleep_policy.h"

void setUp(void) {}
void tearDown(void) {}

// ── Onnistuva tapaus ──────────────────────────────────────────────────
void test_valid_request_accepted(void) {
    TEST_ASSERT_EQUAL(SLEEP_OK, sleep_policy_check(700000, false, 27900));
}

// ── Boot-armonaika ────────────────────────────────────────────────────
void test_boot_grace_blocks_fresh_boot(void) {
    TEST_ASSERT_EQUAL(SLEEP_BOOT_GRACE, sleep_policy_check(0, false, 27900));
    TEST_ASSERT_EQUAL(SLEEP_BOOT_GRACE, sleep_policy_check(599999, false, 27900));
}

void test_boot_grace_boundary_exact(void) {
    // Tasan 10 min riittää — raja on inklusiivinen.
    TEST_ASSERT_EQUAL(SLEEP_OK, sleep_policy_check(600000, false, 27900));
}

// ── OTA ───────────────────────────────────────────────────────────────
void test_ota_active_blocks(void) {
    TEST_ASSERT_EQUAL(SLEEP_OTA_ACTIVE, sleep_policy_check(700000, true, 27900));
}

// ── Kesto ─────────────────────────────────────────────────────────────
void test_duration_too_short_rejected(void) {
    TEST_ASSERT_EQUAL(SLEEP_BAD_DURATION, sleep_policy_check(700000, false, 59));
    TEST_ASSERT_EQUAL(SLEEP_BAD_DURATION, sleep_policy_check(700000, false, 0));
}

void test_duration_too_long_rejected(void) {
    TEST_ASSERT_EQUAL(SLEEP_BAD_DURATION, sleep_policy_check(700000, false, 28801));
}

void test_duration_boundaries_accepted(void) {
    TEST_ASSERT_EQUAL(SLEEP_OK, sleep_policy_check(700000, false, 60));
    TEST_ASSERT_EQUAL(SLEEP_OK, sleep_policy_check(700000, false, 28800));
}

// Klo 22:00 laskettu todellinen kesto klo 05:45:een.
void test_real_nightly_duration_accepted(void) {
    TEST_ASSERT_EQUAL(SLEEP_OK, sleep_policy_check(700000, false, 27900));
}

// ── Tarkistusjärjestys ────────────────────────────────────────────────
// Vasta buutannut laite, jolla on myös kelvoton kesto, saa "boot_grace":
// tärkein este ensin, jotta modeemin loki kertoo oikean syyn.
void test_boot_grace_takes_precedence_over_duration(void) {
    TEST_ASSERT_EQUAL(SLEEP_BOOT_GRACE, sleep_policy_check(1000, false, 999999));
}

void test_boot_grace_takes_precedence_over_ota(void) {
    TEST_ASSERT_EQUAL(SLEEP_BOOT_GRACE, sleep_policy_check(1000, true, 27900));
}

void test_ota_takes_precedence_over_duration(void) {
    TEST_ASSERT_EQUAL(SLEEP_OTA_ACTIVE, sleep_policy_check(700000, true, 5));
}

// ── Virhetunnisteet ───────────────────────────────────────────────────
void test_error_strings(void) {
    TEST_ASSERT_EQUAL_STRING("",             sleep_verdict_error(SLEEP_OK));
    TEST_ASSERT_EQUAL_STRING("boot_grace",   sleep_verdict_error(SLEEP_BOOT_GRACE));
    TEST_ASSERT_EQUAL_STRING("ota_active",   sleep_verdict_error(SLEEP_OTA_ACTIVE));
    TEST_ASSERT_EQUAL_STRING("bad_duration", sleep_verdict_error(SLEEP_BAD_DURATION));
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_valid_request_accepted);
    RUN_TEST(test_boot_grace_blocks_fresh_boot);
    RUN_TEST(test_boot_grace_boundary_exact);
    RUN_TEST(test_ota_active_blocks);
    RUN_TEST(test_duration_too_short_rejected);
    RUN_TEST(test_duration_too_long_rejected);
    RUN_TEST(test_duration_boundaries_accepted);
    RUN_TEST(test_real_nightly_duration_accepted);
    RUN_TEST(test_boot_grace_takes_precedence_over_duration);
    RUN_TEST(test_boot_grace_takes_precedence_over_ota);
    RUN_TEST(test_ota_takes_precedence_over_duration);
    RUN_TEST(test_error_strings);
    return UNITY_END();
}
