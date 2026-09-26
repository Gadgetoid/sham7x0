#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

#include "machine.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct serial_bridge serial_bridge_t;

serial_bridge_t *serial_open(const char *target, char *description, size_t description_size);
void             serial_close(serial_bridge_t *bridge);
void             serial_attach(serial_bridge_t *bridge, machine_t *machine);
void             serial_poll(serial_bridge_t *bridge, machine_t *machine);
void             serial_set_log(serial_bridge_t *bridge, FILE *log);
bool             serial_is_device(const char *path);
int              serial_list_devices(char paths[][64], int max_paths);

#ifdef __cplusplus
}
#endif
