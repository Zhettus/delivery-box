/*
 * states.h — The robot states. One function per state.
 *
 * Calling a function switches the whole box to that state: LED strip,
 * face panels, tail lights, sound and the door. All of them return at once and the
 * box keeps showing the state until another one is called, except
 * charging(), which runs its animation for `duration_ms` and then returns.
 *
 *   state                  strip           face      tail    door    sound
 *   start_up               white pulse     thinking  off     -       startup
 *   moving_with_task       blue pulse      on        off     closes  -
 *   moving_without_task    white pulse     on        off     closes  -
 *   success_waiting        green pulse     on        off     -       arrived
 *   success_open_door      green solid     on        off     opens   greeting (gm.wav)
 *   failure                red pulse       on        brake   stops   failure
 *   failure_not_critical   yellow pulse    on        off     -       warning
 *   charging               cyan comet      follows   off     -       -
 *
 * If someone holds the door while it moves, it goes back (closing -> open,
 * opening -> closed) and the box beeps (main.c, DOOR_HELD_SOUND).
 */
#pragma once

#include <stdint.h>

void start_up(void);
void moving_with_task(void);
void moving_without_task(void);
void success_waiting(void);
void success_open_door(void);
void failure(void);
void failure_not_critical(void);
void charging(uint32_t duration_ms);
