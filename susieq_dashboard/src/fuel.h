#pragma once
#include <Arduino.h>

struct FuelData {
    float liters = 0.0f;      // estimated litres remaining
    float pct    = 0.0f;      // 0–100 %
    bool  valid  = false;
};

void  fuel_init();
void  fuel_tare();            // call once with empty canister on scale
FuelData fuel_read();
