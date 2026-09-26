#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <termios.h>
#include <unistd.h>
#include <util.h>

#include "serial.h"

#define SERIAL_INPUT_SIZE  4096
#define SERIAL_OUTPUT_SIZE 65536

struct serial_bridge {
    int      fd;
    bool     device;
    char     link[512];
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
        ssize_t written = write(bridge->fd, bridge->output + bridge->output_start, run);
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

static void make_raw(int fd, bool device) {
    struct termios settings;
    if (tcgetattr(fd, &settings) < 0) return;
    cfmakeraw(&settings);
    if (device) {
        settings.c_cflag |= CLOCAL | CREAD;
        settings.c_cflag &= ~(tcflag_t)HUPCL;
    }
    tcsetattr(fd, TCSANOW, &settings);
}

static void set_baud(serial_bridge_t *bridge, unsigned baud) {
    bridge->baud = baud;
    if (!bridge->device) return;
    struct termios settings;
    if (tcgetattr(bridge->fd, &settings) < 0) return;
    cfsetspeed(&settings, (speed_t)baud);
    tcsetattr(bridge->fd, TCSANOW, &settings);
}

bool serial_is_device(const char *path) {
    struct stat info;
    return path && stat(path, &info) == 0 && S_ISCHR(info.st_mode);
}

serial_bridge_t *serial_open(const char *target, char *description, size_t description_size) {
    serial_bridge_t *bridge = calloc(1, sizeof *bridge);
    if (!bridge) return NULL;
    bridge->log_direction = -1;
    if (serial_is_device(target)) {
        bridge->device = true;
        bridge->fd = open(target, O_RDWR | O_NOCTTY | O_NONBLOCK);
        if (bridge->fd < 0) {
            snprintf(description, description_size, "cannot open %s: %s", target, strerror(errno));
            free(bridge);
            return NULL;
        }
        make_raw(bridge->fd, true);
        snprintf(description, description_size, "serial on %s", target);
        return bridge;
    }
    int slave = -1;
    char name[128];
    if (openpty(&bridge->fd, &slave, name, NULL, NULL) < 0) {
        snprintf(description, description_size, "cannot open a pty: %s", strerror(errno));
        free(bridge);
        return NULL;
    }
    make_raw(slave, false);
    fcntl(bridge->fd, F_SETFL, fcntl(bridge->fd, F_GETFL) | O_NONBLOCK);
    if (target && *target) {
        unlink(target);
        if (symlink(name, target) < 0) {
            snprintf(description, description_size, "cannot link %s to %s: %s", target, name, strerror(errno));
            close(bridge->fd);
            close(slave);
            free(bridge);
            return NULL;
        }
        snprintf(bridge->link, sizeof bridge->link, "%s", target);
    }
    snprintf(description, description_size, "serial %s%s%s", name, target && *target ? " linked at " : "", target && *target ? target : "");
    return bridge;
}

void serial_close(serial_bridge_t *bridge) {
    if (!bridge) return;
    flush_output(bridge);
    close(bridge->fd);
    if (bridge->link[0]) unlink(bridge->link);
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
        ssize_t count = read(bridge->fd, bridge->input + bridge->input_length, SERIAL_INPUT_SIZE - bridge->input_length);
        if (count > 0) {
            for (ssize_t i = 0; i < count; i++) log_byte(bridge, 0, bridge->input[bridge->input_length + (size_t)i]);
            bridge->input_length += (size_t)count;
        }
    }
    size_t accepted = machine_serial_input(machine, bridge->input, bridge->input_length);
    memmove(bridge->input, bridge->input + accepted, bridge->input_length - accepted);
    bridge->input_length -= accepted;
    if (bridge->log) fflush(bridge->log);
}

static int compare_paths(const void *a, const void *b) {
    return strcmp((const char *)a, (const char *)b);
}

int serial_list_devices(char paths[][64], int max_paths) {
    DIR *directory = opendir("/dev");
    if (!directory) return 0;
    int count = 0;
    struct dirent *entry;
    while ((entry = readdir(directory)) != NULL && count < max_paths) {
        if (strncmp(entry->d_name, "cu.", 3) == 0) snprintf(paths[count++], 64, "/dev/%s", entry->d_name);
    }
    closedir(directory);
    qsort(paths, (size_t)count, 64, compare_paths);
    return count;
}
