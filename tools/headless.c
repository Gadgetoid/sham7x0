#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

#include "lcd.h"
#include "machine.h"
#include "pclink.h"
#include "serial.h"
#include "wzd.h"

static double wall_seconds(void) {
    struct timespec now;
    timespec_get(&now, TIME_UTC);
    return (double)now.tv_sec + (double)now.tv_nsec / 1e9;
}

static void sleep_seconds(double seconds) {
#ifdef _WIN32
    Sleep((DWORD)(seconds * 1000));
#else
    usleep((useconds_t)(seconds * 1e6));
#endif
}

typedef struct {
    double at_seconds;
    int column;
    int row;
    bool down;
} key_event_t;

static uint8_t *read_file(const char *path, size_t *size) {
    FILE *file = fopen(path, "rb");
    if (!file) return NULL;
    fseek(file, 0, SEEK_END);
    long length = ftell(file);
    fseek(file, 0, SEEK_SET);
    uint8_t *data = malloc((size_t)length);
    if (data && fread(data, 1, (size_t)length, file) != (size_t)length) {
        free(data);
        data = NULL;
    }
    fclose(file);
    *size = (size_t)length;
    return data;
}

static bool pixel(const uint8_t *screen, int x, int y) {
    return screen[y * MACHINE_SCREEN_ROW_BYTES + x / 8] >> (x % 8) & 1;
}

static void print_screen(const uint8_t *screen) {
    for (int y = 0; y < MACHINE_SCREEN_HEIGHT; y += 2) {
        char line[MACHINE_SCREEN_WIDTH / 2 + 1];
        for (int x = 0; x < MACHINE_SCREEN_WIDTH; x += 2) {
            int count = pixel(screen, x, y) + pixel(screen, x + 1, y) + pixel(screen, x, y + 1) + pixel(screen, x + 1, y + 1);
            line[x / 2] = " .:#"[count > 2 ? 3 : count];
        }
        line[MACHINE_SCREEN_WIDTH / 2] = 0;
        printf("|%s|\n", line);
    }
}

static void save_pbm(const uint8_t *screen, const char *path) {
    FILE *file = fopen(path, "wb");
    if (!file) return;
    fprintf(file, "P4\n%d %d\n", MACHINE_SCREEN_WIDTH, MACHINE_SCREEN_HEIGHT);
    for (int y = 0; y < MACHINE_SCREEN_HEIGHT; y++) {
        for (int byte = 0; byte < MACHINE_SCREEN_ROW_BYTES; byte++) {
            uint8_t value = screen[y * MACHINE_SCREEN_ROW_BYTES + byte];
            uint8_t reversed = 0;
            for (int bit = 0; bit < 8; bit++) {
                if (value >> bit & 1) reversed |= (uint8_t)(0x80 >> bit);
            }
            fputc(reversed, file);
        }
    }
    fclose(file);
}

static void save_lcd(const uint8_t *screen, machine_lcd_t lcd, int cell, const char *path) {
    for (int y = 0; y < LCD_HEIGHT; y++) {
        for (int x = 0; x < LCD_WIDTH; x++) lcd_framebuffer[y * LCD_WIDTH + x] = pixel(screen, x, y) ? 3 : 0;
    }
    lcd_set_power(lcd.on);
    lcd_set_backlight(lcd.backlight);
    if (lcd.on && lcd.contrast != MACHINE_DEFAULT_CONTRAST) lcd_set_contrast(5 + (lcd.contrast - MACHINE_DEFAULT_CONTRAST) / 3);
    lcd_set_response(0);
    lcd_compose_setup(cell);
    lcd_compose(1.0f);
    FILE *file = fopen(path, "wb");
    if (!file) return;
    int width = lcd_compose_width(), height = lcd_compose_height();
    const uint32_t *pixels = lcd_compose_pixels();
    fprintf(file, "P6\n%d %d\n255\n", width, height);
    for (int i = 0; i < width * height; i++) {
        fputc(pixels[i] & 0xff, file);
        fputc(pixels[i] >> 8 & 0xff, file);
        fputc(pixels[i] >> 16 & 0xff, file);
    }
    fclose(file);
}

static int parse_keys(const char *spec, key_event_t *events, int capacity) {
    int count = 0;
    const char *cursor = spec;
    while (*cursor && count + 1 < capacity) {
        double at = 0, hold = 0.1;
        int column = 0, row = 0, consumed = 0;
        if (sscanf(cursor, "%lf:%d.%d/%lf%n", &at, &column, &row, &hold, &consumed) < 3) break;
        events[count++] = (key_event_t){ at, column, row, true };
        events[count++] = (key_event_t){ at + hold, column, row, false };
        cursor += consumed;
        if (*cursor == ',') cursor++;
    }
    return count;
}

