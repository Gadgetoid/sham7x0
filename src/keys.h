#pragma once
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define HOST_KEY_BACKSPACE 8
#define HOST_KEY_TAB       9
#define HOST_KEY_ENTER     13
#define HOST_KEY_ESC       27
#define HOST_KEY_DELETE    127
#define HOST_KEY_UP        0x100
#define HOST_KEY_DOWN      0x101
#define HOST_KEY_LEFT      0x102
#define HOST_KEY_RIGHT     0x103
#define HOST_KEY_HOME      0x104
#define HOST_KEY_END       0x105
#define HOST_KEY_PGUP      0x106
#define HOST_KEY_PGDN      0x107
#define HOST_KEY_F1        0x110
#define HOST_KEY_NEW       0x120
#define HOST_KEY_SMBL      0x121
#define HOST_KEY_SEARCH    0x122
#define HOST_KEY_CASE      0x123
#define HOST_KEY_CUT       0x124
#define HOST_KEY_COPY      0x125
#define HOST_KEY_PASTE     0x126
#define HOST_KEY_EDIT      0x127
#define HOST_KEY_SYNC      0x128
#define HOST_KEY_PICK      0x12a

#define HOST_MOD_SHIFT 1
#define HOST_MOD_CTRL  2
#define HOST_MOD_ALT   4
#define HOST_MOD_CMD   8
#define HOST_MOD_LID   16
#define HOST_MOD_SECOND 32

typedef struct {
    uint32_t code;
    uint8_t  mods;
} host_key_t;

void keys_push(uint32_t code, uint8_t mods);
bool keys_pop(host_key_t *out);
void keys_clear(void);
void keys_set_held(uint32_t code, bool held);
bool keys_is_held(uint32_t code);
void keys_release_all(void);

#ifdef __cplusplus
}
#endif
