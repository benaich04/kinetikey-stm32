#include "gesture.h"
#include "mbed.h"
#include <cmath>

static InterruptIn user_button(PC_13);
static volatile bool button_pressed = false;

// Calibration baseline
static float base_ax=0, base_ay=0, base_az=0;
static float base_gx=0, base_gy=0, base_gz=0;

// Velocity estimates for slide detection
static float vel_x=0, vel_y=0, vel_z=0;

// Debounce
static uint32_t last_gesture_time = 0;
static uint32_t last_sample_time  = 0;

// Thresholds
#define ACCEL_TILT_THRESH      2.5f
#define ACCEL_SHAKE_THRESH     7.0f
#define ACCEL_SLIDE_THRESH     1.5f
#define GYRO_THRESH           60.0f
#define SLIDE_VEL_THRESH       0.08f
#define VEL_DECAY              0.85f
#define DEBOUNCE_MS            400
#define NOISE_FLOOR_XY         0.5f
#define NOISE_FLOOR_Z          1.5f
#define VEL_CLAMP              2.0f

void on_button_press() {
    button_pressed = true;
}

void Gesture_Calibrate(void) {
    user_button.fall(&on_button_press);

    DigitalOut led(PB_14);
    while (!button_pressed) {
        led = !led;
        ThisThread::sleep_for(200ms);
    }
    led = 0;
    button_pressed = false;

    // Average 50 samples as baseline
    float sum_ax=0, sum_ay=0, sum_az=0;
    float sum_gx=0, sum_gy=0, sum_gz=0;

    for (int i = 0; i < 50; i++) {
        SensorSample s = getFilteredSample();
        sum_ax += s.ax; sum_ay += s.ay; sum_az += s.az;
        sum_gx += s.gx; sum_gy += s.gy; sum_gz += s.gz;
        ThisThread::sleep_for(20ms);
    }

    base_ax = sum_ax / 50.0f;
    base_ay = sum_ay / 50.0f;
    base_az = sum_az / 50.0f;  // this captures gravity (~9.8) on Z
    base_gx = sum_gx / 50.0f;
    base_gy = sum_gy / 50.0f;
    base_gz = sum_gz / 50.0f;

    // Reset everything
    vel_x = vel_y = vel_z = 0;
    last_sample_time = 0;
    last_gesture_time = 0;

    led = 1;
    ThisThread::sleep_for(1000ms);
    led = 0;
}

