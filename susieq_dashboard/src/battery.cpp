#include "battery.h"
#include "../include/config.h"
#include <Wire.h>
#include <INA219_WE.h>

// ─── INA219 Battery Monitor ────────────────────────────────────────────
// Wiring: SDA → GPIO21, SCL → GPIO22, default I2C address 0x40
// Shunt resistor on module: 0.1 Ω  → max measurable current ±3.2 A
// For higher currents replace shunt with 0.01 Ω and update INA219_SHUNT_OHM.
//
// SOC estimation: simple voltage-based lookup for a 12V lead-acid / AGM.
// Works "good enough" for onboard display; replace with coulomb counting
// (integrate current over time) for higher accuracy.

static INA219_WE ina219;

// 12V lead-acid/AGM voltage → SOC lookup table (20 points, 5% steps)
static const float soc_v_table[21] = {
    11.51f, 11.66f, 11.81f, 11.96f, 12.00f,  //  0– 20 %
    12.06f, 12.12f, 12.20f, 12.32f, 12.42f,  // 25– 45 %
    12.50f, 12.60f, 12.70f, 12.79f, 12.88f,  // 50– 70 %
    12.98f, 13.05f, 13.11f, 13.20f, 13.30f,  // 75– 95 %
    13.40f                                     // 100 %
};

float battery_voltage_to_soc(float v) {
    if (v <= soc_v_table[0])  return 0.0f;
    if (v >= soc_v_table[20]) return 100.0f;
    for (int i = 0; i < 20; i++) {
        if (v >= soc_v_table[i] && v < soc_v_table[i + 1]) {
            float frac = (v - soc_v_table[i]) / (soc_v_table[i + 1] - soc_v_table[i]);
            return (i + frac) * 5.0f;
        }
    }
    return 0.0f;
}

void battery_init() {
    Wire.begin(INA219_SDA, INA219_SCL);
    if (!ina219.init()) {
        Serial.println("[battery] INA219 not found — check wiring");
    }
    // Configure for 12V system: max voltage 16V, max current ~3.2A (default)
    ina219.setADCMode(INA219_SAMPLE_MODE_128); // average 128 samples
    ina219.setPGain(INA219_PG_320);            // ±320 mV shunt range → up to 3.2 A w/ 0.1Ω
}

BatteryData battery_read() {
    BatteryData d;
    d.voltage   = ina219.getBusVoltage_V();
    d.current   = ina219.getCurrent_mA() / 1000.0f;  // convert to A
    d.power_w   = ina219.getBusPower() / 1000.0f;
    d.soc_pct   = battery_voltage_to_soc(d.voltage);
    d.valid     = (d.voltage > 1.0f);  // sanity check
    return d;
}
