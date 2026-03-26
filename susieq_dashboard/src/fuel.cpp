#include "fuel.h"
#include "../include/config.h"
#include <HX711.h>
#include <Preferences.h>

// ─── HX711 Load Cell — Fuel Canister Weight ────────────────────────────
// A 50 kg load cell sits under the fuel canister in a fixed holder.
// NOTE: 25L petrol canister weighs ~21.5 kg full — use min 30 kg, recommend 50 kg cell.
// Tare = empty canister weight (stored in NVS flash via Preferences).
// Net weight / fuel_density = litres remaining.
//
// Calibration: after wiring, run fuel_tare() once with EMPTY canister,
// then weigh a known-weight object and adjust CALIBRATION_FACTOR until
// the reading matches.  Typical factor: ~400–450 for most HX711 modules.

#define CALIBRATION_FACTOR  420.0f    // adjust until scale reads correct kg

static HX711 scale;
static Preferences prefs;
static float tare_offset = 0.0f;

void fuel_init() {
    scale.begin(HX711_DOUT_PIN, HX711_SCK_PIN);
    scale.set_scale(CALIBRATION_FACTOR);

    prefs.begin("fuel", true);   // read-only
    tare_offset = prefs.getFloat("tare", 0.0f);
    prefs.end();

    if (tare_offset != 0.0f) {
        scale.set_offset((long)tare_offset);
    }

    Serial.printf("[fuel] tare offset = %.2f (restored from NVS)\n", tare_offset);
}

// Call with empty canister resting on scale; saves tare to flash
void fuel_tare() {
    scale.tare();
    tare_offset = scale.get_offset();
    prefs.begin("fuel", false);
    prefs.putFloat("tare", tare_offset);
    prefs.end();
    Serial.println("[fuel] tare saved");
}

FuelData fuel_read() {
    FuelData d;
    if (!scale.is_ready()) return d;

    float gross_kg = scale.get_units(5);   // average 5 readings
    if (gross_kg < 0) gross_kg = 0;

    float net_kg = gross_kg;               // tare already applied via set_offset
    d.liters = net_kg / FUEL_DENSITY;
    if (d.liters < 0) d.liters = 0;
    if (d.liters > FUEL_CANISTER_MAX_L) d.liters = FUEL_CANISTER_MAX_L;
    d.pct   = (d.liters / FUEL_CANISTER_MAX_L) * 100.0f;
    d.valid = true;
    return d;
}
