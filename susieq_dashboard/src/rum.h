#pragma once
#include <Arduino.h>

struct RumData {
    float liters = 0.0f;      // estimated litres remaining
    float pct    = 0.0f;      // 0–100 %
    bool  valid  = false;
};

void    rum_init();
void    rum_tare();                        // call with empty bottle on scale
bool    rum_calibrate(float known_kg);    // call with known weight on scale; saves factor to NVS
float   rum_get_raw_units();              // raw reading for debugging
RumData rum_read();
