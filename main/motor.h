/*
 * motor.h — The door motor, made easy.
 *
 * The motor is a brushless gear motor with a built-in driver. This file is
 * the simple front door to the `door` component (components/door), which
 * does the careful work underneath: speed control from the motor's FG
 * pulses, the latch solenoid, stopping before reversing, and backing off
 * when something blocks the door (motor current from the INA228).
 *
 * Angles are degrees of the motor's OUTPUT SHAFT: 0 = door closed (where the
 * shaft was at power-up), positive = opening direction.
 * Speeds are output-shaft rpm, up to MOTOR_RPM_AT_FULL_DUTY (config.h).
 *
 *   motor_set_open_angle(120);      // "open" now means 120 degrees
 *   motor_set_speed(10, 5);         // open at 10 rpm, close at 5 rpm
 *   motor_open_door();              // start opening, returns at once
 *   motor_wait_done(10000);         // ...wait until it has arrived
 *
 *   motor_rotate(45, 6);            // turn 45 degrees further open at 6 rpm
 *   motor_rotate(-45, 6);           // and back
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

/** Motor stopped + braked, door locked, control task started.
 *  The door must be CLOSED at power-up: that position becomes 0 degrees. */
esp_err_t motor_init(void);

/* ---- Settings (start-up values are in config.h) --------------------------- */

/** Shaft rotation that means "door open", in degrees. Used by the next
 *  motor_open_door(). */
void motor_set_open_angle(float degrees);

/** Shaft speed for opening and for closing, in rpm. Used by the next move. */
void motor_set_speed(float open_rpm, float close_rpm);

/* ---- Door ------------------------------------------------------------------ */
/* These return at once; the door moves in the background. Calling one again
 * while the door is already there (or on its way) changes nothing. */

void motor_open_door(void);     /* go to the open angle at the opening speed */
void motor_close_door(void);    /* go to 0 degrees at the closing speed      */
void motor_stop(void);          /* stop where it is, brake on                */

/** Wait until the last started move has ended.
 *  @return true if the door reached its target, false if it was blocked,
 *          stopped, or still moving after `timeout_ms`. */
bool motor_wait_done(uint32_t timeout_ms);

/* ---- Free rotation --------------------------------------------------------- */

/**
 * Turn the shaft by `degrees` from where it is now, at `rpm`, and wait until
 * it is done. Positive = opening direction, negative = closing direction.
 * @return true if the shaft reached the target.
 */
bool motor_rotate(float degrees, float rpm);

/** Current shaft angle in degrees (0 = closed). */
float motor_get_angle(void);

/** `callback` runs when the door gives up (blocked on every retry, or timed
 *  out). Keep it short; do not call motor_wait_done()/motor_rotate() in it. */
void motor_on_fault(void (*callback)(void));
