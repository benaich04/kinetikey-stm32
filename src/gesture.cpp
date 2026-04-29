#include "gesture.h"
#include "mbed.h"
#include <cmath>

// ─────────────────────────────────────────────
//  Calibration baseline
// ─────────────────────────────────────────────
static float base_ax=0, base_ay=0, base_az=0;
static float base_gx=0, base_gy=0, base_gz=0;

// ─────────────────────────────────────────────
//  Velocity integration for slide detection
// ─────────────────────────────────────────────
static float vel_x=0, vel_y=0, vel_z=0;

// ─────────────────────────────────────────────
//  State machine
//
//  IDLE      →  board is at rest, looking for gestures
//  SETTLING  →  gesture just fired, waiting for ALL motion
//               to die down before accepting anything new.
//               Braking deceleration is absorbed here.
// ─────────────────────────────────────────────
enum MotionState { M_IDLE, M_SETTLING };
static MotionState motion_state = M_IDLE;
static uint32_t    quiet_count = 0;
static uint32_t    last_sample_time = 0;

// ─────────────────────────────────────────────
//  Previous-sample memory for rise detection
// ─────────────────────────────────────────────
static float prev_ax=0, prev_ay=0, prev_az=0;
static float prev_gx=0, prev_gy=0, prev_gz=0;

// ─────────────────────────────────────────────
//  Tuning constants
// ─────────────────────────────────────────────
#define ACCEL_TILT_THRESH      2.5f
#define ACCEL_SHAKE_THRESH     7.0f
#define GYRO_THRESH           60.0f
#define SLIDE_VEL_THRESH       0.08f

#define NOISE_FLOOR_XY         0.5f
#define NOISE_FLOOR_Z          1.5f
#define VEL_DECAY              0.85f
#define VEL_CLAMP              2.0f

#define QUIET_ACCEL_THRESH     0.8f
#define QUIET_GYRO_THRESH     25.0f
#define QUIET_VEL_THRESH       0.03f
#define QUIET_SAMPLES_NEEDED   5      // ~100ms at 50Hz

#define MIN_IDLE_MS          100

static uint32_t idle_entry_time = 0;

// ─────────────────────────────────────────────
//  Calibration (called by main after button press)
//  Pure math — no button/LED handling here.
// ─────────────────────────────────────────────
void Gesture_Calibrate(void) {
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
    base_az = sum_az / 50.0f;
    base_gx = sum_gx / 50.0f;
    base_gy = sum_gy / 50.0f;
    base_gz = sum_gz / 50.0f;

    // Reset all detection state
    vel_x = vel_y = vel_z = 0;
    prev_ax = prev_ay = prev_az = 0;
    prev_gx = prev_gy = prev_gz = 0;
    motion_state = M_IDLE;
    quiet_count = 0;
    last_sample_time = 0;
    idle_entry_time = 0;
}