int main(int argc, char **argv) {
    const char *rom_path = NULL;
    machine_model_t model = MACHINE_MODEL_OZ750;
    const char *pbm_path = NULL;
    const char *lcd_path = NULL;
    int lcd_cell = 4;
    const char *load_path = NULL;
    const char *save_path = NULL;
    const char *install_paths[MACHINE_ADDIN_SLOTS];
    int install_count = 0;
    double seconds = 3;
    bool trace_ports = false;
    bool backlight_timeout = false;
    double profile_from = -1;
    int dump_page = -1;
    int watch_pc = -1;
    int dump_address = -1, dump_length = 0;
    const char *dump_path = NULL;
    static uint32_t histogram[65536];
    static key_event_t events[1024];
    int event_count = 0;
    bool serial = false;
    const char *serial_link = NULL;
    const char *serial_log_path = NULL;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--serial") == 0) {
            serial = true;
            continue;
        }
        if (strncmp(argv[i], "--serial=", 9) == 0) {
            serial = true;
            serial_link = argv[i] + 9;
            continue;
        }
        if (strncmp(argv[i], "--serial-log=", 13) == 0) {
            serial_log_path = argv[i] + 13;
            continue;
        }
        if (strncmp(argv[i], "--seconds=", 10) == 0) seconds = atof(argv[i] + 10);
        else if (strncmp(argv[i], "--pbm=", 6) == 0) pbm_path = argv[i] + 6;
        else if (strncmp(argv[i], "--lcd=", 6) == 0) lcd_path = argv[i] + 6;
        else if (strncmp(argv[i], "--lcd-cell=", 11) == 0) lcd_cell = atoi(argv[i] + 11);
        else if (strncmp(argv[i], "--load=", 7) == 0) load_path = argv[i] + 7;
        else if (strncmp(argv[i], "--save=", 7) == 0) save_path = argv[i] + 7;
        else if (strncmp(argv[i], "--install=", 10) == 0 && install_count < MACHINE_ADDIN_SLOTS) install_paths[install_count++] = argv[i] + 10;
        else if (strncmp(argv[i], "--keys=", 7) == 0) event_count = parse_keys(argv[i] + 7, events, 1024);
        else if (strcmp(argv[i], "--trace-ports") == 0) trace_ports = true;
        else if (strcmp(argv[i], "--backlight-timeout") == 0) backlight_timeout = true;
        else if (strncmp(argv[i], "--profile=", 10) == 0) profile_from = atof(argv[i] + 10);
        else if (strncmp(argv[i], "--dump=", 7) == 0) {
            char path[512];
            if (sscanf(argv[i] + 7, "%i:%i:%511s", &dump_address, &dump_length, path) == 3) dump_path = strdup(path);
        }
        else if (strncmp(argv[i], "--watch=", 8) == 0) watch_pc = (int)strtol(argv[i] + 8, NULL, 0);
        else if (strncmp(argv[i], "--page=", 7) == 0) dump_page = (int)strtol(argv[i] + 7, NULL, 0);
        else if (strcmp(argv[i], "--model=ZQ-770") == 0) model = MACHINE_MODEL_ZQ770;
        else if (strcmp(argv[i], "--model=OZ-750") == 0) model = MACHINE_MODEL_OZ750;
        else rom_path = argv[i];
    }
    if (!rom_path) {
        fprintf(stderr, "usage: headless ROM [--seconds=N] [--pbm=FILE] [--lcd=FILE] [--lcd-cell=N] [--load=STATE] [--save=STATE] [--trace-ports] [--keys=T:COL.ROW/HOLD,...] [--model=OZ-750|ZQ-770] [--serial[=LINK|DEVICE]] [--serial-log=FILE] [--backlight-timeout]\n");
        return 1;
    }
    size_t size = 0;
    uint8_t *image = read_file(rom_path, &size);
    if (!image) {
        fprintf(stderr, "cannot read %s\n", rom_path);
        return 1;
    }
    machine_t *machine = machine_create(image, size, model);
    int64_t saved_at = 0;
    if (load_path && !machine_load(machine, load_path, &saved_at)) {
        fprintf(stderr, "cannot load state %s\n", load_path);
        return 1;
    }
    pclink_t *links[MACHINE_ADDIN_SLOTS];
    int link_count = 0;
    for (int i = 0; i < install_count; i++) {
        size_t wzd_size = 0;
        uint8_t *wzd_data = read_file(install_paths[i], &wzd_size);
        static uint8_t slot_image[WZD_SLOT_SIZE];
        wzd_program_t program;
        char error[160] = "";
        size_t slot_length = 0;
        int slot = machine_free_addin_slot(machine);
        if (!wzd_data) {
            snprintf(error, sizeof error, "cannot read file");
        } else if (!wzd_parse(wzd_data, wzd_size, &program, error, sizeof error)) {
            pclink_t *link = pclink_create_from_wzd(wzd_data, wzd_size, error, sizeof error);
            if (link) {
                links[link_count++] = link;
                fprintf(stderr, "queued %s from %s\n", pclink_describe(link), install_paths[i]);
                free(wzd_data);
                continue;
            }
        } else if (slot < 0) {
            snprintf(error, sizeof error, "no free slot");
        } else {
            slot_length = wzd_build_slot(&program, slot, slot_image, error, sizeof error);
        }
        if (slot_length && machine_write_addin_slot(machine, slot, slot_image, slot_length)) {
            fprintf(stderr, "installed %s in slot %d (%zu bytes)\n", program.title, slot, slot_length);
        } else {
            fprintf(stderr, "cannot install %s: %s\n", install_paths[i], error);
        }
        free(wzd_data);
    }
    machine_set_trace_ports(machine, trace_ports);
    machine_set_backlight_timeout(machine, backlight_timeout);
    machine_set_watch_pc(machine, watch_pc);
    serial_bridge_t *bridge = NULL;
    if (serial) {
        char description[640];
        bridge = serial_open(serial_link, description, sizeof description);
        fprintf(stderr, "%s\n", description);
        if (!bridge) return 1;
        if (serial_log_path) serial_set_log(bridge, fopen(serial_log_path, "w"));
        serial_attach(bridge, machine);
    }
    double started = wall_seconds();
    int link_index = 0;
    bool link_started = false;
    const int slices_per_second = 100;
    int total_slices = (int)(seconds * slices_per_second);
    for (int slice = 0; slice < total_slices; slice++) {
        double now = (double)slice / slices_per_second;
        bool linking = link_index < link_count;
        if (serial) {
            double ahead = now - (wall_seconds() - started);
            if (ahead > 0) sleep_seconds(ahead);
            if (!linking) serial_poll(bridge, machine);
        }
        if (linking) {
            if (!link_started) {
                pclink_start(links[link_index], machine);
                link_started = true;
            }
            pclink_state_t state = pclink_step(links[link_index], machine);
            if (state != PCLINK_RUNNING) {
                if (state == PCLINK_DONE) fprintf(stderr, "sent %s at %.1fs\n", pclink_describe(links[link_index]), now);
                else fprintf(stderr, "cannot send %s: %s\n", pclink_describe(links[link_index]), pclink_error(links[link_index]));
                pclink_destroy(links[link_index]);
                link_index++;
                link_started = false;
                serial_attach(bridge, machine);
            }
        }
        for (int i = 0; i < event_count; i++) {
            if (events[i].at_seconds >= now && events[i].at_seconds < now + 1.0 / slices_per_second) {
                if (events[i].column == MACHINE_POWER_KEY_COLUMN) machine_set_power_key(machine, events[i].down);
                else machine_set_key(machine, events[i].column, events[i].row, events[i].down);
            }
        }
        if (profile_from >= 0 && now >= profile_from) machine_set_pc_histogram(machine, histogram);
        machine_run(machine, MACHINE_CLOCK_HZ / slices_per_second);
        machine_sound_event_t sound;
        while (machine_pop_sound(machine, &sound)) {
            fprintf(stderr, "sound %.3fs %.0f Hz\n", (double)sound.cycle / MACHINE_CLOCK_HZ, sound.frequency);
        }
        if (slice % slices_per_second == 0) {
            fprintf(stderr, "t=%.1fs pc=%04x%s\n", now, machine_pc(machine), machine_halted(machine) ? " halted" : "");
        }
    }
    if (profile_from >= 0) {
        for (int rank = 0; rank < 40; rank++) {
            int best = 0;
            for (int address = 0; address < 65536; address++) {
                if (histogram[address] > histogram[best]) best = address;
            }
            if (!histogram[best]) break;
            fprintf(stderr, "pc %04x  %u\n", best, histogram[best]);
            histogram[best] = 0;
        }
        int run_start = -1;
        for (int address = 0; address <= 65536; address++) {
            bool hit = address < 65536 && histogram[address];
            if (hit && run_start < 0) run_start = address;
            if (!hit && run_start >= 0) {
                fprintf(stderr, "range %04x-%04x\n", run_start, address - 1);
                run_start = -1;
            }
        }
    }
    if (dump_path) {
        FILE *dump = fopen(dump_path, "wb");
        for (int offset = 0; dump && offset < dump_length; offset++) fputc(machine_peek(machine, (uint16_t)(dump_address + offset)), dump);
        if (dump) fclose(dump);
    }
    machine_lcd_t lcd = machine_lcd(machine);
    fprintf(stderr, "lcd on %d contrast %d backlight %d\n", lcd.on, lcd.contrast, lcd.backlight);
    const uint8_t *screen = machine_screen(machine);
    static uint8_t page_copy[MACHINE_SCREEN_ROW_BYTES * MACHINE_SCREEN_HEIGHT];
    if (dump_page >= 0 && machine_read_page(machine, (uint16_t)dump_page, page_copy, sizeof page_copy)) screen = page_copy;
    print_screen(screen);
    if (pbm_path) save_pbm(screen, pbm_path);
    if (lcd_path) save_lcd(screen, lcd, lcd_cell, lcd_path);
    if (save_path && !machine_save(machine, save_path, 0)) {
        fprintf(stderr, "cannot save state %s\n", save_path);
        return 1;
    }
    machine_destroy(machine);
    free(image);
    serial_close(bridge);
    return 0;
}