Gesture detectGesture(SensorSample sample) {
    // Subtract baseline — this removes gravity from az automatically
    float ax = sample.ax - base_ax;
    float ay = sample.ay - base_ay;
    float az = sample.az - base_az;
    float gx = sample.gx - base_gx;
    float gy = sample.gy - base_gy;
    float gz = sample.gz - base_gz;

    // Time delta
    float dt = 0.02f;
    if (last_sample_time != 0) {
        float measured_dt = (sample.time_ms - last_sample_time) / 1000.0f;
        if (measured_dt > 0.005f && measured_dt < 0.2f)
            dt = measured_dt;
    }
    last_sample_time = sample.time_ms;

    // Integrate acceleration to velocity
    // Use larger dead zone on Z to kill any residual gravity noise
    if (fabsf(ax) > NOISE_FLOOR_XY) vel_x += ax * dt;
    else                             vel_x *= VEL_DECAY;

    if (fabsf(ay) > NOISE_FLOOR_XY) vel_y += ay * dt;
    else                             vel_y *= VEL_DECAY;

    if (fabsf(az) > NOISE_FLOOR_Z)  vel_z += az * dt;
    else                             vel_z *= VEL_DECAY;

    // Clamp velocity to prevent runaway drift
    if (fabsf(vel_x) > VEL_CLAMP) vel_x = 0;
    if (fabsf(vel_y) > VEL_CLAMP) vel_y = 0;
    if (fabsf(vel_z) > VEL_CLAMP) vel_z = 0;

    // Debounce
    uint32_t now = sample.time_ms;
    if ((now - last_gesture_time) < DEBOUNCE_MS) return NONE;

    // Score every possible gesture and pick highest intensity
    Gesture best_g = NONE;
    float   best_i = 0.0f;

    auto check = [&](Gesture g, float intensity) {
        if (intensity > best_i) {
            best_g = g;
            best_i = intensity;
        }
    };

    // --- ROTATIONS (gyro, deg/s) ---
    if (fabsf(gz) > GYRO_THRESH)
        check(gz > 0 ? ROTATE_CW  : ROTATE_CCW,  fabsf(gz));
    if (fabsf(gx) > GYRO_THRESH)
        check(gx > 0 ? ROLL_RIGHT : ROLL_LEFT,    fabsf(gx));
    if (fabsf(gy) > GYRO_THRESH)
        check(gy > 0 ? PITCH_UP   : PITCH_DOWN,   fabsf(gy));

    // --- SHAKES (sudden high acceleration, scaled up to compete with gyro) ---
    if (fabsf(ax) > ACCEL_SHAKE_THRESH)
        check(ax > 0 ? SHAKE_RIGHT    : SHAKE_LEFT,     fabsf(ax) * 8.0f);
    if (fabsf(ay) > ACCEL_SHAKE_THRESH)
        check(ay > 0 ? SHAKE_FORWARD  : SHAKE_BACKWARD, fabsf(ay) * 8.0f);
    if (fabsf(az) > ACCEL_SHAKE_THRESH)
        check(az > 0 ? SHAKE_UP       : SHAKE_DOWN,     fabsf(az) * 8.0f);

    // --- SLIDES (slow movement via velocity, only if accel is low = not a shake) ---
    if (fabsf(ax) < ACCEL_SHAKE_THRESH && fabsf(vel_x) > SLIDE_VEL_THRESH)
        check(vel_x > 0 ? SLIDE_RIGHT    : SLIDE_LEFT,     fabsf(vel_x) * 50.0f);
    if (fabsf(ay) < ACCEL_SHAKE_THRESH && fabsf(vel_y) > SLIDE_VEL_THRESH)
        check(vel_y > 0 ? SLIDE_FORWARD  : SLIDE_BACKWARD, fabsf(vel_y) * 50.0f);
    if (fabsf(az) < ACCEL_SHAKE_THRESH && fabsf(vel_z) > SLIDE_VEL_THRESH)
        check(vel_z > 0 ? SLIDE_UP       : SLIDE_DOWN,     fabsf(vel_z) * 50.0f);

    // --- TILTS (sustained angle, only if acceleration is between noise and shake) ---
    if (fabsf(ay) > ACCEL_TILT_THRESH && fabsf(ay) < ACCEL_SHAKE_THRESH)
        check(ay > 0 ? TILT_UP    : TILT_DOWN,  fabsf(ay) * 2.0f);
    if (fabsf(ax) > ACCEL_TILT_THRESH && fabsf(ax) < ACCEL_SHAKE_THRESH)
        check(ax > 0 ? TILT_RIGHT : TILT_LEFT,  fabsf(ax) * 2.0f);

    if (best_g != NONE) {
        last_gesture_time = now;
        vel_x = vel_y = vel_z = 0;
    }

    return best_g;
}

const char* gestureName(Gesture g) {
    switch(g) {
        case TILT_UP:        return "TILT_UP";
        case TILT_DOWN:      return "TILT_DOWN";
        case TILT_LEFT:      return "TILT_LEFT";
        case TILT_RIGHT:     return "TILT_RIGHT";
        case SHAKE_LEFT:     return "SHAKE_LEFT";
        case SHAKE_RIGHT:    return "SHAKE_RIGHT";
        case SHAKE_UP:       return "SHAKE_UP";
        case SHAKE_DOWN:     return "SHAKE_DOWN";
        case SHAKE_FORWARD:  return "SHAKE_FORWARD";
        case SHAKE_BACKWARD: return "SHAKE_BACKWARD";
        case SLIDE_LEFT:     return "SLIDE_LEFT";
        case SLIDE_RIGHT:    return "SLIDE_RIGHT";
        case SLIDE_UP:       return "SLIDE_UP";
        case SLIDE_DOWN:     return "SLIDE_DOWN";
        case SLIDE_FORWARD:  return "SLIDE_FORWARD";
        case SLIDE_BACKWARD: return "SLIDE_BACKWARD";
        case ROTATE_CW:      return "ROTATE_CW";
        case ROTATE_CCW:     return "ROTATE_CCW";
        case ROLL_LEFT:      return "ROLL_LEFT";
        case ROLL_RIGHT:     return "ROLL_RIGHT";
        case PITCH_UP:       return "PITCH_UP";
        case PITCH_DOWN:     return "PITCH_DOWN";
        default:             return "NONE";
    }
}