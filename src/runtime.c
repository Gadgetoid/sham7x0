#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "beeper.h"
#include "console.h"
#include "keys.h"
#include "lcd.h"
#include "machine.h"
#include "runtime.h"
#include "wzd.h"

#define KEYCODE_TABLE      0x23a3
#define FIRMWARE_SHIFT     0x0800
#define FIRMWARE_SECOND    0x8037
#define FIRMWARE_ENTER     0x8038
#define FIRMWARE_ESC       0x8058
#define FIRMWARE_MENU      0x8032
#define FIRMWARE_NEW       0x8033
#define FIRMWARE_SMBL      0x8035
#define FIRMWARE_DELETE    0x8057
#define FIRMWARE_UP        0x8040
#define FIRMWARE_DOWN      0x8041
#define FIRMWARE_LEFT      0x8042
#define FIRMWARE_RIGHT     0x8043
#define FIRMWARE_LID_UP    0x8044
#define FIRMWARE_LID_DOWN  0x8045
#define FIRMWARE_LID_MENU  0x8068
#define FIRMWARE_LID_ESC   0x8067
#define FIRMWARE_LID_ENTER 0x8066
#define FIRMWARE_LIGHT     0x803b
#define FIRMWARE_RETURN    0x000d
#define FIRMWARE_SHIFT_RIGHT 0x8036
#define MAX_HELD_KEYS      16
#define POWER_KEY_CODE     0xffff
#define MAX_STEP_KEYS      2
#define MAX_STEPS          4
#define QUEUE_SIZE         64
#define PRESS_MS           80
#define GAP_MS             60
#define INIT_HOLD_MS       1500
#define MAX_CATCH_UP_MS    100
#define AUTOSAVE_MS        60000
#define MAX_CLOCK_CATCH_UP (400LL * 24 * 60 * 60)
#define DEFAULT_CONTRAST   32
#define AUDIO_LATENCY_MS   40

typedef struct {
    uint16_t codes[MAX_STEP_KEYS];
    int count;
    int hold_ms;
} key_step_t;

typedef struct {
    key_step_t steps[MAX_STEPS];
    int count;
} key_action_t;

static const uint16_t APP_KEYS[] = { 0x7025, 0x70e9, 0x70ea, 0x70eb, 0x7015 };

typedef struct {
    const char *id;
    uint16_t code;
} named_key_t;

static const named_key_t NAMED_KEYS[] = {
    { "esc", FIRMWARE_ESC }, { "del", FIRMWARE_DELETE }, { "enter_wide", FIRMWARE_RETURN },
    { "shift_left", FIRMWARE_SHIFT }, { "shift_right", FIRMWARE_SHIFT_RIGHT }, { "comma", ',' }, { "period", '.' },
    { "fn_2nd", FIRMWARE_SECOND }, { "menu", FIRMWARE_MENU }, { "new", FIRMWARE_NEW }, { "smbl", FIRMWARE_SMBL },
    { "space", ' ' }, { "minus", '-' }, { "enter", FIRMWARE_ENTER }, { "up", FIRMWARE_UP }, { "left", FIRMWARE_LEFT },
    { "down", FIRMWARE_DOWN }, { "right", FIRMWARE_RIGHT },
};

typedef struct {
    char character;
    uint16_t code;
} shifted_key_t;

static const shifted_key_t SHIFTED_KEYS[] = {
    { '!', '1' }, { '@', '2' }, { '#', '3' }, { '$', '4' }, { '%', '5' }, { '^', '6' }, { '&', '7' },
    { '*', '8' }, { '(', '9' }, { ')', '0' }, { '\'', ',' }, { ':', '.' }, { '_', '-' },
};

typedef struct {
    uint16_t code;
    bool     held_by_host;
    bool     sticky;
    uint32_t pressed_at;
} held_key_t;

static held_key_t held_keys[MAX_HELD_KEYS];
static int held_key_count = 0;

static machine_t *machine = NULL;
static uint8_t *rom_image = NULL;
static size_t rom_size = 0;
static uint16_t matrix_codes[MACHINE_KEY_COLUMNS][MACHINE_KEY_ROWS];
static key_action_t queue[QUEUE_SIZE];
static unsigned queue_head = 0, queue_tail = 0;
static key_step_t active_step;
static bool step_pressed = false;
static double step_time_ms = 0;
static int action_step = 0;
static uint32_t last_step_ms = 0;
static int boots = 0;
static uint64_t emulated_ms = 0;
static bool persist = false;
static char state_path[1024];
static uint32_t last_save_ms = 0;
static uint64_t sound_cursor = 0;
static float sound_frequency = 0;
static machine_lcd_t shown_lcd = { true, DEFAULT_CONTRAST, false };

