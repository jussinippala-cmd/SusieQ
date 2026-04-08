#include "wind.h"
#include "../include/config.h"
#include <ModbusMaster.h>

// ─── RS485 Ultrasonic Wind Sensor via Modbus RTU ──────────────────────
// Veinasa Mini-C2A ultrasonic wind sensor via Modbus RTU.
// Register map (holding registers, function 0x03):
//   0x0000 = wind speed  × 100  (e.g. 272 → 2.72 m/s)
//   0x0001 = wind direction × 1  (e.g. 176 → 176°)

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
    uint8_t result = node.readHoldingRegisters(REG_SPEED, 2);
    if (result == ModbusMaster::ku8MBSuccess) {
        d.speed_ms  = node.getResponseBuffer(0) / 100.0f;
        d.direction = node.getResponseBuffer(1) * 1.0f;
        // Clamp to valid ranges
        if (d.speed_ms  > 60.0f)  d.speed_ms  = 60.0f;
        d.direction = fmod(fmod(d.direction, 360.0f) + 360.0f, 360.0f);
        d.valid = true;
    }
    return d;
}