// ─────────────────────────────────────────────
//  Gesture detection
// ─────────────────────────────────────────────
Gesture detectGesture(SensorSample sample) {

    // Subtract baseline (removes gravity)
    float ax = sample.ax - base_ax;
    float ay = sample.ay - base_ay;
    float az = sample.az - base_az;
    float gx = sample.gx - base_gx;
    float gy = sample.gy - base_gy;
    float gz = sample.gz - base_gz;

    // Time delta
    float dt = 0.02f;
    if (last_sample_time != 0) {
        float measured = (sample.time_ms - last_sample_time) / 1000.0f;
        if (measured > 0.005f && measured < 0.2f) dt = measured;
    }
    last_sample_time = sample.time_ms;
    uint32_t now = sample.time_ms;

    // Integrate velocity (always runs, even while settling)
    if (fabsf(ax) > NOISE_FLOOR_XY) vel_x += ax * dt;
    else                             vel_x *= VEL_DECAY;
    if (fabsf(ay) > NOISE_FLOOR_XY) vel_y += ay * dt;
    else                             vel_y *= VEL_DECAY;
    if (fabsf(az) > NOISE_FLOOR_Z)  vel_z += az * dt;
    else                             vel_z *= VEL_DECAY;

    if (fabsf(vel_x) > VEL_CLAMP) vel_x = 0;
    if (fabsf(vel_y) > VEL_CLAMP) vel_y = 0;
    if (fabsf(vel_z) > VEL_CLAMP) vel_z = 0;

    // ── SETTLING: wait for quiet ──
    if (motion_state == M_SETTLING) {
        bool is_quiet =
            fabsf(ax) < QUIET_ACCEL_THRESH &&
            fabsf(ay) < QUIET_ACCEL_THRESH &&
            fabsf(az) < QUIET_ACCEL_THRESH &&
            fabsf(gx) < QUIET_GYRO_THRESH  &&
            fabsf(gy) < QUIET_GYRO_THRESH  &&
            fabsf(gz) < QUIET_GYRO_THRESH  &&
            fabsf(vel_x) < QUIET_VEL_THRESH &&
            fabsf(vel_y) < QUIET_VEL_THRESH &&
            fabsf(vel_z) < QUIET_VEL_THRESH;

        if (is_quiet) {
            quiet_count++;
            if (quiet_count >= QUIET_SAMPLES_NEEDED) {
                motion_state = M_IDLE;
                idle_entry_time = now;
                vel_x = vel_y = vel_z = 0;
            }
        } else {
            quiet_count = 0;
        }

        prev_ax = ax; prev_ay = ay; prev_az = az;
        prev_gx = gx; prev_gy = gy; prev_gz = gz;
        return NONE;
    }

    // ── M_IDLE: detect gestures ──

    // Anti-jitter guard after settling
    if (idle_entry_time != 0 && (now - idle_entry_time) < MIN_IDLE_MS) {
        prev_ax = ax; prev_ay = ay; prev_az = az;
        prev_gx = gx; prev_gy = gy; prev_gz = gz;
        return NONE;
    }

    // Score candidates, pick strongest
    Gesture best_g = NONE;
    float   best_i = 0.0f;

    auto check = [&](Gesture g, float intensity) {
        if (intensity > best_i) { best_g = g; best_i = intensity; }
    };

    // ROTATIONS — require signal is rising
    if (fabsf(gz) > GYRO_THRESH && fabsf(gz) >= fabsf(prev_gz))
        check(gz > 0 ? ROTATE_CW  : ROTATE_CCW,  fabsf(gz));
    if (fabsf(gx) > GYRO_THRESH && fabsf(gx) >= fabsf(prev_gx))
        check(gx > 0 ? ROLL_RIGHT : ROLL_LEFT,    fabsf(gx));
    if (fabsf(gy) > GYRO_THRESH && fabsf(gy) >= fabsf(prev_gy))
        check(gy > 0 ? PITCH_UP   : PITCH_DOWN,   fabsf(gy));

    // SHAKES — require accel is rising
    if (fabsf(ax) > ACCEL_SHAKE_THRESH && fabsf(ax) >= fabsf(prev_ax))
        check(ax > 0 ? SHAKE_RIGHT    : SHAKE_LEFT,     fabsf(ax) * 8.0f);
    if (fabsf(ay) > ACCEL_SHAKE_THRESH && fabsf(ay) >= fabsf(prev_ay))
        check(ay > 0 ? SHAKE_FORWARD  : SHAKE_BACKWARD, fabsf(ay) * 8.0f);
    if (fabsf(az) > ACCEL_SHAKE_THRESH && fabsf(az) >= fabsf(prev_az))
        check(az > 0 ? SHAKE_UP       : SHAKE_DOWN,     fabsf(az) * 8.0f);

    // SLIDES — vel & accel must agree in direction
    if (fabsf(ax) < ACCEL_SHAKE_THRESH && fabsf(vel_x) > SLIDE_VEL_THRESH)
        if (vel_x * ax > 0 || fabsf(ax) < NOISE_FLOOR_XY)
            check(vel_x > 0 ? SLIDE_RIGHT   : SLIDE_LEFT,     fabsf(vel_x) * 50.0f);
    if (fabsf(ay) < ACCEL_SHAKE_THRESH && fabsf(vel_y) > SLIDE_VEL_THRESH)
        if (vel_y * ay > 0 || fabsf(ay) < NOISE_FLOOR_XY)
            check(vel_y > 0 ? SLIDE_FORWARD : SLIDE_BACKWARD, fabsf(vel_y) * 50.0f);
    if (fabsf(az) < ACCEL_SHAKE_THRESH && fabsf(vel_z) > SLIDE_VEL_THRESH)
        if (vel_z * az > 0 || fabsf(az) < NOISE_FLOOR_Z)
            check(vel_z > 0 ? SLIDE_UP      : SLIDE_DOWN,     fabsf(vel_z) * 50.0f);

    // TILTS — sustained lean
    if (fabsf(ay) > ACCEL_TILT_THRESH && fabsf(ay) < ACCEL_SHAKE_THRESH)
        check(ay > 0 ? TILT_UP    : TILT_DOWN,  fabsf(ay) * 2.0f);
    if (fabsf(ax) > ACCEL_TILT_THRESH && fabsf(ax) < ACCEL_SHAKE_THRESH)
        check(ax > 0 ? TILT_RIGHT : TILT_LEFT,  fabsf(ax) * 2.0f);

    // Accepted → enter settling
    if (best_g != NONE) {
        motion_state = M_SETTLING;
        quiet_count = 0;
        vel_x = vel_y = vel_z = 0;
    }

    prev_ax = ax; prev_ay = ay; prev_az = az;
    prev_gx = gx; prev_gy = gy; prev_gz = gz;

    return best_g;
}

// ─────────────────────────────────────────────
//  Gesture names
// ─────────────────────────────────────────────
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