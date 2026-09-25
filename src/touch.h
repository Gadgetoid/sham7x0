#pragma once
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum { TOUCH_DOWN = 1, TOUCH_MOVE, TOUCH_UP };

typedef struct {
    uint8_t kind;
    float x;
    float y;
} touch_event_t;

bool touch_start(void);
void touch_set_panel(int width, int height);
bool touch_pop(touch_event_t *event);
bool touch_attached(void);
void touch_stop(void);

bool window_cover_display(void *nswindow, bool cover);

#ifdef __cplusplus
}
#endif