static void log_to_console(const char *message) {
    console_notice(message);
}

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

static void load_keycode_table(void) {
    for (int column = 0; column < MACHINE_KEY_COLUMNS; column++) {
        for (int row = 0; row < MACHINE_KEY_ROWS; row++) {
            size_t offset = KEYCODE_TABLE + (size_t)(column * MACHINE_KEY_ROWS + row) * 2;
            matrix_codes[column][row] = offset + 1 < rom_size ? (uint16_t)(rom_image[offset] | rom_image[offset + 1] << 8) : 0;
        }
    }
}

static bool find_code(uint16_t code, int *column, int *row) {
    for (int c = 0; c < MACHINE_KEY_COLUMNS; c++) {
        for (int r = 0; r < MACHINE_KEY_ROWS; r++) {
            if (code && matrix_codes[c][r] == code) {
                *column = c;
                *row = r;
                return true;
            }
        }
    }
    return false;
}

static void add_step(key_action_t *action, uint16_t first, uint16_t second) {
    if (action->count >= MAX_STEPS) return;
    key_step_t *step = &action->steps[action->count++];
    step->count = 0;
    step->hold_ms = PRESS_MS;
    step->codes[step->count++] = first;
    if (second) step->codes[step->count++] = second;
}

static uint16_t firmware_code(uint32_t code, uint8_t mods) {
    if (code >= 'a' && code <= 'z') return (uint16_t)code;
    if (code >= '0' && code <= '9') return (uint16_t)code;
    if (code == ' ' || code == '-' || code == '.' || code == ',') return (uint16_t)code;
    switch (code) {
        case HOST_KEY_ENTER:     return (mods & HOST_MOD_LID) ? FIRMWARE_LID_ENTER : FIRMWARE_ENTER;
        case HOST_KEY_ESC:       return (mods & HOST_MOD_LID) ? FIRMWARE_LID_ESC : FIRMWARE_ESC;
        case HOST_KEY_TAB:       return (mods & HOST_MOD_LID) ? FIRMWARE_LID_MENU : FIRMWARE_MENU;
        case HOST_KEY_NEW:       return FIRMWARE_NEW;
        case HOST_KEY_SMBL:      return FIRMWARE_SMBL;
        case HOST_KEY_BACKSPACE:
        case HOST_KEY_DELETE:    return FIRMWARE_DELETE;
        case HOST_KEY_UP:        return (mods & HOST_MOD_LID) ? FIRMWARE_LID_UP : FIRMWARE_UP;
        case HOST_KEY_DOWN:      return (mods & HOST_MOD_LID) ? FIRMWARE_LID_DOWN : FIRMWARE_DOWN;
        case HOST_KEY_LEFT:      return FIRMWARE_LEFT;
        case HOST_KEY_RIGHT:     return FIRMWARE_RIGHT;
        case HOST_KEY_PGUP:      return FIRMWARE_LID_UP;
        case HOST_KEY_PGDN:      return FIRMWARE_LID_DOWN;
        case HOST_KEY_F1 + 5:    return FIRMWARE_LIGHT;
        default: break;
    }
    if (code >= HOST_KEY_F1 && code < HOST_KEY_F1 + 5) return APP_KEYS[code - HOST_KEY_F1];
    return 0;
}

static bool translate(host_key_t key, key_action_t *action) {
    action->count = 0;
    uint32_t code = key.code;
    uint8_t mods = key.mods;
    if (code == POWER_KEY_CODE) {
        add_step(action, POWER_KEY_CODE, 0);
        return true;
    }
    if (code == HOST_KEY_HOME || code == HOST_KEY_END) {
        add_step(action, FIRMWARE_SECOND, 0);
        add_step(action, code == HOST_KEY_HOME ? FIRMWARE_LEFT : FIRMWARE_RIGHT, 0);
        return true;
    }
    bool shifted = (mods & HOST_MOD_SHIFT) != 0;
    if (code >= 'A' && code <= 'Z') {
        code += 'a' - 'A';
        shifted = true;
    }
    for (size_t i = 0; i < sizeof SHIFTED_KEYS / sizeof SHIFTED_KEYS[0]; i++) {
        if (code == (uint32_t)(unsigned char)SHIFTED_KEYS[i].character) {
            code = SHIFTED_KEYS[i].code;
            shifted = true;
            break;
        }
    }
    uint16_t firmware = firmware_code(code, mods);
    if (!firmware) return false;
    if (mods & HOST_MOD_SECOND) add_step(action, FIRMWARE_SECOND, 0);
    add_step(action, firmware, shifted ? FIRMWARE_SHIFT : 0);
    return true;
}

