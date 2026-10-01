/*
 * states.h — The robot states. One function per state.
 *
 * Calling a function switches the whole box to that state: LED strip,
 * face panels, tail lights and the door. All of them return at once and the
 * box keeps showing the state until another one is called, except
 * charging(), which runs its animation for `duration_ms` and then returns.
 *
 *   state                  strip           face      tail    door
 *   start_up               white pulse     thinking  off     -
 *   moving_with_task       blue pulse      on        off     closes
 *   moving_without_task    white pulse     on        off     closes
 *   success_waiting        green pulse     on        off     -
 *   success_open_door      green solid     on        off     opens
 *   failure                red pulse       on        brake   stops
 *   failure_not_critical   yellow pulse    on        off     -
 *   charging               cyan comet      follows   off     -
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
