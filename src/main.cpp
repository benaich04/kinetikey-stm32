#include "mbed.h"
#include "sensor.h"
#include "gesture.h"
#include <cstring>
#include <cstdio>

// ══════════════════════════════════════════════
//  HARDWARE
// ══════════════════════════════════════════════
static BufferedSerial pc(USBTX, USBRX, 115200);
static DigitalOut led1(PA_5);       // LED1 — key-saved indicator
static DigitalOut led2(PB_14);      // LED2 — main status feedback
static InterruptIn button(PC_13);   // User button (active-low)
static Ticker sampler;

// ══════════════════════════════════════════════
//  HELPERS
// ══════════════════════════════════════════════
static void print(const char* msg) {
    pc.write(msg, strlen(msg));
}

static uint32_t now_ms() {
    return (uint32_t)(Kernel::Clock::now().time_since_epoch().count());
}

// Blocking blink — used for short feedback bursts
static void led_blink(int times, int on_ms = 200, int off_ms = 200) {
    for (int i = 0; i < times; i++) {
        led2 = 1;
        ThisThread::sleep_for(chrono::milliseconds(on_ms));
        led2 = 0;
        if (i < times - 1 || off_ms > 0)
            ThisThread::sleep_for(chrono::milliseconds(off_ms));
    }
}

// ══════════════════════════════════════════════
//  SYSTEM STATE MACHINE
// ══════════════════════════════════════════════
enum SystemState {
    SYS_IDLE,
    SYS_RECORDING,
    SYS_UNLOCKING,
    SYS_SUCCESS,
    SYS_FAIL
};

static volatile SystemState sys_state = SYS_IDLE;

// Gesture storage
static Gesture savedKey[3] = {NONE, NONE, NONE};
static Gesture attempt[3]  = {NONE, NONE, NONE};
static int     gesture_index = 0;
static bool    key_saved = false;

// Timing
static uint32_t state_entry_time   = 0;
static uint32_t gesture_wait_start = 0;
static uint32_t last_blink_toggle  = 0;

#define GESTURE_TIMEOUT_MS  10000    // 10s to perform each gesture
#define SUCCESS_DISPLAY_MS   3000    // solid LED for 3s on success
#define FAIL_DISPLAY_MS      2500    // fast blink for 2.5s on fail

// ══════════════════════════════════════════════
//  BUTTON — long press vs short press
//
//  fall (HIGH→LOW) = pressed down  → record timestamp
//  rise (LOW→HIGH) = released      → measure duration
//
//  >= 1000 ms  →  long press  →  RECORD mode
//  <  1000 ms  →  short press →  UNLOCK mode
// ══════════════════════════════════════════════
static volatile uint32_t btn_down_time = 0;
static volatile bool     btn_event     = false;
static volatile bool     btn_long      = false;

static void on_btn_fall() {
    btn_down_time = now_ms();
}

static void on_btn_rise() {
    uint32_t dur = now_ms() - btn_down_time;
    if (dur > 50) {                   // debounce
        btn_long  = (dur >= 1000);
        btn_event = true;
    }
}

// ══════════════════════════════════════════════
//  SAMPLING
// ══════════════════════════════════════════════
static volatile bool do_sample = false;
static void on_tick() { do_sample = true; }

// ══════════════════════════════════════════════
//  CALIBRATION BUTTON (simple flag for startup)
// ══════════════════════════════════════════════
static volatile bool cal_pressed = false;
static void on_cal_press() { cal_pressed = true; }

