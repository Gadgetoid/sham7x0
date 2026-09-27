#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

#include "serial_port.h"

struct serial_port {
    HANDLE handle;
};

static const char *port_name(const char *path) {
    if (strncmp(path, "\\\\.\\", 4) == 0) return path + 4;
    return path;
}

serial_port_t *serial_port_open_device(const char *path, char *error, size_t error_size) {
    char device[80];
    snprintf(device, sizeof device, "\\\\.\\%s", port_name(path));
    HANDLE handle = CreateFileA(device, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
    if (handle == INVALID_HANDLE_VALUE) {
        snprintf(error, error_size, "cannot open %s: error %lu", path, (unsigned long)GetLastError());
        return NULL;
    }
    DCB settings = { 0 };
    settings.DCBlength = sizeof settings;
    GetCommState(handle, &settings);
    settings.BaudRate = CBR_9600;
    settings.ByteSize = 8;
    settings.Parity = NOPARITY;
    settings.StopBits = ONESTOPBIT;
    settings.fBinary = TRUE;
    settings.fOutxCtsFlow = FALSE;
    settings.fOutxDsrFlow = FALSE;
    settings.fDtrControl = DTR_CONTROL_ENABLE;
    settings.fRtsControl = RTS_CONTROL_ENABLE;
    settings.fOutX = FALSE;
    settings.fInX = FALSE;
    SetCommState(handle, &settings);
    COMMTIMEOUTS timeouts = { 0 };
    timeouts.ReadIntervalTimeout = MAXDWORD;
    timeouts.WriteTotalTimeoutConstant = 50;
    SetCommTimeouts(handle, &timeouts);
    serial_port_t *port = calloc(1, sizeof *port);
    if (!port) {
        CloseHandle(handle);
        return NULL;
    }
    port->handle = handle;
    return port;
}

serial_port_t *serial_port_open_virtual(const char *link, char *name, size_t name_size, char *error, size_t error_size) {
    (void)link;
    (void)name;
    (void)name_size;
    snprintf(error, error_size, "virtual serial ports need a com0com pair on Windows; pass one of its ports, such as --serial=COM5");
    return NULL;
}

long serial_port_read(serial_port_t *port, uint8_t *data, size_t size) {
    DWORD count = 0;
    if (!ReadFile(port->handle, data, (DWORD)size, &count, NULL)) return -1;
    return (long)count;
}

long serial_port_write(serial_port_t *port, const uint8_t *data, size_t size) {
    DWORD count = 0;
    if (!WriteFile(port->handle, data, (DWORD)size, &count, NULL)) return -1;
    return (long)count;
}

void serial_port_set_baud(serial_port_t *port, unsigned baud) {
    DCB settings = { 0 };
    settings.DCBlength = sizeof settings;
    if (!baud || !GetCommState(port->handle, &settings)) return;
    settings.BaudRate = baud;
    SetCommState(port->handle, &settings);
}

void serial_port_close(serial_port_t *port) {
    CloseHandle(port->handle);
    free(port);
}

bool serial_port_is_device(const char *path) {
    if (!path) return false;
    const char *name = port_name(path);
    if (_strnicmp(name, "COM", 3) != 0 || !name[3]) return false;
    for (const char *digit = name + 3; *digit; digit++) {
        if (!isdigit((unsigned char)*digit)) return false;
    }
    return true;
}

static int compare_ports(const void *a, const void *b) {
    return atoi((const char *)a + 3) - atoi((const char *)b + 3);
}

int serial_port_list(char paths[][64], int max_paths) {
    static char devices[65536];
    DWORD length = QueryDosDeviceA(NULL, devices, sizeof devices);
    int count = 0;
    for (const char *device = devices; length && *device && count < max_paths; device += strlen(device) + 1) {
        if (serial_port_is_device(device) && strlen(device) < 64) snprintf(paths[count++], 64, "%s", device);
    }
    qsort(paths, (size_t)count, 64, compare_ports);
    return count;
}
