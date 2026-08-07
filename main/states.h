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
