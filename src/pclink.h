#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "machine.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct pclink pclink_t;

typedef enum {
    PCLINK_RUNNING,
    PCLINK_DONE,
    PCLINK_FAILED,
} pclink_state_t;

pclink_t      *pclink_create_from_wzd(const uint8_t *data, size_t length, char *error, size_t error_size);
void           pclink_destroy(pclink_t *link);
void           pclink_start(pclink_t *link, machine_t *machine);
pclink_state_t pclink_step(pclink_t *link, machine_t *machine);
const char    *pclink_describe(pclink_t *link);
const char    *pclink_error(pclink_t *link);
float          pclink_progress(pclink_t *link);

#ifdef __cplusplus
}
#endif
