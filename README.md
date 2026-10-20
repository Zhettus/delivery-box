# Delivery Box — final firmware (ESP32-S3)

LED and face logic from `delivery-box`, door motor logic and pin map from
`delivery-box-esp32s3` (via `delivery-box-digital-bridge`), plus:

- **WM8960 Audio HAT** — speaker sounds per state, with the codec setup that
  worked together with the motor and the INA228 in `motor-audio-check`.
- **Door-held detection** — the INA228 measures the motor current. If someone
  holds the door while it moves, the current rises and the door goes back to
  its previous state: **closing → opens again**, **opening → closes again**
  (and the latch locks), then beeps.
- **Microphone over micro-ROS** — the ICS43434 is streamed live to ROS 2
  (`/delivery_box/mic/audio`, 16 kHz), switched on and off by a topic. From
  `mic_ros_handoff`.

## Build and flash

```bash
. ~/.espressif/tools/activate_idf_v5.2.7.sh
cd ~/delivery-box-final
idf.py build
idf.py -p /dev/ttyACM0 flash monitor        # exit the monitor with Ctrl+]
```

**Audio HAT check:** `idf.py -DAUDIO_CHECK=1 build flash monitor` builds
`main/audio_check.c` instead of the firmware: it plays `main/gm.wav` in a loop
(motor held stopped) and logs whether the WM8960 answers. Back to the firmware:
`idf.py -DAUDIO_CHECK=0 build flash`.

**Microphone check:** `idf.py -DMIC_CHECK=1 build flash monitor` builds
`main/mic_check.c`: a level meter for the ICS43434 in the log (two lines a
second), with a wiring diagnosis (no data, stuck line, SEL on the wrong side,
clock wires swapped). With the audio HAT working it also records 3 s every 10 s
("speak now") and plays it back. The HAT is not needed for the meter. Back to
the firmware: `idf.py -DMIC_CHECK=0 build flash`. Only one check at a time.

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
| `main/mic_ros.c` | microphone -> micro-ROS stream (built only with micro-ROS) |
| `main/usb_jtag_transport.c` | micro-ROS transport over the native USB port |
| `tools/listen_live.py` | on the computer: switch the mic on, play it, save a WAV |
| `main/config.h` | every number you may want to tune |
| `main/board.h` | every pin |
| `components/door` | door control internals from `delivery-box-esp32s3`; going back on a held door added |
| `components/ina228` | motor-current sensor driver, copied unchanged |
| `components/audio` | one full-duplex I2S port: WM8960 playback (from `motor-audio-check`) + ICS43434 capture and `mic_dsp` (from `mic_ros_handoff`) |
| `components/micro_ros_espidf_component` | micro-ROS (NOT in git, copy it in: see "Microphone") |
| `test/host` | PC unit tests: door logic + mic DSP (`cd test/host && make`) |

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

## Microphone (micro-ROS)

The ICS43434 shares the I2S clocks with the speaker, so both run at
`AUDIO_SAMPLE_RATE_HZ` (48 kHz). Capture: 48 kHz → high-pass, automatic gain,
limiter → 16 kHz → 10 ms chunks → micro-ROS. The mic is **off** after boot and
after every reconnect; nothing is sent until someone switches it on.

| Topic | Type | Content |
|---|---|---|
| `/delivery_box/mic/enable` | `std_msgs/Bool` (in) | `true` = stream, `false` = stop |
| `/delivery_box/mic/status` | `std_msgs/Bool` (out) | real state, on change + every 1 s |
| `/delivery_box/mic/audio` | `std_msgs/Int16MultiArray` (out, best effort) | 160 samples = 10 ms of 16 kHz mono |

**micro-ROS is only built when `components/micro_ros_espidf_component`
exists.** It is ~475 MB, so it is not in git. Copy it from the working project
(it has the `jazzy` branch plus the local `libmicroros.mk` fix):

```bash
rsync -a ~/delivery-box-esp32s3-microros/components/micro_ros_espidf_component/ \
         components/micro_ros_espidf_component/
rm -f sdkconfig && idf.py fullclean      # once: the console moves to UART0
idf.py build
```

On a new computer, follow `MICRO_ROS_NEW_COMPUTER_SETUP.md` (the first build
of the library takes several minutes). `app-colcon.meta` allows 6 publishers
and 4 subscriptions, the same as `delivery-box-esp32s3-microros`. After
changing it, run `idf.py clean-microros`.

**Two USB-C ports with micro-ROS:**
- "USB" (native, `/dev/ttyACM*`, on a Mac `/dev/cu.usbmodem*`): micro-ROS.
  Flashing works through it too, but **no `idf.py monitor` there**.
- "UART"/"COM" (`/dev/ttyUSB*`, on a Mac `/dev/cu.usbserial*` or
  `/dev/cu.wchusbserial*`): the logs. `idf.py -p <that port> monitor`.

Listen on the computer:

```bash
# terminal 1
source /opt/ros/jazzy/setup.bash && source ~/uros_ws/install/local_setup.bash
ros2 run micro_ros_agent micro_ros_agent serial --dev /dev/ttyACM0 -b 115200
# terminal 2
source /opt/ros/jazzy/setup.bash
python3 tools/listen_live.py              # --wav talk.wav to save, --no-play for stats
```

Without micro-ROS the box works as before (logs on the native USB port) and
the microphone is simply not streamed.

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
| 17 | I2S DIN ← ICS43434 DOUT (SEL → GND, 3V → 3.3 V) |
| 19, 20 | native USB: micro-ROS (when built with it) |
| 5 | door-closed switch to GND (closed = door closed = LOW) |
# delivery-box-digital-bridge
