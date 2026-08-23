#include "sleep_policy.h"

SleepVerdict sleep_policy_check(uint32_t uptime_ms, bool ota_active, uint32_t seconds) {
    if (uptime_ms < SLEEP_BOOT_GRACE_MS) return SLEEP_BOOT_GRACE;
    if (ota_active)                      return SLEEP_OTA_ACTIVE;
    if (seconds < SLEEP_MIN_S || seconds > SLEEP_MAX_S) return SLEEP_BAD_DURATION;
    return SLEEP_OK;
}

const char* sleep_verdict_error(SleepVerdict v) {
    switch (v) {
        case SLEEP_BOOT_GRACE:   return "boot_grace";
        case SLEEP_OTA_ACTIVE:   return "ota_active";
        case SLEEP_BAD_DURATION: return "bad_duration";
        default:                 return "";
    }
}