static void apply_step(const key_step_t *step, bool down) {
    for (int i = 0; i < step->count; i++) {
        if (step->codes[i] == POWER_KEY_CODE) {
            machine_set_power_key(machine, down);
            continue;
        }
        int column, row;
        if (find_code(step->codes[i], &column, &row)) machine_set_key(machine, column, row, down);
    }
}

static void push_action(const key_action_t *action) {
    unsigned next = (queue_head + 1) % QUEUE_SIZE;
    if (next == queue_tail) return;
    queue[queue_head] = *action;
    queue_head = next;
}

static void enqueue(host_key_t key) {
    key_action_t action;
    if (!translate(key, &action)) {
        char message[64];
        snprintf(message, sizeof message, "no mapping for key %x mods %x", key.code, key.mods);
        console_notice(message);
        return;
    }
    push_action(&action);
}

static void advance_keys(double elapsed_ms) {
    step_time_ms += elapsed_ms;
    if (step_pressed) {
        if (step_time_ms < active_step.hold_ms) return;
        apply_step(&active_step, false);
        step_pressed = false;
        step_time_ms = 0;
        return;
    }
    if (step_time_ms < GAP_MS || queue_tail == queue_head) return;
    key_action_t *action = &queue[queue_tail];
    active_step = action->steps[action_step++];
    if (action_step >= action->count) {
        action_step = 0;
        queue_tail = (queue_tail + 1) % QUEUE_SIZE;
    }
    apply_step(&active_step, true);
    step_pressed = true;
    step_time_ms = 0;
}

static uint16_t keyboard_code(const char *id) {
    if (id[0] >= 'a' && id[0] <= 'z' && id[1] == 0) return (uint16_t)id[0];
    if (strncmp(id, "digit_", 6) == 0 && id[6] >= '0' && id[6] <= '9') return (uint16_t)id[6];
    for (size_t i = 0; i < sizeof NAMED_KEYS / sizeof NAMED_KEYS[0]; i++) {
        if (strcmp(id, NAMED_KEYS[i].id) == 0) return NAMED_KEYS[i].code;
    }
    return 0;
}

static void set_matrix_code(uint16_t code, bool down) {
    int column, row;
    if (find_code(code, &column, &row)) machine_set_key(machine, column, row, down);
}

bool runtime_keyboard_key(const char *id, bool down) {
    uint16_t code = machine ? keyboard_code(id) : 0;
    if (!code) return false;
    for (int i = 0; i < held_key_count; i++) {
        if (held_keys[i].code != code) continue;
        if (down && !held_keys[i].held_by_host) {
            held_keys[i].held_by_host = true;
            if (held_keys[i].sticky && code == FIRMWARE_SHIFT) held_keys[i].sticky = false;
            else held_keys[i].pressed_at = host_ticks_ms();
            set_matrix_code(code, true);
        } else if (!down && held_keys[i].held_by_host) {
            held_keys[i].held_by_host = false;
            if (code != FIRMWARE_SHIFT) {
                for (int other = 0; other < held_key_count; other++) {
                    if (held_keys[other].code == FIRMWARE_SHIFT && !held_keys[other].held_by_host && held_keys[other].sticky) {
                        held_keys[other].sticky = false;
                        held_keys[other].pressed_at = held_keys[i].pressed_at;
                    }
                }
            }
        }
        return true;
    }
    if (down && held_key_count < MAX_HELD_KEYS) {
        held_keys[held_key_count++] = (held_key_t){ code, true, code == FIRMWARE_SHIFT, host_ticks_ms() };
        set_matrix_code(code, true);
    }
    return true;
}

bool runtime_keyboard_latched(const char *id) {
    uint16_t code = machine ? keyboard_code(id) : 0;
    for (int i = 0; code && i < held_key_count; i++) {
        if (held_keys[i].code == code && !held_keys[i].held_by_host) return held_keys[i].sticky;
    }
    return false;
}

static void release_keyboard_keys(uint32_t now) {
    for (int i = 0; i < held_key_count;) {
        if (!held_keys[i].held_by_host && !held_keys[i].sticky && now - held_keys[i].pressed_at >= PRESS_MS) {
            set_matrix_code(held_keys[i].code, false);
            held_keys[i] = held_keys[--held_key_count];
        } else {
            i++;
        }
    }
}

