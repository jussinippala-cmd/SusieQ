#include "wind.h"
#include "../include/config.h"
#include <ModbusMaster.h>

// ─── RS485 Ultrasonic Wind Sensor via Modbus RTU ──────────────────────
// Compatible with JXCT / Rika / generic RS485 wind sensors.
// Typical register map (0-based):
//   0x0000 = wind speed  × 10  (e.g. 125 → 12.5 m/s)
//   0x0001 = wind direction × 10 (e.g. 2700 → 270.0°)
// Adjust REG_SPEED / REG_DIR if your sensor uses different registers.

#define REG_SPEED    0x0000
#define REG_DIR      0x0001

static ModbusMaster node;
static HardwareSerial rs485(2);  // UART2

// MAX485 DE/RE pre-/post-transmission callbacks
static void preTransmit()  { digitalWrite(WIND_DE_PIN, HIGH); }
static void postTransmit() { digitalWrite(WIND_DE_PIN, LOW);  }

void wind_init() {
    pinMode(WIND_DE_PIN, OUTPUT);
    digitalWrite(WIND_DE_PIN, LOW);

    rs485.begin(WIND_BAUD, SERIAL_8N1, WIND_RX_PIN, WIND_TX_PIN);
    node.begin(WIND_MODBUS_ID, rs485);
    node.preTransmission(preTransmit);
    node.postTransmission(postTransmit);
}

WindData wind_read() {
    WindData d;
    uint8_t result = node.readInputRegisters(REG_SPEED, 2);
    if (result == ModbusMaster::ku8MBSuccess) {
        d.speed_ms  = node.getResponseBuffer(0) / 10.0f;
        d.direction = node.getResponseBuffer(1) / 10.0f;
        // Clamp to valid ranges (source is uint16_t, so always >= 0)
        if (d.speed_ms  > 60.0f)  d.speed_ms  = 60.0f;
        if (d.direction > 360.0f) d.direction = fmod(d.direction, 360.0f);
        d.valid = true;
    }
    return d;
}
