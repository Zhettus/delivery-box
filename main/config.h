/*
 * config.h — Every tunable value of the Delivery Box in one place.
 *
 * Pins live in board.h. What each robot state looks like lives in states.c.
 *
 * Values marked  [MEASURE]  depend on your mechanics/hardware and were
 * taken from the delivery-box-esp32s3 project.
 */
#pragma once

/* ======================================================================== */
/* Door — THE VALUES YOU WILL CHANGE MOST OFTEN                              */
/* ======================================================================== */
/* How far the motor's output shaft turns to open the door, and how fast.
 * These are the start-up values; change them while running with
 * motor_set_open_angle() and motor_set_speed() (see motor.h). */
#define DOOR_OPEN_ANGLE_DEG           90.0f  /* closed -> open shaft rotation (180 = half a turn) */
/* Closing runs until the door-closed switch (board.h PIN_DOOR_SWITCH) closes:
 *   switch closes -> DOOR_LATCH_DELAY_MS later the solenoid switches OFF
 *   (latch locks) -> the motor keeps closing until DOOR_SEAT_MS after the
 *   switch AND DOOR_STOP_AFTER_LATCH_MS after the solenoid went off, then
 *   stops (the solenoid is always off BEFORE the motor stops). Once locked
 *   and the shaft has stopped moving, that spot becomes the new 0.
 * Closing may go up to DOOR_CLOSE_EXTRA_DEG past the last closed position
 * (90 + 20 = 110 degrees) looking for the switch. If the switch never
 * closes, the close ends as "not_closed" and the latch stays unlocked. The
 * latch only ever locks while the switch is closed. */
#define DOOR_CLOSE_EXTRA_DEG          20.0f
#define DOOR_SEAT_MS                  1000   /* keep closing after the switch closes */
#define DOOR_LATCH_DELAY_MS           1000   /* switch closed -> this long -> latch locks */
#define DOOR_STOP_AFTER_LATCH_MS      300    /* solenoid off -> this long -> motor stops */
#define DOOR_SWITCH_CLOSED_LEVEL      0      /* GPIO level when the door is closed */

/* Before opening from closed: turn this far CLOSING way (clockwise) while
 * the solenoid retracts the latch, then open to DOOR_OPEN_ANGLE_DEG. If the
 * door can't move that far, the nudge ends after DOOR_OPEN_NUDGE_MS. */
#define DOOR_OPEN_NUDGE_DEG           15.0f  /* 0 = no nudge */
#define DOOR_OPEN_NUDGE_MS            2000   /* 15 deg at 3 rpm takes ~0.8 s + spin-up */
/* 3 rpm is about the slowest this motor turns smoothly (top speed ~45 rpm). */
#define DOOR_OPEN_RPM                 3.0f   /* output-shaft speed when opening */
#define DOOR_CLOSE_RPM                3.0f   /* output-shaft speed when closing */

/* Which way the shaft turns to OPEN the door. If the door opens the wrong
 * way, flip this between 1 and 0. 0 = counterclockwise (set 2026-10-01). */
#define MOTOR_DIR_LEVEL_OPEN          0

/* 1 = test run (see main.c): wait, then repeat: start_up, open, pause,
 *     close. Needs STATES_MOVE_DOOR 1.
 * 0 = the normal program: cycle through all robot states. */
#define TEST_RUN                      1
#define TEST_START_DELAY_MS           5000   /* 1. after the ESP32 starts: nothing  */
#define TEST_STARTUP_MS               3000   /* 2. start_up, white pulse              */
#define TEST_OPEN_PAUSE_MS            3000   /* 4. door stays open (0 = close at once) */

/* 1 = the robot states open / close / stop the door.
 * 0 = the states only change the lights (for checking LEDs); the motor is
 *     still set up, held stopped and braked, and the latch stays locked. */
#define STATES_MOVE_DOOR              1

/* While the door moves, print a status line this often (0 = never):
 * angle, speed, PWM duty, motor current, and what the FG wire is doing. */
#define MOTOR_LOG_EVERY_MS            200

/* How long main.c shows each state before moving on to the next one. */
#define STATE_HOLD_MS                 5000

/* ======================================================================== */
/* LED strip — one WS2815 strip, 12 V                                       */
/* ======================================================================== */
#define LED_PIXELS                    134    /* LEDs on the strip       [MEASURE] */

/* Direction of the comet and fill animations along the strip.
 * 0 = from LED 0 towards LED 133, 1 = the other way round. */
#define LED_REVERSED                  0

#define LED_MAX_BRIGHTNESS            220    /* 0..255 */
#define LED_FRAME_MS                  10     /* 100 frames per second */
#define LED_COMET_TAIL_PX             10     /* lit length of the comet */
#define LED_WIRE_ORDER_RGB            1      /* WS2815 = RGB on the wire */

/* TypicalLEDStrip colour correction (0xFFB0F0) */
#define LED_CORRECTION_R              0xFF
#define LED_CORRECTION_G              0xB0
#define LED_CORRECTION_B              0xF0

