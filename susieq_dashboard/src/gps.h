#pragma once
#include <Arduino.h>

struct GpsData {
    float sog_knots = 0.0f;  // nopeus knopeissa
    float cog_deg   = 0.0f;  // kurssi asteina
    double lat      = 0.0;
    double lon      = 0.0;
    uint8_t hour    = 0;
    uint8_t minute  = 0;
    uint8_t second  = 0;
    bool  time_valid = false; // GPS-aika saatavilla
    bool  fix       = false;  // GPS-lukitus saatu
    bool  valid     = false;  // moduuli vastaa
};

void    gps_init();
GpsData gps_read();  // ei-blokkaava, kutsu joka sensorisyklissä
