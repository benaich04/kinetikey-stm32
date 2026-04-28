#ifndef SENSOR_H
#define SENSOR_H

#include <stdint.h>

typedef struct {
    float ax, ay, az;
    float gx, gy, gz;
    uint32_t time_ms;
} SensorSample;

void Sensor_Init(void);
SensorSample getFilteredSample(void);

#endif