static void present_screen(void) {
    const uint8_t *screen = machine_screen(machine);
    for (int y = 0; y < LCD_HEIGHT; y++) {
        for (int x = 0; x < LCD_WIDTH; x++) {
            bool ink = screen[y * MACHINE_SCREEN_ROW_BYTES + x / 8] >> (x % 8) & 1;
            lcd_framebuffer[y * LCD_WIDTH + x] = ink ? 3 : 0;
        }
    }
}

static void reset_holding(uint16_t first, uint16_t second, const char *notice) {
    machine_release_keys(machine);
    queue_head = queue_tail = 0;
    step_pressed = false;
    action_step = 0;
    key_action_t action = { .count = 0 };
    add_step(&action, first, second);
    active_step = action.steps[0];
    active_step.hold_ms = INIT_HOLD_MS;
    apply_step(&active_step, true);
    step_pressed = true;
    step_time_ms = 0;
    machine_reset(machine);
    boots++;
    console_notice(notice);
}

void runtime_initialize_memory(void) {
    reset_holding(POWER_KEY_CODE, 0, "reset with ON held");
}

void runtime_enter_test_mode(void) {
    reset_holding(FIRMWARE_ESC, 'd', "reset with ESC and D held: factory test mode");
}

static void save_state(void) {
    if (!persist || !machine) return;
    if (!machine_save(machine, state_path, (int64_t)time(NULL))) console_notice("could not save state");
    last_save_ms = host_ticks_ms();
}

static void load_state(void) {
    int64_t saved_at = 0;
    if (!persist) return;
    if (!machine_load(machine, state_path, &saved_at)) {
        FILE *existing = fopen(state_path, "rb");
        if (!existing) return;
        fclose(existing);
        char backup[sizeof state_path + 8];
        snprintf(backup, sizeof backup, "%s.old", state_path);
        rename(state_path, backup);
        char message[sizeof backup + 80];
        snprintf(message, sizeof message, "saved state is from an older version, kept as %s and starting fresh", backup);
        console_notice(message);
        return;
    }
    int64_t away = (int64_t)time(NULL) - saved_at;
    if (away > MAX_CLOCK_CATCH_UP) away = MAX_CLOCK_CATCH_UP;
    if (away > 0) machine_advance_clock(machine, away);
    console_notice("restored saved state");
}

static uint64_t cycles_for_ms(uint64_t ms) {
    return ms * MACHINE_CLOCK_HZ / 1000;
}

static void queue_sound(float frequency, uint64_t until) {
    uint64_t whole_ms = (until - sound_cursor) * 1000 / MACHINE_CLOCK_HZ;
    if (whole_ms == 0) return;
    if (beeper_sound()) {
        bool busy = beeper_busy();
        if (frequency > 0 && !busy) beeper_tone(0, 0, AUDIO_LATENCY_MS);
        if (frequency > 0 || busy) beeper_tone(frequency, 0, (uint32_t)whole_ms);
    }
    sound_cursor += cycles_for_ms(whole_ms);
}

static void drain_sound(void) {
    machine_sound_event_t event;
    while (machine_pop_sound(machine, &event)) {
        queue_sound(sound_frequency, event.cycle);
        sound_frequency = event.frequency;
    }
    queue_sound(sound_frequency, machine_cycles(machine));
}

static void present_lcd(void) {
    machine_lcd_t lcd = machine_lcd(machine);
    if (lcd.on != shown_lcd.on) lcd_set_power(lcd.on);
    if (lcd.backlight != shown_lcd.backlight) lcd_set_backlight(lcd.backlight);
    if (lcd.contrast != shown_lcd.contrast && lcd.on) lcd_set_contrast(5 + (lcd.contrast - DEFAULT_CONTRAST) / 3);
    shown_lcd = lcd;
}

bool runtime_install_wzd(const char *path) {
    char message[256];
    char error[160] = "";
    size_t size = 0;
    uint8_t *data = machine ? read_file(path, &size) : NULL;
    static uint8_t slot_image[WZD_SLOT_SIZE];
    wzd_program_t program;
    size_t slot_length = 0;
    int slot = machine ? machine_free_addin_slot(machine) : -1;
    if (!data) snprintf(error, sizeof error, "cannot read the file");
    else if (slot < 0) snprintf(error, sizeof error, "all %d My Programs slots are in use", MACHINE_ADDIN_SLOTS);
    else if (wzd_parse(data, size, &program, error, sizeof error)) slot_length = wzd_build_slot(&program, slot, slot_image, error, sizeof error);
    bool installed = slot_length && machine_write_addin_slot(machine, slot, slot_image, slot_length);
    if (installed) snprintf(message, sizeof message, "installed %s as My Programs %d", program.title, slot + 1);
    else snprintf(message, sizeof message, "cannot install %s: %s", path, error);
    console_notice(message);
    free(data);
    return installed;
}

