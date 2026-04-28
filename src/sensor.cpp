#include "sensor.h"
#include "mbed.h"

#define LSM6DSL_ADDR    (0x6A << 1)
#define CTRL1_XL        0x10
#define CTRL2_G         0x11
#define OUTX_L_G        0x22
#define OUTX_L_XL       0x28


static I2C i2c(PB_11, PB_10);  // CORRECT - I2C2, internal sensors bus

static float fax=0,fay=0,faz=0,fgx=0,fgy=0,fgz=0;
#define ALPHA 0.8f

static void sensor_write(uint8_t reg, uint8_t val) {
    char buf[2] = {(char)reg, (char)val};
    i2c.write(LSM6DSL_ADDR, buf, 2);
}

static void sensor_read(uint8_t reg, uint8_t *data, int len) {
    char r = (char)reg;
    i2c.write(LSM6DSL_ADDR, &r, 1);
    i2c.read(LSM6DSL_ADDR, (char*)data, len);
}

void Sensor_Init(void) {
    i2c.frequency(100000);
    ThisThread::sleep_for(10ms);
    
    // Check WHO_AM_I register - should return 0x6A
    uint8_t who = 0;
    sensor_read(0x0F, &who, 1);
    printf("WHO_AM_I = 0x%02X (expected 0x6A)\r\n", who);
    
    sensor_write(CTRL1_XL, 0x40);
    sensor_write(CTRL2_G,  0x40);
}

SensorSample getFilteredSample(void) {
    uint8_t raw[6];

    sensor_read(OUTX_L_G, raw, 6);
    int16_t raw_gx = (int16_t)(raw[1]<<8 | raw[0]);
    int16_t raw_gy = (int16_t)(raw[3]<<8 | raw[2]);
    int16_t raw_gz = (int16_t)(raw[5]<<8 | raw[4]);

    sensor_read(OUTX_L_XL, raw, 6);
    int16_t raw_ax = (int16_t)(raw[1]<<8 | raw[0]);
    int16_t raw_ay = (int16_t)(raw[3]<<8 | raw[2]);
    int16_t raw_az = (int16_t)(raw[5]<<8 | raw[4]);

    float ax = raw_ax * 0.000598f;
    float ay = raw_ay * 0.000598f;
    float az = raw_az * 0.000598f;
    float gx = raw_gx * 0.00875f;
    float gy = raw_gy * 0.00875f;
    float gz = raw_gz * 0.00875f;

    fax = ALPHA*fax + (1.0f-ALPHA)*ax;
    fay = ALPHA*fay + (1.0f-ALPHA)*ay;
    faz = ALPHA*faz + (1.0f-ALPHA)*az;
    fgx = ALPHA*fgx + (1.0f-ALPHA)*gx;
    fgy = ALPHA*fgy + (1.0f-ALPHA)*gy;
    fgz = ALPHA*fgz + (1.0f-ALPHA)*gz;

    SensorSample s;
    s.ax = fax; s.ay = fay; s.az = faz;
    s.gx = fgx; s.gy = fgy; s.gz = fgz;
    s.time_ms = (uint32_t)(Kernel::Clock::now().time_since_epoch().count());
    return s;
}