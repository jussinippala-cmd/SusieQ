#include "colight_rtc.h"

uint32_t colight_rtc_checksum(const uint8_t frame[6]) {
    uint32_t hash = 2166136261u;  // FNV-1a offset basis
    for (int i = 0; i < 6; i++) {
        hash ^= frame[i];
        hash *= 16777619u;        // FNV-1a prime
    }
    return hash;
}

bool colight_rtc_frame_valid(uint32_t magic, uint32_t check, const uint8_t frame[6]) {
    return magic == COLIGHT_RTC_MAGIC && check == colight_rtc_checksum(frame);
}
