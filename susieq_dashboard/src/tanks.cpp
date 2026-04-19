#include "tanks.h"
#include "../include/config.h"
#include <HX711.h>
#include <Preferences.h>

// ─── HX711 Load Cell — Water Tank Weight ───────────────────────────────
// A 50 kg load cell sits under the removable water tank.
// Tare = empty tank weight (stored in NVS flash via Preferences).
// Net weight / 1.0 kg/L (water density) = litres remaining.
//
// Calibration: after wiring, run water_tare() once with EMPTY tank,
// then place a known-weight object and adjust WATER_CALIBRATION_FACTOR
// until the reading matches.  Typical factor: ~400–450.

static HX711 water_scale;
static Preferences prefs;
static float tare_offset = 0.0f;

void tanks_init() {
    water_scale.begin(WATER_HX711_DOUT_PIN, WATER_HX711_SCK_PIN);
    water_scale.set_scale(WATER_CALIBRATION_FACTOR);

    prefs.begin("water", true);   // read-only
    tare_offset = prefs.getFloat("tare", 0.0f);
    prefs.end();

    if (tare_offset != 0.0f) {
        water_scale.set_offset((long)tare_offset);
    }

    Serial.printf("[water] tare offset = %.2f (restored from NVS)\n", tare_offset);
}

// Call with empty tank resting on scale; saves tare to flash
// Debounced: ignores calls within 10 s of last tare to protect NVS flash
static unsigned long last_tare_ms = 0;
void water_tare() {
    if (millis() - last_tare_ms < 10000) {
        Serial.println("[water] tare debounced — wait 10s");
        return;
    }
    water_scale.tare();
    tare_offset = water_scale.get_offset();
    prefs.begin("water", false);
    prefs.putFloat("tare", tare_offset);
    prefs.end();
    last_tare_ms = millis();
    Serial.println("[water] tare saved");
}

TankData tanks_read() {
    TankData d;
    if (!water_scale.is_ready()) return d;

    float gross_kg = water_scale.get_units(3);   // average 3 readings
    if (gross_kg < 0) gross_kg = 0;

    // Water density = 1.0 kg/L
    d.liters = gross_kg;   // tare already applied via set_offset
    if (d.liters < 0) d.liters = 0;
    if (d.liters > WATER_TANK_CAPACITY_L) d.liters = WATER_TANK_CAPACITY_L;
    d.level_pct = (int)((d.liters / WATER_TANK_CAPACITY_L) * 100.0f);
    d.valid = true;
    return d;
}
