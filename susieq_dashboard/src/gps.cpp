#include "gps.h"
#include "../include/config.h"
#include <TinyGPSPlus.h>

static TinyGPSPlus gps;
static HardwareSerial gpsSerial(1);  // UART1

void gps_init() {
    gpsSerial.begin(GPS_BAUD, SERIAL_8N1, GPS_RX_PIN, GPS_TX_PIN);
}

GpsData gps_read() {
    while (gpsSerial.available()) gps.encode(gpsSerial.read());

    GpsData d;
    d.valid = gps.speed.isValid();
    if (gps.speed.isValid()) {
        d.sog_knots = (float)gps.speed.knots();
        d.fix = true;
    }
    if (gps.course.isValid()) d.cog_deg = (float)gps.course.deg();
    if (gps.location.isValid()) {
        d.lat = gps.location.lat();
        d.lon = gps.location.lng();
    }
    if (gps.time.isValid() && gps.time.isUpdated()) {
        d.hour       = gps.time.hour();
        d.minute     = gps.time.minute();
        d.second     = gps.time.second();
        d.time_valid = true;
    }
    return d;
}
