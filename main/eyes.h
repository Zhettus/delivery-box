#pragma once

#include <stdbool.h>
#include "esp_err.h"


typedef enum {
    EYE_OFF = 0,
    EYE_THINKING,
    EYE_CONSTANT,
} eye_emotion_t;

esp_err_t eyes_init(void);

void eyes_set_emotion(eye_emotion_t e);
                    
eye_emotion_t eyes_get_emotion(void);

eye_emotion_t eyes_get_target(void);

bool eyes_is_transitioning(void);

void eyes_set_brightness_cap(float cap);

void eyes_set_zero(void);