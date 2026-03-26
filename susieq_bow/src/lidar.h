#pragma once
#include <stdint.h>

struct LidarData {
    uint16_t distance_cm;   // 0–800 cm (TF-Luna range: 20–800)
    uint16_t strength;      // signal strength
    bool     valid;
};

void      lidar_init();
LidarData lidar_read();
void      lidar_flush();   // discard stale UART data (call after wake)
