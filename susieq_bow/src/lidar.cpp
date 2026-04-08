#include "lidar.h"
#include "../include/config.h"
#include <Arduino.h>

// TF-Luna UART protocol:
// 9-byte frame: [0x59] [0x59] [Dist_L] [Dist_H] [Str_L] [Str_H] [Temp_L] [Temp_H] [Checksum]
// Checksum = low byte of sum of first 8 bytes

static HardwareSerial LidarSerial(1);  // UART1

static LidarData _last = { 0, 0, false };
static unsigned long _lastValidMs = 0;

void lidar_init() {
    LidarSerial.setRxBufferSize(1024);  // prevent overflow during sleep
    LidarSerial.begin(LIDAR_BAUD, SERIAL_8N1, LIDAR_RX_PIN, LIDAR_TX_PIN);
    Serial.println("[lidar] TF-Luna UART init on RX=" +
                   String(LIDAR_RX_PIN) + " TX=" + String(LIDAR_TX_PIN));
}

void lidar_flush() {
    while (LidarSerial.available()) LidarSerial.read();
}

LidarData lidar_read() {
    // Try to read all available frames, keep the latest valid one.
    // Improved sync: read byte-by-byte until 0x59 0x59 header found,
    // then read remaining 7 bytes. Prevents losing valid frames on desync.
    int bytesProcessed = 0;
    while (LidarSerial.available() >= 9 && bytesProcessed < 72) {
        // Sync: look for first 0x59
        if (LidarSerial.peek() != 0x59) {
            LidarSerial.read();  // discard non-header byte
            continue;
        }
        // Consume first 0x59
        LidarSerial.read();

        // Check second byte — must also be 0x59
        if (LidarSerial.available() < 8) break;  // not enough data yet
        if (LidarSerial.peek() != 0x59) {
            continue;  // false start, keep searching
        }

        // Read remaining 8 bytes (second 0x59 + 6 data + checksum)
        uint8_t buf[9];
        buf[0] = 0x59;
        if (LidarSerial.readBytes(buf + 1, 8) != 8) break;

        // Verify checksum
        uint8_t sum = 0;
        for (int i = 0; i < 8; i++) sum += buf[i];
        if (sum != buf[8]) continue;

        uint16_t dist = buf[2] | (buf[3] << 8);
        uint16_t str  = buf[4] | (buf[5] << 8);

        // Apply mounting offset
        uint16_t adjusted = (dist > LIDAR_OFFSET_CM) ? (dist - LIDAR_OFFSET_CM) : 0;

        // TF-Luna valid range: 20–800 cm, strength > 100
        bytesProcessed += 9;
        if (adjusted >= 20 && adjusted <= 800 && str > 100) {
            _last.distance_cm = adjusted;
            _last.strength    = str;
            _last.valid       = true;
            _lastValidMs      = millis();
        }
    }

    // Mark stale if no valid reading for 2 seconds
    if (millis() - _lastValidMs > 2000) {
        _last.valid = false;
    }

    return _last;
}
