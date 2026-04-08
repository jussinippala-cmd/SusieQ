#pragma once
#include <Arduino.h>

struct RumData {
    float liters = 0.0f;      // estimated litres remaining
    float pct    = 0.0f;      // 0–100 %
    bool  valid  = false;
};

void    rum_init();
void    rum_tare();            // call once with empty bottle on scale
RumData rum_read();
