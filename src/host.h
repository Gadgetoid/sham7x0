#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const char *rom_path;
    const char *data_path;
    bool        persist;
    int         model;
    const char *state_name;
} host_config_t;

uint32_t host_ticks_ms(void);

#ifdef __cplusplus
}
#endif
