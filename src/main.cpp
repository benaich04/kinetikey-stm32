#include "mbed.h"
#include "sensor.h"
#include "gesture.h"

static BufferedSerial pc(USBTX, USBRX, 115200);
static Ticker sampler;
static volatile bool do_sample = false;

void on_tick() {
    do_sample = true;
}

int main() {
    // Init sensor
    Sensor_Init();

    // Calibrate — waits for button press
    Gesture_Calibrate();

    // Start 50Hz sampling
    sampler.attach(&on_tick, 20ms);

    while (true) {
        if (do_sample) {
            do_sample = false;
            SensorSample s = getFilteredSample();
            Gesture g = detectGesture(s);

            if (g != NONE) {
                const char* name = gestureName(g);
                pc.write("Gesture: ", 9);
                pc.write(name, strlen(name));
                pc.write("\r\n", 2);
            }
        }
    }
}