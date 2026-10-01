# Delivery Box — final firmware (ESP32-S3)

LED and face logic from `delivery-box`, door motor logic and pin map from
`delivery-box-esp32s3`, in one small project.

## Build and flash

```bash
. ~/.espressif/tools/activate_idf_v5.2.7.sh
cd ~/delivery-box-final
idf.py build
idf.py -p /dev/ttyACM0 flash monitor        # exit the monitor with Ctrl+]
```

**Power up with the door closed.** The shaft position at power-up is taken as
"closed" (0°).

## Where things are

| File | What it does |
|---|---|
| `main/main.c` | starts everything, then loops through the states |
| `main/states.c` | one function per robot state |
| `main/motor.c` | easy door/motor functions |
| `main/led.c` | the LED strip (134 LEDs): fade, pulse, blink, comet, fill |
| `main/face.c` | the face (eyes + mouth, one MOSFET) |
| `main/tail_light.c` | tail lights, on/off |
| `main/config.h` | every number you may want to tune |
| `main/board.h` | every pin |
| `components/door` | door control internals, copied unchanged from `delivery-box-esp32s3` |
| `components/ina228` | motor-current sensor driver, copied unchanged |

## States

Call a function and the whole box switches to that state:

| Function | Strips | Face | Tail | Door |
|---|---|---|---|---|
| `start_up()` | white pulse | thinking | off | – |
| `moving_with_task()` | blue pulse | on | off | closes |
| `moving_without_task()` | white pulse | on | off | closes |
| `success_waiting()` | green pulse | on | off | – |
| `success_open_door()` | green solid | on | off | opens |
| `failure()` | red pulse | on | brake | stops |
| `failure_not_critical()` | yellow pulse | on | off | – |
| `charging(ms)` | cyan comet | follows the comet | off | – |

All return at once except `charging(ms)`, which runs for `ms` milliseconds.
If the door stays blocked after all retries, the box switches to
`failure_not_critical()` by itself.

## Controlling the motor

Start-up values, at the top of `main/config.h`:

```c
#define DOOR_OPEN_ANGLE_DEG   90.0f   // how far the shaft turns to open the door
#define DOOR_OPEN_RPM         3.0f    // shaft speed when opening (3 = slowest smooth)
#define DOOR_CLOSE_RPM        3.0f    // shaft speed when closing
#define DOOR_CLOSE_EXTRA_DEG  20.0f   // closing may go up to 110 deg (90 + 20) looking for the switch
#define DOOR_SEAT_MS          1000    // after the door switch closes: keep closing 1 s, stop
#define DOOR_LATCH_DELAY_MS   1000    // 1 s after the door switch closes: latch locks
#define DOOR_OPEN_NUDGE_DEG   15.0f   // before opening: this far clockwise first
#define MOTOR_DIR_LEVEL_OPEN  0       // 0 = opens counterclockwise; flip if it opens the wrong way
```

More switches in the same place:

```c
#define TEST_RUN              1       // 1 = test run (see main.c), 0 = all robot states
#define STATES_MOVE_DOOR      1       // 0 = states change only the lights (LED check)
#define MOTOR_LOG_EVERY_MS    200     // status line while the door moves, 0 = off
```

With `TEST_RUN 1` the box runs this sequence (times in `config.h`):

| Step | What | Strip | Tail lights | Door / solenoid |
|---|---|---|---|---|
| 1 | wait 5 s after the ESP32 starts (once) | off | off | closed, locked |
| 2 | `start_up()`, 3 s | white pulse | DRL | closed, locked |
| 3 | open | green pulse | DRL | 15° clockwise with the solenoid on at the same time → opens counterclockwise to +90° |
| 4 | wait 3 s | green pulse | DRL | open |
| 5 | close | green pulse | DRL | closes until the door switch closes → 1 s: solenoid off (latch locks) → 0.3 s later motor stops → shaft still → angle zeroed |

The face (eyes + mouth) is solid on at 5 % brightness from step 2 on.

Then it repeats from step 2. If the door does not reach open or closed, the
motor stops, the strip turns yellow (`failure_not_critical`), the DRL stays
on, and the test ends until the next reset. The status line shows
angle, rpm, PWM duty, motor supply voltage, current, and the FG pin
(made-up numbers, to show the layout):

```
motor: opening  45.0 deg  3.0 rpm duty 10% dir +1 | 12.0 V 0.120 A (limit 0.220) | FG pin 1, 118 edges
```

"FG edges" must grow while the shaft turns. If it does not, the ESP32 gets
no position pulses and cannot stop at the right angle.

While running (`main/motor.h`):

```c
motor_set_open_angle(120);      // "open" now means 120 degrees
motor_set_speed(10, 5);         // open at 10 rpm, close at 5 rpm

motor_open_door();              // start opening, returns at once
motor_close_door();
motor_stop();
motor_wait_done(10000);         // wait for the move to end (true = arrived)

motor_rotate(45, 6);            // turn 45 degrees further open at 6 rpm, waits
motor_rotate(-45, 6);           // and back
motor_get_angle();              // where the shaft is now, in degrees
```

Angles and speeds are at the motor's output shaft. Top speed is
`MOTOR_RPM_AT_FULL_DUTY` (48 rpm).

Every move retracts the latch first, stops before reversing, and backs off
and retries if the motor current shows that something is in the way.

## Pins

| GPIO | Signal |
|---|---|
| 4 | LED strip data (one WS2815 strip, 134 LEDs) |
| 12 | face: eyes + mouth (one XY-MOS TRIG/PWM) |
| 18 / 21 / 40 / 38 | motor PWM / DIR / BRAKE / FG |
| 41 | door latch solenoid (XY-MOS) |
| 8, 9, 10, 11 | tail light brake / run / left / right |
| 1, 2 | I2C SDA / SCL (INA228 motor-current sensor) |
| 5 | door-closed switch to GND (closed = door closed = LOW) |
# delivery-box-digital-bridge
