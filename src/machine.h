#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MACHINE_SCREEN_WIDTH      240
#define MACHINE_SCREEN_HEIGHT     80
#define MACHINE_SCREEN_ROW_BYTES  30
#define MACHINE_KEY_COLUMNS       11
#define MACHINE_KEY_ROWS          8
#define MACHINE_CLOCK_HZ          9830400
#define MACHINE_POWER_KEY_COLUMN  99
#define MACHINE_ADDIN_SLOTS       10

typedef struct machine machine_t;

typedef void (*machine_log_fn)(const char *message);
typedef void (*machine_serial_out_fn)(void *context, uint8_t value);

typedef struct {
    uint64_t cycle;
    float    frequency;
} machine_sound_event_t;

typedef struct {
    bool on;
    int  contrast;
    bool backlight;
} machine_lcd_t;

machine_t     *machine_create(const uint8_t *flash_image, size_t flash_size);
void           machine_destroy(machine_t *machine);
void           machine_reset(machine_t *machine);
void           machine_run(machine_t *machine, uint32_t cycles);
void           machine_set_key(machine_t *machine, int column, int row, bool down);
void           machine_set_power_key(machine_t *machine, bool down);
void           machine_release_keys(machine_t *machine);
const uint8_t *machine_screen(machine_t *machine);
machine_lcd_t  machine_lcd(machine_t *machine);
bool           machine_pop_sound(machine_t *machine, machine_sound_event_t *event);
uint64_t       machine_cycles(machine_t *machine);
bool           machine_save(machine_t *machine, const char *path, int64_t host_time);
bool           machine_load(machine_t *machine, const char *path, int64_t *host_time);
void           machine_advance_clock(machine_t *machine, int64_t seconds);
uint16_t       machine_pc(machine_t *machine);
bool           machine_halted(machine_t *machine);
void           machine_set_log(machine_t *machine, machine_log_fn log);
void           machine_set_trace_ports(machine_t *machine, bool trace);
void           machine_set_pc_histogram(machine_t *machine, uint32_t *counts);
void           machine_set_watch_pc(machine_t *machine, int pc);
int            machine_free_addin_slot(machine_t *machine);
bool           machine_write_addin_slot(machine_t *machine, int slot, const uint8_t *data, size_t length);
uint8_t        machine_peek(machine_t *machine, uint16_t address);
bool           machine_read_page(machine_t *machine, uint16_t page, uint8_t *out, size_t length);
void           machine_set_serial_output(machine_t *machine, machine_serial_out_fn output, void *context);
unsigned       machine_serial_baud(machine_t *machine);
size_t         machine_serial_input(machine_t *machine, const uint8_t *data, size_t length);

#ifdef __cplusplus
}
#endif
