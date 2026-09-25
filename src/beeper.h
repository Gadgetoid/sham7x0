#pragma once
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

bool beeper_init(void);
void beeper_tone(float frequency, float second_frequency, uint32_t duration_ms);
void beeper_stop(void);
bool beeper_busy(void);
void beeper_set_sound(bool on);
bool beeper_sound(void);
void beeper_deinit(void);

#ifdef __cplusplus
}
#endif
