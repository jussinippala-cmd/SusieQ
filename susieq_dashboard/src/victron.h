#pragma once
#include <Arduino.h>

struct VictronData {
    float battery_voltage = 0.0f;  // V
    float battery_current = 0.0f;  // A
    float pv_power_w      = 0.0f;  // W from solar panel
    float pv_voltage      = 0.0f;  // V panel voltage
    uint8_t charge_state  = 0;     // 0=off 2=fault 3=bulk 4=absorb 5=float
    bool  valid           = false;
    unsigned long last_seen_ms = 0;
};

// enabled only when VICTRON_KEY is non-empty in config.h
void victron_init();
VictronData victron_get();   // returns latest cached data (updated by BLE callback)

// 12V lead-acid/AGM voltage → SOC estimation (used with Victron battery voltage)
float battery_voltage_to_soc(float v);
