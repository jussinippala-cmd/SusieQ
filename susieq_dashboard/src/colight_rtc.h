#pragma once
#include <cstdint>

// Validation logic for the CoLight state frame persisted in RTC_NOINIT
// memory across warm (sw/wdt/panic) reboots — see docs/superpowers/specs/
// 2026-07-15-colight-rtc-state-cache-design.md. Pure functions with no
// Arduino/ESP dependency so they can be unit-tested natively; the actual
// RTC variables, the save sites, and the reset-reason gate live in
// colight.cpp.
constexpr uint32_t COLIGHT_RTC_MAGIC = 0xC0119870u;

// FNV-1a 32-bit over the 6 frame bytes. Distinguishes a genuinely saved
// frame from cold-boot RTC garbage that happens to have a plausible magic.
uint32_t colight_rtc_checksum(const uint8_t frame[6]);

// True only when magic matches COLIGHT_RTC_MAGIC and check matches the
// frame's checksum. The caller must additionally gate on esp_reset_reason()
// (RTC contents are undefined after poweron/brownout).
bool colight_rtc_frame_valid(uint32_t magic, uint32_t check, const uint8_t frame[6]);

// What to do with the state cache at boot.
enum ColightBootAction {
    COLIGHT_BOOT_UNKNOWN = 0,  // start from unknown_state
    COLIGHT_BOOT_RESTORE,      // use the frame saved in RTC memory
    COLIGHT_BOOT_ASSUME_OFF,   // seed the all-off baseline frame
};

// warm: reset reason preserves RTC memory (sw/wdt/panic).
// rtc_valid: colight_rtc_frame_valid() on the stored data.
// power_cycle: reset reason is poweron/brownout — the panel shares the
// boat's main switch, so it lost power too and always boots all-off,
// which makes the all-off assumption safe. A power cycle overrides even
// a valid-looking RTC frame (the panel rebooted, so it is stale).
ColightBootAction colight_boot_action(bool warm, bool rtc_valid, bool power_cycle);
