#include "rum.h"
#include "../include/config.h"
#include <HX711.h>
#include <Preferences.h>

// ─── HX711 Load Cell — Rum Bottle Weight ────────────────────────────────
// A small load cell sits under the rum bottle holder.
// Tare = empty bottle weight (stored in NVS flash via Preferences).
// Net weight / rum_density = litres remaining.

static HX711 rum_scale;
static Preferences rum_prefs;
static float rum_tare_offset   = 0.0f;
static float rum_cal_factor    = RUM_CALIBRATION_FACTOR;

void rum_init() {
    rum_scale.begin(RUM_HX711_DOUT_PIN, RUM_HX711_SCK_PIN);

    rum_prefs.begin("rum", true);   // read-only
    rum_tare_offset = rum_prefs.getFloat("tare", 0.0f);
    rum_cal_factor  = rum_prefs.getFloat("cal",  RUM_CALIBRATION_FACTOR);
    rum_prefs.end();

    rum_scale.set_scale(rum_cal_factor);
    if (rum_tare_offset != 0.0f) {
        rum_scale.set_offset((long)rum_tare_offset);
    }

    Serial.printf("[rum] init — cal_factor=%.2f  tare_offset=%.2f (from NVS)\n",
                  rum_cal_factor, rum_tare_offset);
}

// Call with empty bottle resting on scale; saves tare to flash
// Debounced: ignores calls within 10 s of last tare to protect NVS flash
static unsigned long last_tare_ms = 0;
void rum_tare() {
    if (millis() - last_tare_ms < 10000) {
        Serial.println("[rum] tare debounced — wait 10s");
        return;
    }
    rum_scale.tare();
    rum_tare_offset = rum_scale.get_offset();
    rum_prefs.begin("rum", false);
    rum_prefs.putFloat("tare", rum_tare_offset);
    rum_prefs.end();
    last_tare_ms = millis();
    Serial.println("[rum] tare saved");
}

// Call with a known weight on the scale (after tare).
// Reads 20 samples, calculates calibration factor, saves to NVS.
// Returns false if scale not ready.
bool rum_calibrate(float known_kg) {
    // Wait up to 2 s for the sensor to become ready
    unsigned long deadline = millis() + 2000;
    while (!rum_scale.is_ready() && millis() < deadline) {
        delay(10);
    }
    if (!rum_scale.is_ready()) {
        Serial.println("[rum] calibrate: scale not ready");
        return false;
    }
    // Temporarily set scale to 1.0 to get raw ADC value
    rum_scale.set_scale(1.0f);
    float raw = rum_scale.get_units(20);
    if (raw == 0.0f || known_kg <= 0.0f) {
        rum_scale.set_scale(rum_cal_factor);   // restore
        Serial.println("[rum] calibrate: bad raw or weight");
        return false;
    }
    rum_cal_factor = raw / known_kg;
    rum_scale.set_scale(rum_cal_factor);

    rum_prefs.begin("rum", false);
    rum_prefs.putFloat("cal", rum_cal_factor);
    rum_prefs.end();

    Serial.printf("[rum] calibrated — raw=%.2f  known=%.3f kg  factor=%.2f\n",
                  raw, known_kg, rum_cal_factor);
    return true;
}

// Returns current kg reading (tare applied) for debugging
float rum_get_raw_units() {
    unsigned long deadline = millis() + 2000;
    while (!rum_scale.is_ready() && millis() < deadline) {
        delay(10);
    }
    if (!rum_scale.is_ready()) return -1.0f;
    return rum_scale.get_units(5);
}

RumData rum_read() {
    RumData d;
    if (!rum_scale.is_ready()) return d;

    float net_kg = rum_scale.get_units(3);   // average 3 readings, tare applied
    if (net_kg < 0) net_kg = 0;

    d.liters = net_kg / RUM_DENSITY;
    if (d.liters < 0) d.liters = 0;
    float max_liters = RUM_BOTTLE_FULL_KG / RUM_DENSITY;
    if (d.liters > max_liters) d.liters = max_liters;
    d.pct   = (net_kg / RUM_BOTTLE_FULL_KG) * 100.0f;
    if (d.pct > 100.0f) d.pct = 100.0f;
    d.valid = true;
    return d;
}
