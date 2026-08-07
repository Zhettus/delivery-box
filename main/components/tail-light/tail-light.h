#ifndef TAIL_LIGHT_H
#define TAIL_LIGHT_H

#include <stdbool.h>

void tail_lights_init(void);

void tail_lights_set_running(bool enable);
void tail_lights_set_brake(bool enable);
void tail_lights_set_left_turn(bool enable);
void tail_lights_set_right_turn(bool enable);

void tail_lights_off_all(void);

#endif 