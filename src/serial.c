#include <stdlib.h>
#include <string.h>

#include "serial.h"
#include "serial_port.h"

#define SERIAL_INPUT_SIZE  4096
#define SERIAL_OUTPUT_SIZE 65536

struct serial_bridge {
    serial_port_t *port;
    FILE    *log;
    int      log_direction;
    int      log_column;
    unsigned baud;
    uint8_t  input[SERIAL_INPUT_SIZE];
    size_t   input_length;
    uint8_t  output[SERIAL_OUTPUT_SIZE];
    size_t   output_start;
    size_t   output_length;
};

static void log_byte(serial_bridge_t *bridge, int direction, uint8_t value) {
    if (!bridge->log) return;
    if (direction != bridge->log_direction || bridge->log_column == 32) {
        fprintf(bridge->log, "%s%s", bridge->log_column ? "\n" : "", direction ? "wizard>" : "host>  ");
        bridge->log_direction = direction;
        bridge->log_column = 0;
    }
    fprintf(bridge->log, " %02x", value);
    bridge->log_column++;
}

static void flush_output(serial_bridge_t *bridge) {
    while (bridge->output_length) {
        size_t run = SERIAL_OUTPUT_SIZE - bridge->output_start;
        if (run > bridge->output_length) run = bridge->output_length;
        long written = serial_port_write(bridge->port, bridge->output + bridge->output_start, run);
        if (written <= 0) return;
        bridge->output_start = (bridge->output_start + (size_t)written) % SERIAL_OUTPUT_SIZE;
        bridge->output_length -= (size_t)written;
    }
}

static void serial_output(void *context, uint8_t value) {
    serial_bridge_t *bridge = context;
    log_byte(bridge, 1, value);
    if (bridge->output_length == SERIAL_OUTPUT_SIZE) return;
    bridge->output[(bridge->output_start + bridge->output_length) % SERIAL_OUTPUT_SIZE] = value;
    bridge->output_length++;
    flush_output(bridge);
}

static void set_baud(serial_bridge_t *bridge, unsigned baud) {
    bridge->baud = baud;
    serial_port_set_baud(bridge->port, baud);
}

bool serial_is_device(const char *path) {
    return serial_port_is_device(path);
}

serial_bridge_t *serial_open(const char *target, char *description, size_t description_size) {
    serial_bridge_t *bridge = calloc(1, sizeof *bridge);
    if (!bridge) return NULL;
    bridge->log_direction = -1;
    if (serial_is_device(target)) {
        bridge->port = serial_port_open_device(target, description, description_size);
        if (!bridge->port) {
            free(bridge);
            return NULL;
        }
        snprintf(description, description_size, "serial on %s", target);
        return bridge;
    }
    char name[128];
    bridge->port = serial_port_open_virtual(target, name, sizeof name, description, description_size);
    if (!bridge->port) {
        free(bridge);
        return NULL;
    }
    snprintf(description, description_size, "serial %s%s%s", name, target && *target ? " linked at " : "", target && *target ? target : "");
    return bridge;
}

void serial_close(serial_bridge_t *bridge) {
    if (!bridge) return;
    flush_output(bridge);
    serial_port_close(bridge->port);
    if (bridge->log) fflush(bridge->log);
    free(bridge);
}

void serial_attach(serial_bridge_t *bridge, machine_t *machine) {
    machine_set_serial_output(machine, bridge ? serial_output : NULL, bridge);
    if (bridge) set_baud(bridge, machine_serial_baud(machine));
}

void serial_set_log(serial_bridge_t *bridge, FILE *log) {
    bridge->log = log;
}

void serial_poll(serial_bridge_t *bridge, machine_t *machine) {
    unsigned baud = machine_serial_baud(machine);
    if (baud != bridge->baud) set_baud(bridge, baud);
    flush_output(bridge);
    if (bridge->input_length < SERIAL_INPUT_SIZE) {
        long count = serial_port_read(bridge->port, bridge->input + bridge->input_length, SERIAL_INPUT_SIZE - bridge->input_length);
        if (count > 0) {
            for (long i = 0; i < count; i++) log_byte(bridge, 0, bridge->input[bridge->input_length + (size_t)i]);
            bridge->input_length += (size_t)count;
        }
    }
    size_t accepted = machine_serial_input(machine, bridge->input, bridge->input_length);
    memmove(bridge->input, bridge->input + accepted, bridge->input_length - accepted);
    bridge->input_length -= accepted;
    if (bridge->log) fflush(bridge->log);
}

int serial_list_devices(char paths[][64], int max_paths) {
    return serial_port_list(paths, max_paths);
}