// ══════════════════════════════════════════════
//  MAIN
// ══════════════════════════════════════════════
int main() {
    led1 = 0;
    led2 = 0;

    Sensor_Init();

    // ────────────────────────────────────────
    //  STEP 1 — Calibration
    // ────────────────────────────────────────
    print("\r\n");
    print("========================================\r\n");
    print("   KinetiKey — Gesture-Based Lock\r\n");
    print("========================================\r\n");
    print("\r\n");
    print("CALIBRATION\r\n");
    print("  1. Place the board FLAT and STILL\r\n");
    print("  2. Press the blue button\r\n");
    print("  3. Keep it still until the LED goes solid\r\n");
    print("\r\n");
    print("Waiting for button press...\r\n");

    // Blink LED2 while waiting for calibration press
    button.fall(&on_cal_press);
    while (!cal_pressed) {
        led2 = !led2;
        ThisThread::sleep_for(200ms);
    }
    led2 = 0;
    ThisThread::sleep_for(100ms);     // debounce settle

    print("Calibrating... hold still...\r\n");
    Gesture_Calibrate();              // averages 50 samples (~1 second)

    // Confirmation feedback
    led2 = 1;
    ThisThread::sleep_for(1000ms);
    led2 = 0;

    print("Calibration complete!\r\n");
    print("\r\n");
    print("CONTROLS:\r\n");
    print("  LONG  press (hold >1s) = RECORD a new 3-gesture key\r\n");
    print("  SHORT press (tap)      = UNLOCK with your saved key\r\n");
    print("\r\n");
    print("HOW TO MOVE:\r\n");
    print("  - Do ONE gesture, then hold still, then the next\r\n");
    print("  - Tilt:   lean the board and hold it\r\n");
    print("  - Shake:  quick sharp jerk in one direction\r\n");
    print("  - Slide:  slow smooth push in one direction\r\n");
    print("  - Rotate: twist the board on its flat plane\r\n");
    print("\r\n");
    print("Waiting...\r\n");
    print("\r\n");

    // ────────────────────────────────────────
    //  STEP 2 — Wire up button for main loop
    // ────────────────────────────────────────
    button.fall(&on_btn_fall);
    button.rise(&on_btn_rise);

    // Start 50 Hz sampling
    sampler.attach(&on_tick, 20ms);

    sys_state = SYS_IDLE;
    last_blink_toggle = now_ms();

    // ────────────────────────────────────────
    //  STEP 3 — Main loop
    // ────────────────────────────────────────
    while (true) {
        uint32_t now = now_ms();

        switch (sys_state) {

        // ────────────────────────────────────
        //  IDLE — slow blink, wait for button
        // ────────────────────────────────────
        case SYS_IDLE: {

            // Slow blink LED2 (toggle every 500ms)
            if (now - last_blink_toggle >= 500) {
                led2 = !led2;
                last_blink_toggle = now;
            }

            // LED1 = key-saved indicator
            led1 = key_saved ? 1 : 0;

            // Keep sensor filter warm
            if (do_sample) {
                do_sample = false;
                SensorSample s = getFilteredSample();
                detectGesture(s);
            }

            // Check button
            if (btn_event) {
                btn_event = false;
                led2 = 0;

                if (btn_long) {
                    // ═══ ENTER RECORD MODE ═══
                    sys_state = SYS_RECORDING;
                    gesture_index = 0;

                    print("----------------------------------------\r\n");
                    print("  RECORD MODE — save a new 3-gesture key\r\n");
                    print("----------------------------------------\r\n");

                    // Entry feedback: 3 rapid blinks
                    led_blink(3, 150, 150);
                    ThisThread::sleep_for(300ms);

                    print("  Perform gesture 1 of 3...\r\n");
                    gesture_wait_start = now_ms();

                } else {
                    // ═══ ENTER UNLOCK MODE ═══
                    if (!key_saved) {
                        print("  No key saved yet!\r\n");
                        print("  Long-press the button to record one first.\r\n");
                        print("\r\n");
                        led_blink(5, 50, 50);   // error buzz
                    } else {
                        sys_state = SYS_UNLOCKING;
                        gesture_index = 0;

                        print("----------------------------------------\r\n");
                        print("  UNLOCK MODE — repeat your 3-gesture key\r\n");
                        print("----------------------------------------\r\n");

                        // Entry feedback: 2 rapid blinks
                        led_blink(2, 150, 150);
                        ThisThread::sleep_for(300ms);

                        print("  Perform gesture 1 of 3...\r\n");
                        gesture_wait_start = now_ms();
                    }
                }
            }
            break;
        }

        // ────────────────────────────────────
        //  RECORDING — collect 3 gestures as the key
        // ────────────────────────────────────
        case SYS_RECORDING: {

            // Timeout?
            if (now - gesture_wait_start > GESTURE_TIMEOUT_MS) {
                print("\r\n");
                print("  TIMEOUT — no gesture detected.\r\n");
                print("  Returning to idle...\r\n");
                print("\r\n");
                led_blink(2, 400, 400);
                sys_state = SYS_IDLE;
                last_blink_toggle = now_ms();
                break;
            }

            // Cancel on button press
            if (btn_event) {
                btn_event = false;
                print("  Cancelled.\r\n\r\n");
                sys_state = SYS_IDLE;
                last_blink_toggle = now_ms();
                break;
            }

            // Sample + detect
            if (do_sample) {
                do_sample = false;
                SensorSample s = getFilteredSample();
                Gesture g = detectGesture(s);

                if (g != NONE) {
                    savedKey[gesture_index] = g;

                    char buf[64];
                    snprintf(buf, sizeof(buf),
                             "  [%d/3] Recorded: %s\r\n",
                             gesture_index + 1, gestureName(g));
                    print(buf);

                    gesture_index++;

                    // Blink N times to confirm Nth gesture
                    led_blink(gesture_index, 200, 200);

                    if (gesture_index >= 3) {
                        // ── KEY SAVED ──
                        key_saved = true;
                        print("\r\n");
                        print("  KEY SAVED!\r\n");

                        char seq[100];
                        snprintf(seq, sizeof(seq),
                                 "  Your key: %s -> %s -> %s\r\n",
                                 gestureName(savedKey[0]),
                                 gestureName(savedKey[1]),
                                 gestureName(savedKey[2]));
                        print(seq);
                        print("\r\n");
                        print("  Short-press the button to unlock.\r\n");
                        print("\r\n");

                        // Solid LED for 1.5s to confirm save
                        led2 = 1;
                        ThisThread::sleep_for(1500ms);
                        led2 = 0;

                        sys_state = SYS_IDLE;
                        last_blink_toggle = now_ms();

                    } else {
                        char buf2[40];
                        snprintf(buf2, sizeof(buf2),
                                 "  Perform gesture %d of 3...\r\n",
                                 gesture_index + 1);
                        print(buf2);
                        gesture_wait_start = now_ms();
                    }
                }
            }
            break;
        }

        // ────────────────────────────────────
        //  UNLOCKING — collect 3, then compare all
        // ────────────────────────────────────
        case SYS_UNLOCKING: {

            // Timeout?
            if (now - gesture_wait_start > GESTURE_TIMEOUT_MS) {
                print("\r\n");
                print("  TIMEOUT — no gesture detected.\r\n");
                print("  Returning to idle...\r\n");
                print("\r\n");
                led_blink(2, 400, 400);
                sys_state = SYS_IDLE;
                last_blink_toggle = now_ms();
                break;
            }

            // Cancel on button press
            if (btn_event) {
                btn_event = false;
                print("  Cancelled.\r\n\r\n");
                sys_state = SYS_IDLE;
                last_blink_toggle = now_ms();
                break;
            }

            // Sample + detect
            if (do_sample) {
                do_sample = false;
                SensorSample s = getFilteredSample();
                Gesture g = detectGesture(s);

                if (g != NONE) {
                    attempt[gesture_index] = g;

                    char buf[64];
                    snprintf(buf, sizeof(buf),
                             "  [%d/3] Attempt: %s\r\n",
                             gesture_index + 1, gestureName(g));
                    print(buf);

                    gesture_index++;

                    // Blink N times to acknowledge Nth gesture
                    led_blink(gesture_index, 200, 200);

                    if (gesture_index >= 3) {
                        // ── COMPARE ALL 3 ──
                        ThisThread::sleep_for(400ms);
                        print("\r\n");
                        print("  Checking sequence...\r\n");

                        bool all_match = true;
                        for (int i = 0; i < 3; i++) {
                            bool ok = (attempt[i] == savedKey[i]);
                            if (!ok) all_match = false;

                            char cmp[80];
                            snprintf(cmp, sizeof(cmp),
                                     "    Gesture %d:  %s  vs  %s  — %s\r\n",
                                     i + 1,
                                     gestureName(attempt[i]),
                                     gestureName(savedKey[i]),
                                     ok ? "MATCH" : "WRONG");
                            print(cmp);
                        }

                        print("\r\n");
                        if (all_match) {
                            print("  *** UNLOCK SUCCESS! ***\r\n");
                            sys_state = SYS_SUCCESS;
                        } else {
                            print("  *** UNLOCK FAILED ***\r\n");
                            sys_state = SYS_FAIL;
                        }
                        print("\r\n");
                        state_entry_time = now_ms();
                        last_blink_toggle = now_ms();

                    } else {
                        char buf2[40];
                        snprintf(buf2, sizeof(buf2),
                                 "  Perform gesture %d of 3...\r\n",
                                 gesture_index + 1);
                        print(buf2);
                        gesture_wait_start = now_ms();
                    }
                }
            }
            break;
        }

        // ────────────────────────────────────
        //  SUCCESS — solid LED for 3 seconds
        // ────────────────────────────────────
        case SYS_SUCCESS: {
            led2 = 1;   // solid ON

            // Keep filter warm
            if (do_sample) {
                do_sample = false;
                SensorSample s = getFilteredSample();
                detectGesture(s);
            }

            if (now - state_entry_time >= SUCCESS_DISPLAY_MS) {
                led2 = 0;
                print("  Returning to idle...\r\n\r\n");
                sys_state = SYS_IDLE;
                last_blink_toggle = now_ms();
            }

            // Consume stale button events
            if (btn_event) btn_event = false;
            break;
        }

        // ────────────────────────────────────
        //  FAIL — fast blink for 2.5 seconds
        // ────────────────────────────────────
        case SYS_FAIL: {
            // Fast blink: toggle every 100ms
            if (now - last_blink_toggle >= 100) {
                led2 = !led2;
                last_blink_toggle = now;
            }

            // Keep filter warm
            if (do_sample) {
                do_sample = false;
                SensorSample s = getFilteredSample();
                detectGesture(s);
            }

            if (now - state_entry_time >= FAIL_DISPLAY_MS) {
                led2 = 0;
                print("  Returning to idle...\r\n\r\n");
                sys_state = SYS_IDLE;
                last_blink_toggle = now_ms();
            }

            // Consume stale button events
            if (btn_event) btn_event = false;
            break;
        }

        } // end switch
    } // end while
}