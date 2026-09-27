#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <termios.h>
#include <unistd.h>
#ifdef __APPLE__
#include <util.h>
#else
#include <pty.h>
#endif

#include "serial_port.h"

struct serial_port {
    int  fd;
    bool device;
    char link[512];
};

#ifdef __APPLE__
static const char *const DEVICE_PREFIXES[] = { "cu." };

static speed_t baud_speed(unsigned baud) {
    return (speed_t)baud;
}
#else
static const char *const DEVICE_PREFIXES[] = { "ttyUSB", "ttyACM" };

static speed_t baud_speed(unsigned baud) {
    static const struct { unsigned baud; speed_t speed; } speeds[] = {
        { 1200, B1200 }, { 2400, B2400 }, { 4800, B4800 }, { 9600, B9600 }, { 19200, B19200 },
        { 38400, B38400 }, { 57600, B57600 }, { 115200, B115200 }, { 230400, B230400 },
    };
    for (size_t i = 0; i < sizeof speeds / sizeof speeds[0]; i++) {
        if (speeds[i].baud == baud) return speeds[i].speed;
    }
    return 0;
}
#endif

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

serial_port_t *serial_port_open_device(const char *path, char *error, size_t error_size) {
    serial_port_t *port = calloc(1, sizeof *port);
    if (!port) return NULL;
    port->device = true;
    port->fd = open(path, O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (port->fd < 0) {
        snprintf(error, error_size, "cannot open %s: %s", path, strerror(errno));
        free(port);
        return NULL;
    }
    make_raw(port->fd, true);
    return port;
}

serial_port_t *serial_port_open_virtual(const char *link, char *name, size_t name_size, char *error, size_t error_size) {
    serial_port_t *port = calloc(1, sizeof *port);
    if (!port) return NULL;
    int slave = -1;
    char pty_name[128];
    if (openpty(&port->fd, &slave, pty_name, NULL, NULL) < 0) {
        snprintf(error, error_size, "cannot open a pty: %s", strerror(errno));
        free(port);
        return NULL;
    }
    make_raw(slave, false);
    fcntl(port->fd, F_SETFL, fcntl(port->fd, F_GETFL) | O_NONBLOCK);
    if (link && *link) {
        unlink(link);
        if (symlink(pty_name, link) < 0) {
            snprintf(error, error_size, "cannot link %s to %s: %s", link, pty_name, strerror(errno));
            close(port->fd);
            close(slave);
            free(port);
            return NULL;
        }
        snprintf(port->link, sizeof port->link, "%s", link);
    }
    snprintf(name, name_size, "%s", pty_name);
    return port;
}

long serial_port_read(serial_port_t *port, uint8_t *data, size_t size) {
    return (long)read(port->fd, data, size);
}

long serial_port_write(serial_port_t *port, const uint8_t *data, size_t size) {
    return (long)write(port->fd, data, size);
}

void serial_port_set_baud(serial_port_t *port, unsigned baud) {
    if (!port->device) return;
    speed_t speed = baud_speed(baud);
    struct termios settings;
    if (!speed || tcgetattr(port->fd, &settings) < 0) return;
    cfsetspeed(&settings, speed);
    tcsetattr(port->fd, TCSANOW, &settings);
}

void serial_port_close(serial_port_t *port) {
    close(port->fd);
    if (port->link[0]) unlink(port->link);
    free(port);
}

bool serial_port_is_device(const char *path) {
    struct stat info;
    return path && stat(path, &info) == 0 && S_ISCHR(info.st_mode);
}

static int compare_paths(const void *a, const void *b) {
    return strcmp((const char *)a, (const char *)b);
}

int serial_port_list(char paths[][64], int max_paths) {
    DIR *directory = opendir("/dev");
    if (!directory) return 0;
    int count = 0;
    struct dirent *entry;
    while ((entry = readdir(directory)) != NULL && count < max_paths) {
        for (size_t i = 0; i < sizeof DEVICE_PREFIXES / sizeof DEVICE_PREFIXES[0]; i++) {
            if (strncmp(entry->d_name, DEVICE_PREFIXES[i], strlen(DEVICE_PREFIXES[i])) == 0 && strlen(entry->d_name) < 64 - 5) {
                snprintf(paths[count++], 64, "/dev/%.*s", 64 - 6, entry->d_name);
                break;
            }
        }
    }
    closedir(directory);
    qsort(paths, (size_t)count, 64, compare_paths);
    return count;
}
