#include <string.h>

#include "keys.h"

#define KEY_QUEUE_SIZE 256
#define HELD_CODES     0x200

static host_key_t queue[KEY_QUEUE_SIZE];
static unsigned head = 0, tail = 0;
static bool held[HELD_CODES];

void keys_push(uint32_t code, uint8_t mods) {
    unsigned next = (head + 1) % KEY_QUEUE_SIZE;
    if (next == tail) return;
    queue[head] = (host_key_t){ code, mods };
    head = next;
}

bool keys_pop(host_key_t *out) {
    if (tail == head) return false;
    *out = queue[tail];
    tail = (tail + 1) % KEY_QUEUE_SIZE;
    return true;
}

void keys_clear(void) {
    head = tail = 0;
}

void keys_set_held(uint32_t code, bool is_held) {
    if (code < HELD_CODES) held[code] = is_held;
}

bool keys_is_held(uint32_t code) {
    return code < HELD_CODES && held[code];
}

void keys_release_all(void) {
    memset(held, 0, sizeof held);
}