static void run_command(const char *line) {
    char message[128];
    if (strcmp(line, "reset") == 0) {
        machine_reset(machine);
        console_notice("reset");
    } else if (strcmp(line, "init") == 0) {
        runtime_initialize_memory();
    } else if (strcmp(line, "testmode") == 0) {
        runtime_enter_test_mode();
    } else if (strcmp(line, "on") == 0) {
        enqueue((host_key_t){ POWER_KEY_CODE, 0 });
    } else if (strcmp(line, "pc") == 0) {
        snprintf(message, sizeof message, "pc %04x%s", machine_pc(machine), machine_halted(machine) ? " halted" : "");
        console_notice(message);
    } else if (strncmp(line, "install ", 8) == 0) {
        runtime_install_wzd(line + 8);
    } else if (strcmp(line, "save") == 0) {
        save_state();
        console_notice(persist ? "saved" : "persistence is off");
    } else if (strcmp(line, "trace on") == 0 || strcmp(line, "trace off") == 0) {
        machine_set_trace_ports(machine, strcmp(line, "trace on") == 0);
    } else {
        console_notice("commands: reset, init (reset holding ON), testmode (reset holding ESC+D), on, install PATH, save, pc, trace on, trace off");
    }
}

void runtime_service(void) {
    char *line;
    while ((line = console_take_input()) != NULL) {
        size_t length = strlen(line);
        while (length && (line[length - 1] == '\n' || line[length - 1] == ' ')) line[--length] = 0;
        if (length) run_command(line);
        free(line);
    }
}

bool runtime_init(const host_config_t *config) {
    rom_image = read_file(config->rom_path, &rom_size);
    if (!rom_image) {
        fprintf(stderr, "zq77x-emu: cannot read ROM %s\n", config->rom_path);
        return false;
    }
    load_keycode_table();
    machine = machine_create(rom_image, rom_size);
    if (!machine) return false;
    machine_set_log(machine, log_to_console);
    persist = config->persist;
    snprintf(state_path, sizeof state_path, "%s/state.bin", config->data_path);
    load_state();
    sound_cursor = machine_cycles(machine);
    boots = 1;
    last_step_ms = last_save_ms = host_ticks_ms();
    return true;
}

void runtime_step(void) {
    if (!machine) return;
    runtime_service();
    host_key_t key;
    while (keys_pop(&key)) enqueue(key);
    uint32_t now = host_ticks_ms();
    release_keyboard_keys(now);
    uint32_t elapsed = now - last_step_ms;
    last_step_ms = now;
    if (elapsed > MAX_CATCH_UP_MS) elapsed = MAX_CATCH_UP_MS;
    const uint32_t slice_ms = 2;
    for (uint32_t done = 0; done < elapsed; done += slice_ms) {
        uint32_t this_slice = elapsed - done < slice_ms ? elapsed - done : slice_ms;
        advance_keys(this_slice);
        uint64_t cycles = cycles_for_ms(emulated_ms + this_slice) - cycles_for_ms(emulated_ms);
        emulated_ms += this_slice;
        machine_run(machine, (uint32_t)cycles);
    }
    drain_sound();
    present_lcd();
    present_screen();
    if (persist && now - last_save_ms >= AUTOSAVE_MS) save_state();
}

void runtime_request_reload(void) {
    if (!machine) return;
    machine_release_keys(machine);
    machine_reset(machine);
    boots++;
    console_notice("reset");
}

void runtime_interrupt(void) {
    enqueue((host_key_t){ POWER_KEY_CODE, 0 });
}

void runtime_press_power(void) {
    enqueue((host_key_t){ POWER_KEY_CODE, 0 });
}

bool runtime_idle(void) {
    return machine && machine_halted(machine);
}

bool runtime_console_busy(void) {
    return false;
}

int runtime_boots(void) {
    return boots;
}

const char *runtime_get_resume(void) {
    return "";
}

void runtime_set_resume(const char *name) {
    (void)name;
}

void runtime_deinit(void) {
    save_state();
    machine_destroy(machine);
    machine = NULL;
    free(rom_image);
    rom_image = NULL;
}
