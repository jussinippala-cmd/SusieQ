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
