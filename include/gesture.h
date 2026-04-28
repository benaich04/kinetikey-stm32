#ifndef GESTURE_H
#define GESTURE_H

#include "sensor.h"

enum Gesture {
    NONE,
    // Tilts (sustained angle)
    TILT_UP,
    TILT_DOWN,
    TILT_LEFT,
    TILT_RIGHT,
    // Shakes (fast sudden snap)
    SHAKE_LEFT,
    SHAKE_RIGHT,
    SHAKE_UP,
    SHAKE_DOWN,
    SHAKE_FORWARD,
    SHAKE_BACKWARD,
    // Slides (slow deliberate movement)
    SLIDE_LEFT,
    SLIDE_RIGHT,
    SLIDE_UP,
    SLIDE_DOWN,
    SLIDE_FORWARD,
    SLIDE_BACKWARD,
    // Rotations (gyroscope)
    ROTATE_CW,
    ROTATE_CCW,
    ROLL_LEFT,
    ROLL_RIGHT,
    PITCH_UP,
    PITCH_DOWN
};

void Gesture_Calibrate(void);
Gesture detectGesture(SensorSample sample);
const char* gestureName(Gesture g);

#endif