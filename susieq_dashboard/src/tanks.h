#pragma once
#include <Arduino.h>

struct TankData {
    float liters    = 0.0f;   // litres remaining
    int   level_pct = 0;      // 0–100 %
    bool  valid     = false;
};

void tanks_init();
void water_tare();
TankData tanks_read();
