#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "console.h"
#include "keys.h"
#include "lcd.h"
#include "machine.h"
#include "runtime.h"

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
#define FIRMWARE_LIGHT     0x803b
#define POWER_KEY_CODE     0xffff
#define MAX_STEP_KEYS      2
#define MAX_STEPS          4
#define QUEUE_SIZE         64
#define PRESS_MS           80
#define GAP_MS             60
#define INIT_HOLD_MS       1500
#define MAX_CATCH_UP_MS    100

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

bool mp_repl_continue_with_input(const char *input) {
    (void)input;
    return false;
}

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
        case HOST_KEY_ENTER:     return FIRMWARE_ENTER;
        case HOST_KEY_ESC:       return FIRMWARE_ESC;
        case HOST_KEY_TAB:       return FIRMWARE_MENU;
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

static void present_screen(void) {
    const uint8_t *screen = machine_screen(machine);
    for (int y = 0; y < LCD_HEIGHT; y++) {
        for (int x = 0; x < LCD_WIDTH; x++) {
            bool ink = screen[y * MACHINE_SCREEN_ROW_BYTES + x / 8] >> (x % 8) & 1;
            lcd_framebuffer[y * LCD_WIDTH + x] = ink ? 3 : 0;
        }
    }
}

void runtime_initialize_memory(void) {
    machine_release_keys(machine);
    queue_head = queue_tail = 0;
    step_pressed = false;
    action_step = 0;
    key_action_t action = { .count = 0 };
    add_step(&action, POWER_KEY_CODE, 0);
    active_step = action.steps[0];
    active_step.hold_ms = INIT_HOLD_MS;
    apply_step(&active_step, true);
    step_pressed = true;
    step_time_ms = 0;
    machine_reset(machine);
    boots++;
    console_notice("reset with ON held");
}

static void run_command(const char *line) {
    char message[128];
    if (strcmp(line, "reset") == 0) {
        machine_reset(machine);
        console_notice("reset");
    } else if (strcmp(line, "init") == 0) {
        runtime_initialize_memory();
    } else if (strcmp(line, "on") == 0) {
        enqueue((host_key_t){ POWER_KEY_CODE, 0 });
    } else if (strcmp(line, "pc") == 0) {
        snprintf(message, sizeof message, "pc %04x%s", machine_pc(machine), machine_halted(machine) ? " halted" : "");
        console_notice(message);
    } else if (strcmp(line, "trace on") == 0 || strcmp(line, "trace off") == 0) {
        machine_set_trace_ports(machine, strcmp(line, "trace on") == 0);
    } else {
        console_notice("commands: reset, init (reset holding ON), on, pc, trace on, trace off");
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
    boots = 1;
    last_step_ms = host_ticks_ms();
    return true;
}

void runtime_step(void) {
    if (!machine) return;
    runtime_service();
    host_key_t key;
    while (keys_pop(&key)) enqueue(key);
    uint32_t now = host_ticks_ms();
    uint32_t elapsed = now - last_step_ms;
    last_step_ms = now;
    if (elapsed > MAX_CATCH_UP_MS) elapsed = MAX_CATCH_UP_MS;
    const uint32_t slice_ms = 2;
    for (uint32_t done = 0; done < elapsed; done += slice_ms) {
        uint32_t this_slice = elapsed - done < slice_ms ? elapsed - done : slice_ms;
        advance_keys(this_slice);
        machine_run(machine, MACHINE_CLOCK_HZ / 1000 * this_slice);
    }
    present_screen();
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

bool runtime_repl_busy(void) {
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
    machine_destroy(machine);
    machine = NULL;
    free(rom_image);
    rom_image = NULL;
}
