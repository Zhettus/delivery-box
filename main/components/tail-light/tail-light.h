#pragma once

#include <stdbool.h>

typedef struct {
    bool run;    /* presence / running light (baseline, on while active) */
    bool brake;  /* brake / stop */
    bool left;   /* left turn  (the lamp does its own sweep while held on) */
    bool right;  /* right turn */
} light_t;

void light_init(void);

void light_set(light_t s);

light_t light_get(void);

void light_run  (bool on);
void light_brake(bool on);
void light_left (bool on);
void light_right(bool on);

/* Common combinations. */
void light_all_off(void);            /* everything open */
void light_moving (void);            /* run only */
void light_stop   (void);            /* run + brake */
void light_signal_left (bool on);    /* left indicator (clears right) */
void light_signal_right(bool on);    /* right indicator (clears left) */
void light_hazards(bool on);         /* both indicators */