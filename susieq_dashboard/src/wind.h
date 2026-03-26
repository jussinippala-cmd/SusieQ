#pragma once
#include <Arduino.h>

struct WindData {
    float speed_ms = 0.0f;   // m/s
    float direction = 0.0f;  // degrees 0–360
    bool  valid = false;
};

void wind_init();
WindData wind_read();
