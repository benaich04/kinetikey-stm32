# KinetiKey — STM32 Gesture Lock System

KinetiKey is an STM32-based gesture lock system that lets users record and unlock using a custom 3-step motion key made from tilts, shakes, slides, and rotations.

Instead of typing a password, the user performs physical gestures with the board. The system records a sequence of three gestures and later unlocks only when the same gestures are repeated in the correct order.

---

## Project Overview

KinetiKey uses motion data from the STM32 board to detect user gestures and compare them against a saved gesture key.

The system supports:

- Calibration to learn the board's resting state
- Recording a new 3-gesture key
- Unlocking by repeating the saved gesture sequence
- LED feedback for every system state
- Serial monitor debugging at 115200 baud
- Timeout and cancel behavior for safer user interaction

---

## User Guide Blueprint

The following blueprint summarizes the complete user flow, LED signals, gesture types, and hardware setup.

> Save the generated image inside your project as:
>
> `docs/kinetikey_user_guide_infographic.png`

![KinetiKey User Guide Blueprint](docs/kinetikey_user_guide_infographic.png)

---

## How to Use KinetiKey

### 1. Calibration

1. Plug the STM32 board into your Mac using USB.
2. Open the serial monitor at 115200 baud.
3. Place the board flat on a table.
4. Do not touch the board.
5. Press the blue button once.
6. When the LED goes solid for one second, calibration is complete.

Calibration is important because the system needs to know what “still” looks like before it can detect gestures correctly.

---

### 2. Recording a Gesture Key

1. Long-press the blue button for more than one second.
2. Release the button.
3. The LED flashes 3 times to show that record mode started.
4. Perform Gesture 1.
5. Return the board to rest until it is still.
6. The LED blinks once to confirm Gesture 1.
7. Perform Gesture 2.
8. Return the board to rest until it is still.
9. The LED blinks twice to confirm Gesture 2.
10. Perform Gesture 3.
11. Return the board to rest until it is still.
12. The LED blinks 3 times to confirm Gesture 3.
13. The LED goes solid briefly.
14. The key is saved.

After the key is saved, LED1 stays on to show that a gesture key exists.

---

### 3. Unlocking

1. Quickly tap the blue button for less than one second.
2. The LED flashes twice to show that unlock mode started.
3. Repeat the same 3 gestures in the same order.
4. Pause briefly after each gesture until the board is still.
5. If all gestures match, the LED goes solid for 3 seconds.
6. If any gesture is wrong, the LED blinks rapidly for about 2.5 seconds.

---

## Gesture Types

| Gesture | How to Perform It |
|---|---|
| Tilt | Slowly lean the board in one direction and hold it briefly. |
| Shake | Quickly jerk the board in one direction. |
| Slide | Smoothly push the board across the table. |
| Rotation | Twist the board while keeping it flat, like turning a dial. |

---

## Important Rule

Do one gesture, then return the board to rest before performing the next gesture.

The system only accepts a new gesture after it detects that the board has stopped moving. A short pause of about half a second is enough.

---

## System Flowchart

```mermaid
flowchart TD
    A([Start]) --> B[Plug STM32 board into Mac via USB]
    B --> C[Open serial monitor at 115200 baud]
    C --> D[Place board flat on table]
    D --> E[Do not touch the board]
    E --> F[Press blue button once]

    F --> G{LED goes solid<br/>for 1 second?}
    G -- Yes --> H[Calibration complete]
    G -- No --> D

    H --> I{What do you want to do?}

    I -- Record new key --> J[Long-press blue button<br/>for more than 1 second]
    J --> K[Release button]
    K --> L[LED flashes 3 times<br/>Recording mode started]

    L --> M[Perform Gesture 1]
    M --> N[Return board to rest<br/>hold still briefly]
    N --> O[LED blinks once]

    O --> P[Perform Gesture 2]
    P --> Q[Return board to rest<br/>hold still briefly]
    Q --> R[LED blinks twice]

    R --> S[Perform Gesture 3]
    S --> T[Return board to rest<br/>hold still briefly]
    T --> U[LED blinks 3 times]

    U --> V[LED goes solid briefly]
    V --> W[Key saved]
    W --> X[LED1 stays ON<br/>to show a key exists]
    X --> I

    I -- Unlock --> Y{Has a key been recorded?}
    Y -- No --> Z[LED flashes quickly 5 times]
    Z --> I

    Y -- Yes --> AA[Quick-tap blue button<br/>less than 1 second]
    AA --> AB[LED flashes twice<br/>Unlock mode started]

    AB --> AC[Repeat Gesture 1]
    AC --> AD[Return board to rest<br/>hold still briefly]
    AD --> AE[LED blinks once]

    AE --> AF[Repeat Gesture 2]
    AF --> AG[Return board to rest<br/>hold still briefly]
    AG --> AH[LED blinks twice]

    AH --> AI[Repeat Gesture 3]
    AI --> AJ[Return board to rest<br/>hold still briefly]
    AJ --> AK[LED blinks 3 times]

    AK --> AL{Do all 3 gestures<br/>match the saved key?}
    AL -- Yes --> AM[LED solid for 3 seconds]
    AM --> AN([Unlocked successfully])

    AL -- No --> AO[LED blinks rapidly<br/>for about 2.5 seconds]
    AO --> AP([Unlock failed])

    AN --> I
    AP --> I

    L -. No gesture within 10 seconds .-> AQ[Timeout]
    AB -. No gesture within 10 seconds .-> AQ
    AQ --> I

    L -. Button pressed during mode .-> AR[Cancel and return to idle]
    AB -. Button pressed during mode .-> AR
    AR --> I
````

---

## LED Signals

| LED Behavior                   | Meaning                      |
| ------------------------------ | ---------------------------- |
| Solid for 1 second             | Calibration complete         |
| 3 quick flashes                | Record mode started          |
| 2 quick flashes                | Unlock mode started          |
| Solid for 3 seconds            | Unlock successful            |
| Rapid blinking for 2.5 seconds | Unlock failed                |
| 5 quick flashes                | No key has been recorded yet |
| LED1 stays on                  | A key exists                 |

---

## Timeout and Cancel Behavior

* If no gesture is performed within 10 seconds, the system returns to idle.
* If the button is pressed during recording or unlocking, the current mode is cancelled.
* Long-pressing the button again records a new key and overwrites the old one.
* The serial monitor prints detected gestures and system status for debugging.

---

## Serial Monitor

Open the serial monitor using:

```bash
pio device monitor
```

Baud rate:

```text
115200
```

The serial monitor can be used to check:

* Calibration status
* Current mode
* Detected gestures
* Timeout events
* Unlock success or failure

---

## Repository Structure

```text
kinetikey-stm32/
├── README.md
├── docs/
│   └── kinetikey_user_guide_infographic.png
├── src/
│   └── main.cpp
├── include/
├── lib/
├── test/
└── platformio.ini
```

---

## Hardware Used

* STM32 development board
* Built-in blue user button
* Built-in status LED
* Built-in LED1 indicator
* USB cable
* Mac or computer with PlatformIO installed

---

## Goal

The goal of KinetiKey is to create a simple, physical, motion-based authentication system using embedded sensing, gesture detection, and clear user feedback through LEDs and serial output.

```
```