/* Brightness is reduced automatically if a frame would draw more than this. */
#define LED_POWER_BUDGET_MA           2500   /* 12 V current allowed for the strip */
#define LED_MA_PER_PIXEL_WHITE        20.0f  /* one pixel at full white [MEASURE] */
#define LED_MA_PER_PIXEL_IDLE         1.0f   /* one pixel switched off  */

/* ======================================================================== */
/* Face — eyes + mouth on one wire, one XY-MOS module                       */
/* ======================================================================== */
#define FACE_PWM_HZ                   1500
#define FACE_MIN_PULSE_US             2      /* shortest pulse; XY-MOS switches fast. 20 (the old JZ-MOS value) made the 5 % face jump to 3 % instead of fading */
#define FACE_GAMMA                    2.2f
/* Brightest the face ever gets (0..1 = PWM duty): 0.05 = 5 % of full. */
#define FACE_BRIGHTNESS_CAP           0.05f
#define FACE_INVERT                   0      /* 1 if the module lights on LOW */

/* ======================================================================== */
/* Door motor — brushless motor with built-in driver              [MEASURE] */
/* ======================================================================== */
#define MOTOR_PWM_HZ                  20000
#define MOTOR_PWM_INVERTED            0      /* 0: more duty = faster */
#define MOTOR_HAS_BRAKE               1      /* 0 if the motor has no brake wire */
#define MOTOR_BRAKE_LEVEL_ON          1      /* BRAKE level that brakes (HIGH) */
/* FG pulses per OUTPUT-shaft revolution (motor FG pulses/rev x gear ratio). */
#define MOTOR_FG_PER_OUTPUT_REV       480.0f
#define MOTOR_RPM_AT_FULL_DUTY        48.0f  /* output-shaft rpm at 100 % PWM = top speed */
#define MOTOR_MIN_DUTY                0.10f  /* least duty that turns the motor */
#define MOTOR_STOP_BEFORE_REVERSE_MS  500    /* stop time before reversing */

/* Speed control (PI on the output-shaft rpm) */
#define MOTOR_KP                      0.012f /* duty per rpm of error */
#define MOTOR_KI                      0.12f  /* duty per (rpm x s): how fast it adds power under load */
#define MOTOR_INTEGRAL_LIMIT          0.90f  /* extra duty it may add for a heavy door (0..1) */
#define MOTOR_SPEED_WINDOW_MS         200    /* speed measured over this window */
#define MOTOR_CONTROL_MS              20     /* 50 Hz control loop */

/* ======================================================================== */
/* Door safety                                                              */
/* ======================================================================== */
#define DOOR_TOLERANCE_DEG            2.0f   /* "arrived" window */
#define DOOR_SLOWDOWN_DEG             20.0f
#define DOOR_APPROACH_RPM             3.0f
#define DOOR_HOMING_RPM               3.0f

/* Resistance from the MOTOR CURRENT (INA228). A limit of 0 switches it off.
 * Resistance = current above LIMIT, or RISE above the normal running
 * current, for CONFIRM_MS.                                         [MEASURE] */
#define DOOR_CURRENT_LIMIT_A          1.50f  /* estimate: motor draws 0.17 A free at full speed */
#define DOOR_CURRENT_RISE_A           0.50f  /* the door gets heavier as it lifts: leave room */
#define DOOR_CURRENT_CONFIRM_MS       250
#define DOOR_CURRENT_IGNORE_MS        400    /* start-up spike of every move */

/* On resistance: stop -> go back to where the move started -> wait -> retry */
#define DOOR_SPINUP_GRACE_MS          500    /* no detection right after start */
#define DOOR_OBSTRUCTION_MS           800    /* too slow for this long = blocked */
#define DOOR_OBSTRUCTION_RATIO        0.35f  /* too slow = < 35 % of commanded */
#define DOOR_RETRY_WAIT_MS            3000
#define DOOR_MAX_RETRIES              3
#define DOOR_MOVE_TIMEOUT_MS          10000

/* Close the door by itself this long after it opened. 0 = never: the door
 * stays open until a state closes it (moving_with_task / moving_without_task). */
#define DOOR_AUTO_CLOSE_MS            0

/* ======================================================================== */
/* Door latch — solenoid via XY-MOS (board.h PIN_DOOR_LATCH)                */
/* ======================================================================== */
#define LATCH_ENABLED                 1      /* 0 = no solenoid fitted */
#define LATCH_UNLOCK_MS               200    /* latch fully retracted after this */
#define LATCH_SETTLE_MS               100    /* door seats, then latch extends */
#define LATCH_PULL_IN_MS              300    /* full power this long, then HOLD_DUTY */
#define LATCH_HOLD_DUTY               1.0f   /* 1.0 = full power all the time */
#define LATCH_PWM_HZ                  1000

/* ======================================================================== */
/* Motor current sensor — INA228                                            */
/* ======================================================================== */
#define INA228_ADDRESS                0x40   /* A0 = A1 = GND */
#define INA228_SHUNT_OHMS             0.002f /* on-board shunt marked "R002" */
#define INA228_MAX_CURRENT_A          20.0f
#define I2C_SPEED_HZ                  400000
