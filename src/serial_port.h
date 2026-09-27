#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct serial_port serial_port_t;

serial_port_t *serial_port_open_device(const char *path, char *error, size_t error_size);
serial_port_t *serial_port_open_virtual(const char *link, char *name, size_t name_size, char *error, size_t error_size);
long           serial_port_read(serial_port_t *port, uint8_t *data, size_t size);
long           serial_port_write(serial_port_t *port, const uint8_t *data, size_t size);
void           serial_port_set_baud(serial_port_t *port, unsigned baud);
void           serial_port_close(serial_port_t *port);
bool           serial_port_is_device(const char *path);
int            serial_port_list(char paths[][64], int max_paths);
