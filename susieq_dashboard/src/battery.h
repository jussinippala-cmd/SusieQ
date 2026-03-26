#pragma once
#include <Arduino.h>

struct BatteryData {
    float voltage = 0.0f;    // V
    float current = 0.0f;    // A  (positive = charging)
    float power_w = 0.0f;    // W
    float soc_pct = 0.0f;    // state of charge 0–100 %
    bool  valid = false;
};

void battery_init();
BatteryData battery_read();
float battery_voltage_to_soc(float v);
