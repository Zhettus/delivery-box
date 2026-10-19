# Delivery Box — final firmware (ESP32-S3)

LED and face logic from `delivery-box`, door motor logic and pin map from
`delivery-box-esp32s3` (via `delivery-box-digital-bridge`), plus:

- **WM8960 Audio HAT** — speaker sounds per state, with the codec setup that
  worked together with the motor and the INA228 in `motor-audio-check`.
- **Door-held detection** — the INA228 measures the motor current. If someone
  holds the door while it moves, the current rises and the door goes back to
  its previous state: **closing → opens again**, **opening → closes again**
  (and the latch locks), then beeps.

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
| `main/sound.c` | the box's sounds (melodies + `main/gm.wav`) |
| `main/config.h` | every number you may want to tune |
| `main/board.h` | every pin |
| `components/door` | door control internals from `delivery-box-esp32s3`; going back on a held door added |
| `components/ina228` | motor-current sensor driver, copied unchanged |
| `components/audio` | WM8960 + I2S playback (WAV clips, tones), from `motor-audio-check` |
| `test/host` | PC unit tests of the door logic (`cd test/host && make`) |

## States

Call a function and the whole box switches to that state:

| Function | Strips | Face | Tail | Door | Sound |
|---|---|---|---|---|---|
| `start_up()` | white pulse | thinking | off | – | start-up notes |
| `moving_with_task()` | blue pulse | on | off | closes | – |
| `moving_without_task()` | white pulse | on | off | closes | – |
| `success_waiting()` | green pulse | on | off | – | two notes |
| `success_open_door()` | green solid | on | off | opens | `gm.wav` |
| `failure()` | red pulse | on | brake | stops | falling notes |
| `failure_not_critical()` | yellow pulse | on | off | – | two beeps |
| `charging(ms)` | cyan comet | follows the comet | off | – | – |

All return at once except `charging(ms)`, which runs for `ms` milliseconds.
If the door gets stuck both ways (or times out), the box switches to
`failure_not_critical()` by itself. `STATE_SOUNDS 0` in `config.h` silences
the states.

## Someone holds the door

While the door moves, the INA228 reads the motor current every 5 ms. The
door counts as **held** when the current stays above
`DOOR_CURRENT_LIMIT_A`, or `DOOR_CURRENT_RISE_A` above the normal running
current it learned for that direction, for `DOOR_CURRENT_CONFIRM_MS`
(the first `DOOR_CURRENT_IGNORE_MS` of every move are ignored: start-up
spike). A door much slower than commanded (`DOOR_OBSTRUCTION_*`) counts too.

| Door was | Held → it | Ends as |
|---|---|---|
| closing | stops, waits 0.5 s, opens again | `OPEN` |
| opening | stops, waits 0.5 s, closes again (seats on the switch, latch locks) | `CLOSED` |
| going back, and held again | stops where it is | fault `blocked` |

"Previous state" is the other end even if the move began half way (a close
that interrupted an open goes back to open). Then the box beeps
(`DOOR_HELD_SOUND`) and the door stays there until the next state moves it;
`DOOR_MAX_RETRIES > 0` makes it try again by itself after
`DOOR_RETRY_WAIT_MS`. In code: `motor_wait()` returns `MOTOR_WENT_BACK`, and
`motor_on_door_held()` sets a callback.

The current thresholds are still estimates **[MEASURE]**. To set them, run
`motor-audio-check` with `idf.py -DCURRENT_LOG=1 build flash monitor`: it opens
and closes the door, logs the current, and suggests values. Then hold the door
during a test run (`TEST_RUN 1`) and check the log shows
`door held while closing: back OPEN`.

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

Then it repeats from step 2. Hold the door to test the current guard: held
while opening it closes again and the test starts over at step 2; held while
closing it opens again and closes again after `TEST_OPEN_PAUSE_MS`. If the
door does not reach open or closed for any other reason, the
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

Every move retracts the latch first, stops before reversing, and goes back
to the previous state if the motor current shows that someone holds the door.

## Pins

| GPIO | Signal |
|---|---|
| 4 | LED strip data (one WS2815 strip, 134 LEDs) |
| 12 | face: eyes + mouth (one XY-MOS TRIG/PWM) |
| 18 / 21 / 40 / 38 | motor PWM / DIR / BRAKE / FG |
| 41 | door latch solenoid (XY-MOS) |
| 8, 9, 10, 11 | tail light brake / run / left / right |
| 1, 2 | I2C SDA / SCL, shared, 100 kHz: INA228 (0x40) + WM8960 HAT (0x1A) |
| 14, 15, 16 | I2S BCLK / WS / DOUT → WM8960 P12 / P35 / P40 |
| 17 | I2S DIN ← ICS43434 microphone (wired, not used yet) |
| 5 | door-closed switch to GND (closed = door closed = LOW) |
# delivery-box-digital-bridge
