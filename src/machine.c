#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "machine.h"
#include "z80.h"

#define PAGE_SIZE            0x2000
#define FLASH_SIZE           0x400000
#define FLASH_BLOCK_SIZE     0x10000
#define FLASH_FIRST_DATA_PAGE 0x48
#define FLASH_MANUFACTURER_ID 0x89
#define FLASH_DEVICE_ID      0xa6
#define ADDIN_FIRST_PAGE     0x60
#define ADDIN_SLOT_PAGES     4
#define ADDIN_ACTIVE_BIT     0x40
#define FLASH_PAGES          (FLASH_SIZE / PAGE_SIZE)
#define RAM_FIRST_PAGE       0x400
#define RAM_CHIP_PAGES       16
#define RAM_SECOND_CHIP_PAGE 0x500
#define RAM_PAGES            (RAM_CHIP_PAGES * 2)
#define LCD_CONTROL_PAGE     0x300
#define FIXED_RAM_BASE       0xc000
#define BACKLIGHT_SECONDS    0xc00d
#define TIMEOUT_HOLD         30
#define FIXED_RAM_FIRST_PAGE 0x402
#define TICK_HZ              64

#define INTERRUPT_KEYBOARD   0x01
#define INTERRUPT_SERIAL     0x04
#define INTERRUPT_SECOND     0x10
#define INTERRUPT_TICK       0x20
#define INTERRUPT_POWER_KEY  0x80
#define POWER_KEY_BIT        0x10
#define STATUS_INPUTS        0xe8
#define ZQ770_STATUS_INPUTS  0xf8
#define FLASH_MIRROR_FIRST   0x180
#define FLASH_MIRROR_OFFSET  0x80
#define SOUND_QUEUE_SIZE     256
#define SOUND_BASE_HZ        16384.0f
#define SNAPSHOT_MAGIC       "ZQ77XSNAP3"
#define UART_CLOCK_BAUD      153600
#define UART_BITS_PER_BYTE   10
#define UART_RX_QUEUE_SIZE   4096
#define UART_LCR_DLAB        0x80
#define UART_IER_RX          0x01
#define UART_IER_THRE        0x02
#define UART_LSR_DATA_READY  0x01
#define UART_LSR_THRE        0x20
#define UART_LSR_TEMT        0x40
#define UART_IIR_NONE        0x01
#define UART_IIR_THRE        0x02
#define UART_IIR_RX          0x04

typedef enum {
    FLASH_READ_ARRAY,
    FLASH_READ_STATUS,
    FLASH_READ_ID,
    FLASH_PROGRAM_SETUP,
    FLASH_ERASE_SETUP,
    FLASH_LOCK_SETUP,
} flash_mode_t;

typedef struct {
    flash_mode_t mode;
    uint32_t     block;
    uint8_t      status;
} flash_state_t;

typedef struct {
    uint8_t  registers[4][13];
    uint8_t  mode;
    uint32_t cycles_into_second;
} rtc_t;

typedef struct {
    uint8_t  divisor_low;
    uint8_t  divisor_high;
    uint8_t  interrupt_enable;
    uint8_t  line_control;
    uint8_t  modem_control;
    uint8_t  scratch;
    uint8_t  receive_buffer;
    bool     receive_full;
    bool     transmit_busy;
    bool     transmit_empty_pending;
    uint64_t receive_ready_at;
    uint64_t transmit_done_at;
    uint8_t  queue[UART_RX_QUEUE_SIZE];
    unsigned queue_head;
    unsigned queue_tail;
    machine_serial_out_fn output;
    void    *output_context;
} uart_t;

typedef struct {
    uint16_t low_window_page;
    uint16_t high_window_page;
    uint16_t display_page;
    uint16_t lcd_control;
    bool     lcd_control_written;
    uint8_t  interrupt_status;
    uint8_t  interrupt_mask;
    uint8_t  column_select_low;
    uint8_t  column_select_high;
    uint8_t  ports[256];
    uint32_t cycles_into_tick;
    rtc_t    rtc;
    flash_state_t flash_state;
} saved_state_t;

struct machine {
    z80      cpu;
    machine_model_t model;
    uint8_t  bus;
    uint8_t *flash;
    uint8_t *ram;
    flash_state_t flash_state;
    uint16_t low_window_page;
    uint16_t high_window_page;
    uint16_t display_page;
    uint16_t lcd_control;
    bool     lcd_control_written;
    uint8_t  interrupt_status;
    uint8_t  interrupt_mask;
    uint8_t  column_select_low;
    uint8_t  column_select_high;
    uint8_t  keys[MACHINE_KEY_COLUMNS];
    bool     power_key;
    uint8_t  ports[256];
    bool     trace_ports;
    uint32_t cycles_into_tick;
    rtc_t    rtc;
    uart_t   uart;
    bool     backlight_timeout;
    uint8_t  screen[MACHINE_SCREEN_ROW_BYTES * MACHINE_SCREEN_HEIGHT];
    machine_log_fn log;
    uint32_t *pc_histogram;
    int watch_pc;
    int watch_hits;
    float    sound_frequency;
    machine_sound_event_t sound_queue[SOUND_QUEUE_SIZE];
    unsigned sound_head;
    unsigned sound_tail;
};

static void machine_log(machine_t *machine, const char *format, ...) {
    char message[256];
    va_list args;
    va_start(args, format);
    vsnprintf(message, sizeof message, format, args);
    va_end(args);
    if (machine->log) machine->log(message);
    else fprintf(stderr, "%s\n", message);
}

static int ram_index(machine_t *machine, uint16_t page) {
    if (machine->model == MACHINE_MODEL_ZQ770) {
        if (page >= RAM_FIRST_PAGE && page < RAM_SECOND_CHIP_PAGE) return (page - RAM_FIRST_PAGE) % RAM_CHIP_PAGES;
        return -1;
    }
    if (page >= RAM_FIRST_PAGE && page < RAM_FIRST_PAGE + RAM_CHIP_PAGES) return page - RAM_FIRST_PAGE;
    if (page >= RAM_SECOND_CHIP_PAGE && page < RAM_SECOND_CHIP_PAGE + RAM_CHIP_PAGES) {
        return RAM_CHIP_PAGES + page - RAM_SECOND_CHIP_PAGE;
    }
    return -1;
}

static uint8_t *page_pointer(machine_t *machine, uint16_t page) {
    if (page < FLASH_PAGES) return machine->flash + (size_t)page * PAGE_SIZE;
    int index = ram_index(machine, page);
    if (index >= 0) return machine->ram + (size_t)index * PAGE_SIZE;
    return NULL;
}

static bool page_is_ram(machine_t *machine, uint16_t page) {
    return ram_index(machine, page) >= 0;
}

static uint16_t flash_mirror(uint16_t page) {
    return page >= FLASH_MIRROR_FIRST && page < FLASH_PAGES ? (uint16_t)(page - FLASH_MIRROR_OFFSET) : page;
}

static uint16_t window_page(machine_t *machine, uint16_t address) {
    if (address >= FIXED_RAM_BASE) return (uint16_t)(FIXED_RAM_FIRST_PAGE + ((address - FIXED_RAM_BASE) >> 13));
    if (address < 0xa000) return flash_mirror((uint16_t)(machine->low_window_page + 4));
    return flash_mirror(machine->high_window_page);
}

static uint32_t flash_address(uint16_t page, uint16_t address) {
    return (uint32_t)page * PAGE_SIZE + (address & (PAGE_SIZE - 1));
}

static uint8_t flash_read(machine_t *machine, uint32_t offset) {
    flash_state_t *flash = &machine->flash_state;
    if (flash->mode == FLASH_READ_ARRAY || offset / FLASH_BLOCK_SIZE != flash->block) return machine->flash[offset];
    if (flash->mode == FLASH_READ_ID) {
        static const uint8_t identifier[4] = { FLASH_MANUFACTURER_ID, FLASH_DEVICE_ID, 0x00, 0x00 };
        return identifier[offset & 3];
    }
    return flash->status;
}

static void flash_write(machine_t *machine, uint32_t offset, uint8_t value) {
    flash_state_t *flash = &machine->flash_state;
    uint32_t block = offset / FLASH_BLOCK_SIZE;
    if (flash->mode == FLASH_PROGRAM_SETUP) {
        if (offset >= (uint32_t)FLASH_FIRST_DATA_PAGE * PAGE_SIZE) machine->flash[offset] &= value;
        if (machine->trace_ports) machine_log(machine, "flash program %06x <- %02x = %02x pc %04x", offset, value, machine->flash[offset], machine->cpu.pc);
        flash->mode = FLASH_READ_STATUS;
        flash->block = block;
        flash->status = 0x80;
        return;
    }
    if (flash->mode == FLASH_ERASE_SETUP) {
        if (machine->trace_ports) machine_log(machine, "flash erase %02x block %03x pc %04x", value, block, machine->cpu.pc);
        if (value == 0xd0 && offset >= (uint32_t)FLASH_FIRST_DATA_PAGE * PAGE_SIZE) {
            memset(machine->flash + (size_t)block * FLASH_BLOCK_SIZE, 0xff, FLASH_BLOCK_SIZE);
        }
        flash->mode = FLASH_READ_STATUS;
        flash->block = block;
        flash->status = 0x80;
        return;
    }
    if (flash->mode == FLASH_LOCK_SETUP) {
        if (machine->trace_ports) machine_log(machine, "flash lock %02x block %03x pc %04x", value, block, machine->cpu.pc);
        flash->mode = FLASH_READ_STATUS;
        flash->block = block;
        flash->status = 0x80;
        return;
    }
    if (machine->trace_ports) machine_log(machine, "flash command %02x at %06x pc %04x", value, offset, machine->cpu.pc);
    flash->block = block;
    switch (value) {
        case 0xff: flash->mode = FLASH_READ_ARRAY; break;
        case 0x70: flash->mode = FLASH_READ_STATUS; break;
        case 0x90: flash->mode = FLASH_READ_ID; break;
        case 0x50: flash->status = 0x80; break;
        case 0x10:
        case 0x40: flash->mode = FLASH_PROGRAM_SETUP; break;
        case 0x20: flash->mode = FLASH_ERASE_SETUP; break;
        case 0x60: flash->mode = FLASH_LOCK_SETUP; break;
        default:
            machine_log(machine, "flash command %02x at %06x pc %04x", value, offset, machine->cpu.pc);
            break;
    }
}

static uint8_t read_memory(machine_t *machine, uint16_t address) {
    if (address < 0x8000) return flash_read(machine, address);
    uint16_t page = window_page(machine, address);
    if (page < FLASH_PAGES) {
        if (machine->trace_ports && page >= FLASH_FIRST_DATA_PAGE) machine_log(machine, "data read page %03x addr %04x pc %04x", page, address, machine->cpu.pc);
        return flash_read(machine, flash_address(page, address));
    }
    uint8_t *memory = page_pointer(machine, page);
    if (memory) return memory[address & (PAGE_SIZE - 1)];
    bool open_bus = machine->model == MACHINE_MODEL_ZQ770;
    if (page == LCD_CONTROL_PAGE && !open_bus) return (address & 1) ? machine->lcd_control >> 8 : machine->lcd_control & 0xff;
    if (machine->trace_ports) machine_log(machine, "unmapped read page %03x addr %04x pc %04x", page, address, machine->cpu.pc);
    return open_bus ? machine->bus : 0xff;
}

static uint8_t read_byte(void *userdata, uint16_t address) {
    machine_t *machine = userdata;
    machine->bus = read_memory(machine, address);
    return machine->bus;
}

static void write_byte(void *userdata, uint16_t address, uint8_t value) {
    machine_t *machine = userdata;
    if (address < 0x8000) {
        if (machine->trace_ports) machine_log(machine, "fixed flash write %04x <- %02x pc %04x low %03x high %03x", address, value, machine->cpu.pc, machine->low_window_page, machine->high_window_page);
        flash_write(machine, address, value);
        return;
    }
    uint16_t page = window_page(machine, address);
    if (page < FLASH_PAGES) {
        flash_write(machine, flash_address(page, address), value);
        return;
    }
    if (page_is_ram(machine, page)) {
        page_pointer(machine, page)[address & (PAGE_SIZE - 1)] = value;
        return;
    }
    if (page == LCD_CONTROL_PAGE) {
        machine->lcd_control_written = true;
        if (address & 1) machine->lcd_control = (uint16_t)((machine->lcd_control & 0x00ff) | value << 8);
        else machine->lcd_control = (uint16_t)((machine->lcd_control & 0xff00) | value);
        return;
    }
}

static uint8_t keyboard_rows(machine_t *machine) {
    uint8_t rows = 0;
    for (int column = 0; column < 8; column++) {
        if (machine->column_select_low & (1 << column)) rows |= machine->keys[column];
    }
    for (int column = 8; column < MACHINE_KEY_COLUMNS; column++) {
        if (machine->column_select_high & (1 << (column - 8))) rows |= machine->keys[column];
    }
    return rows;
}

static void note_port(machine_t *machine, uint8_t port, bool write, uint8_t value) {
    if (!machine->trace_ports) return;
    machine_log(machine, "port %02x %s %02x at pc %04x t %.3f", port, write ? "<-" : "->", value, machine->cpu.pc,
                (double)machine->cpu.cyc / MACHINE_CLOCK_HZ);
}

static uint8_t rtc_read(machine_t *machine, uint8_t index) {
    rtc_t *rtc = &machine->rtc;
    if (index == 0x0d) return rtc->mode;
    if (index > 0x0c) return 0;
    return rtc->registers[rtc->mode & 3][index];
}

static void rtc_write(machine_t *machine, uint8_t index, uint8_t value) {
    rtc_t *rtc = &machine->rtc;
    value &= 0x0f;
    if (index == 0x0d) rtc->mode = value;
    else if (index <= 0x0c) rtc->registers[rtc->mode & 3][index] = value;
}

static void rtc_set_digits(uint8_t *registers, int index, int value) {
    registers[index] = (uint8_t)(value % 10);
    registers[index + 1] = (uint8_t)(value / 10);
}

static int rtc_get_digits(const uint8_t *registers, int index) {
    return registers[index] + registers[index + 1] * 10;
}

static void rtc_load_host_time(rtc_t *rtc) {
    time_t now = time(NULL);
    struct tm local;
#ifdef _WIN32
    localtime_s(&local, &now);
#else
    localtime_r(&now, &local);
#endif
    uint8_t *clock = rtc->registers[0];
    rtc_set_digits(clock, 0, local.tm_sec);
    rtc_set_digits(clock, 2, local.tm_min);
    rtc_set_digits(clock, 4, local.tm_hour);
    clock[6] = (uint8_t)local.tm_wday;
    rtc_set_digits(clock, 7, local.tm_mday);
    rtc_set_digits(clock, 9, local.tm_mon + 1);
    rtc_set_digits(clock, 11, local.tm_year % 100);
    rtc->registers[1][0x0a] = 1;
}

static void rtc_advance_second(rtc_t *rtc) {
    static const int days_in_month[] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    uint8_t *clock = rtc->registers[0];
    int second = rtc_get_digits(clock, 0) + 1;
    int minute = rtc_get_digits(clock, 2);
    int hour = rtc_get_digits(clock, 4);
    int day = rtc_get_digits(clock, 7);
    int month = rtc_get_digits(clock, 9);
    int year = rtc_get_digits(clock, 11);
    if (second >= 60) { second = 0; minute++; }
    if (minute >= 60) { minute = 0; hour++; }
    if (hour >= 24) {
        hour = 0;
        day++;
        clock[6] = (uint8_t)((clock[6] + 1) % 7);
    }
    int month_length = month >= 1 && month <= 12 ? days_in_month[month - 1] : 31;
    if (month == 2 && year % 4 == 0) month_length = 29;
    if (day > month_length) { day = 1; month++; }
    if (month > 12) { month = 1; year = (year + 1) % 100; }
    rtc_set_digits(clock, 0, second);
    rtc_set_digits(clock, 2, minute);
    rtc_set_digits(clock, 4, hour);
    rtc_set_digits(clock, 7, day);
    rtc_set_digits(clock, 9, month);
    rtc_set_digits(clock, 11, year);
}

static void update_sound(machine_t *machine) {
    uint16_t divisor = (uint16_t)(machine->ports[0x17] | machine->ports[0x18] << 8);
    float frequency = (machine->ports[0x16] & 1) ? SOUND_BASE_HZ / (float)(divisor + 2) : 0.0f;
    if (frequency == machine->sound_frequency) return;
    machine->sound_frequency = frequency;
    unsigned next = (machine->sound_head + 1) % SOUND_QUEUE_SIZE;
    if (next == machine->sound_tail) return;
    machine->sound_queue[machine->sound_head] = (machine_sound_event_t){ machine->cpu.cyc, frequency };
    machine->sound_head = next;
}

static uint64_t uart_byte_cycles(const uart_t *uart) {
    unsigned divisor = (unsigned)(uart->divisor_low | uart->divisor_high << 8);
    if (divisor == 0) divisor = 0x10000;
    return (uint64_t)MACHINE_CLOCK_HZ * UART_BITS_PER_BYTE * divisor / UART_CLOCK_BAUD;
}

static void uart_update(machine_t *machine) {
    uart_t *uart = &machine->uart;
    uint64_t now = machine->cpu.cyc;
    if (uart->transmit_busy && now >= uart->transmit_done_at) {
        uart->transmit_busy = false;
        uart->transmit_empty_pending = true;
    }
    if (!uart->receive_full && uart->queue_tail != uart->queue_head && now >= uart->receive_ready_at) {
        uart->receive_buffer = uart->queue[uart->queue_tail];
        uart->queue_tail = (uart->queue_tail + 1) % UART_RX_QUEUE_SIZE;
        uart->receive_full = true;
    }
    bool receive_interrupt = (uart->interrupt_enable & UART_IER_RX) && uart->receive_full;
    bool transmit_interrupt = (uart->interrupt_enable & UART_IER_THRE) && uart->transmit_empty_pending;
    if (receive_interrupt || transmit_interrupt) machine->interrupt_status |= INTERRUPT_SERIAL;
}

static uint8_t uart_read(machine_t *machine, uint8_t index) {
    uart_t *uart = &machine->uart;
    bool latch = uart->line_control & UART_LCR_DLAB;
    switch (index) {
        case 0:
            if (latch) return uart->divisor_low;
            if (uart->receive_full) {
                uart->receive_full = false;
                uart->receive_ready_at = machine->cpu.cyc + uart_byte_cycles(uart);
            }
            return uart->receive_buffer;
        case 1: return latch ? uart->divisor_high : uart->interrupt_enable;
        case 2:
            if ((uart->interrupt_enable & UART_IER_RX) && uart->receive_full) return UART_IIR_RX;
            if ((uart->interrupt_enable & UART_IER_THRE) && uart->transmit_empty_pending) {
                uart->transmit_empty_pending = false;
                return UART_IIR_THRE;
            }
            return UART_IIR_NONE;
        case 3: return uart->line_control;
        case 4: return uart->modem_control;
        case 5: return (uint8_t)((uart->receive_full ? UART_LSR_DATA_READY : 0) | (uart->transmit_busy ? 0 : UART_LSR_THRE | UART_LSR_TEMT));
        default: return uart->scratch;
    }
}

static void uart_write(machine_t *machine, uint8_t index, uint8_t value) {
    uart_t *uart = &machine->uart;
    bool latch = uart->line_control & UART_LCR_DLAB;
    switch (index) {
        case 0:
            if (latch) {
                uart->divisor_low = value;
                return;
            }
            if (uart->output) uart->output(uart->output_context, value);
            uart->transmit_busy = true;
            uart->transmit_empty_pending = false;
            uart->transmit_done_at = machine->cpu.cyc + uart_byte_cycles(uart);
            return;
        case 1:
            if (latch) {
                uart->divisor_high = value;
                return;
            }
            if ((value & UART_IER_THRE) && !(uart->interrupt_enable & UART_IER_THRE) && !uart->transmit_busy) uart->transmit_empty_pending = true;
            uart->interrupt_enable = value & 0x0f;
            return;
        case 3: uart->line_control = value; return;
        case 4: uart->modem_control = value; return;
        case 7: uart->scratch = value; return;
        default: return;
    }
}

unsigned machine_serial_baud(machine_t *machine) {
    unsigned divisor = (unsigned)(machine->uart.divisor_low | machine->uart.divisor_high << 8);
    return UART_CLOCK_BAUD / (divisor ? divisor : 0x10000);
}

void machine_set_serial_output(machine_t *machine, machine_serial_out_fn output, void *context) {
    machine->uart.output = output;
    machine->uart.output_context = context;
}

size_t machine_serial_input(machine_t *machine, const uint8_t *data, size_t length) {
    uart_t *uart = &machine->uart;
    size_t accepted = 0;
    while (accepted < length) {
        unsigned next = (uart->queue_head + 1) % UART_RX_QUEUE_SIZE;
        if (next == uart->queue_tail) break;
        uart->queue[uart->queue_head] = data[accepted++];
        uart->queue_head = next;
    }
    return accepted;
}

static bool zq770_port_undecoded(uint8_t port) {
    if (port >= 0x50) return true;
    if (port >= 0x25 && port <= 0x2f) return true;
    if (port >= 0x1a && port <= 0x1f) return true;
    if (port >= 0x0d && port <= 0x0f) return true;
    return port == 0x00 || port == 0x06 || port == 0x09 || port == 0x0a || port == 0x16;
}

static uint8_t zq770_port(uint8_t port) {
    return port >= 0x48 && port <= 0x4f ? (uint8_t)(port - 8) : port;
}

static uint8_t port_in(z80 *cpu, uint8_t port) {
    machine_t *machine = cpu->userdata;
    uint8_t value;
    if (machine->model == MACHINE_MODEL_ZQ770) {
        if (zq770_port_undecoded(port)) return machine->bus;
        port = zq770_port(port);
    }
    switch (port) {
        case 0x01: value = machine->low_window_page & 0xff; break;
        case 0x02: value = machine->low_window_page >> 8; break;
        case 0x03: value = machine->high_window_page & 0xff; break;
        case 0x04: value = machine->high_window_page >> 8; break;
        case 0x05: value = machine->interrupt_status; break;
        case 0x07: value = machine->interrupt_mask; break;
        case 0x10: value = keyboard_rows(machine); break;
        case 0x11: value = machine->column_select_low; break;
        case 0x12: {
            uint8_t inputs = machine->model == MACHINE_MODEL_ZQ770 ? ZQ770_STATUS_INPUTS : STATUS_INPUTS;
            value = (uint8_t)((machine->ports[0x12] & ~inputs) | inputs);
            break;
        }
        case 0x22: value = machine->display_page & 0xff; break;
        case 0x23: value = machine->display_page >> 8; break;
        case 0x46: value = (uint8_t)((machine->ports[0x46] & ~POWER_KEY_BIT) | (machine->power_key ? POWER_KEY_BIT : 0)); break;
        default:
            if (port >= 0x30 && port <= 0x3f) value = rtc_read(machine, port - 0x30);
            else if (port >= 0x40 && port <= 0x47) value = uart_read(machine, port - 0x40);
            else value = machine->ports[port];
            break;
    }
    if (port == 0x10 && machine->trace_ports) machine_log(machine, "keys cols %02x %02x -> %02x at pc %04x", machine->column_select_low, machine->column_select_high, value, machine->cpu.pc);
    if (port != 0x10 && port != 0x05) note_port(machine, port, false, value);
    return value;
}

static void port_out(z80 *cpu, uint8_t port, uint8_t value) {
    machine_t *machine = cpu->userdata;
    if (machine->model == MACHINE_MODEL_ZQ770) port = zq770_port(port);
    machine->ports[port] = value;
    switch (port) {
        case 0x01: machine->low_window_page = (uint16_t)((machine->low_window_page & 0xff00) | value); return;
        case 0x02: machine->low_window_page = (uint16_t)((machine->low_window_page & 0x00ff) | value << 8); return;
        case 0x03: machine->high_window_page = (uint16_t)((machine->high_window_page & 0xff00) | value); return;
        case 0x04: machine->high_window_page = (uint16_t)((machine->high_window_page & 0x00ff) | value << 8); return;
        case 0x06: machine->interrupt_status &= (uint8_t)~value; break;
        case 0x07: machine->interrupt_mask = value; break;
        case 0x11: machine->column_select_low = value; return;
        case 0x12: machine->column_select_high = value & 0x07; return;
        case 0x22: machine->display_page = (uint16_t)((machine->display_page & 0xff00) | value); break;
        case 0x23: machine->display_page = (uint16_t)((machine->display_page & 0x00ff) | value << 8); break;
        case 0x16:
        case 0x17:
        case 0x18:
        case 0x19: update_sound(machine); break;
        default:
            if (port >= 0x30 && port <= 0x3f) rtc_write(machine, port - 0x30, value);
            else if (port >= 0x40 && port <= 0x47) uart_write(machine, port - 0x40, value);
            break;
    }
    note_port(machine, port, true, value);
}

machine_t *machine_create(const uint8_t *flash_image, size_t flash_size, machine_model_t model) {
    machine_t *machine = calloc(1, sizeof *machine);
    if (!machine) return NULL;
    machine->model = model;
    machine->watch_pc = -1;
    machine->flash = malloc(FLASH_SIZE);
    machine->ram = calloc(RAM_PAGES, PAGE_SIZE);
    if (!machine->flash || !machine->ram) {
        machine_destroy(machine);
        return NULL;
    }
    memset(machine->flash, 0xff, FLASH_SIZE);
    memcpy(machine->flash, flash_image, flash_size < FLASH_SIZE ? flash_size : FLASH_SIZE);
    rtc_load_host_time(&machine->rtc);
    machine_reset(machine);
    return machine;
}

machine_model_t machine_get_model(machine_t *machine) {
    return machine->model;
}

void machine_destroy(machine_t *machine) {
    if (!machine) return;
    free(machine->flash);
    free(machine->ram);
    free(machine);
}

void machine_reset(machine_t *machine) {
    z80_init(&machine->cpu);
    machine->cpu.read_byte = read_byte;
    machine->cpu.write_byte = write_byte;
    machine->cpu.port_in = port_in;
    machine->cpu.port_out = port_out;
    machine->cpu.userdata = machine;
    machine->low_window_page = 0;
    machine->high_window_page = 0;
    machine->display_page = RAM_FIRST_PAGE;
    machine->interrupt_status = 0;
    machine->interrupt_mask = 0xff;
    machine->cycles_into_tick = 0;
    machine->lcd_control_written = false;
    machine->ports[0x16] = 0;
    update_sound(machine);
    uart_t *uart = &machine->uart;
    uart->divisor_low = 16;
    uart->divisor_high = 0;
    uart->interrupt_enable = 0;
    uart->line_control = 0x03;
    uart->modem_control = 0;
    uart->receive_full = false;
    uart->transmit_busy = false;
    uart->transmit_empty_pending = false;
    uart->receive_ready_at = 0;
    uart->transmit_done_at = 0;
}

static void update_interrupt_line(machine_t *machine) {
    bool asserted = (machine->interrupt_status & (uint8_t)~machine->interrupt_mask) != 0;
    machine->cpu.int_pending = asserted;
    machine->cpu.int_data = 0xff;
}

static uint8_t *fixed_ram(machine_t *machine, uint16_t address) {
    return page_pointer(machine, window_page(machine, address)) + (address & (PAGE_SIZE - 1));
}

static void hold_timeouts(machine_t *machine) {
    if (!machine->backlight_timeout) {
        uint8_t *seconds = fixed_ram(machine, BACKLIGHT_SECONDS);
        if (*seconds > TIMEOUT_HOLD) *seconds = 1;
    }
}

void machine_set_backlight_timeout(machine_t *machine, bool enabled) {
    machine->backlight_timeout = enabled;
}

void machine_run(machine_t *machine, uint32_t cycles) {
    const uint32_t cycles_per_tick = MACHINE_CLOCK_HZ / TICK_HZ;
    unsigned long end = machine->cpu.cyc + cycles;
    while (machine->cpu.cyc < end) {
        unsigned long before = machine->cpu.cyc;
        uart_update(machine);
        update_interrupt_line(machine);
        if (machine->pc_histogram) machine->pc_histogram[machine->cpu.pc]++;
        if (machine->cpu.pc == machine->watch_pc && machine->watch_hits < 5000) {
            machine->watch_hits++;
            machine_log(machine, "watch %.2fs pc %04x a %02x low page %03x high page %03x bc %02x%02x hl %02x%02x de %02x%02x sp %04x", (double)machine->cpu.cyc / MACHINE_CLOCK_HZ, machine->cpu.pc, machine->cpu.a, machine->low_window_page,
                        machine->high_window_page, machine->cpu.b, machine->cpu.c, machine->cpu.h, machine->cpu.l, machine->cpu.d, machine->cpu.e, machine->cpu.sp);
        }
        z80_step(&machine->cpu);
        uint32_t elapsed = (uint32_t)(machine->cpu.cyc - before);
        machine->cycles_into_tick += elapsed;
        if (machine->cycles_into_tick >= cycles_per_tick) {
            machine->cycles_into_tick -= cycles_per_tick;
            machine->interrupt_status |= INTERRUPT_TICK;
        }
        machine->rtc.cycles_into_second += elapsed;
        if (machine->rtc.cycles_into_second >= MACHINE_CLOCK_HZ) {
            machine->rtc.cycles_into_second -= MACHINE_CLOCK_HZ;
            rtc_advance_second(&machine->rtc);
            machine->interrupt_status |= INTERRUPT_SECOND;
        }
    }
    hold_timeouts(machine);
}

void machine_set_key(machine_t *machine, int column, int row, bool down) {
    if (column < 0 || column >= MACHINE_KEY_COLUMNS || row < 0 || row >= MACHINE_KEY_ROWS) return;
    if (down) {
        machine->keys[column] |= (uint8_t)(1 << row);
        machine->interrupt_status |= INTERRUPT_KEYBOARD;
    } else {
        machine->keys[column] &= (uint8_t)~(1 << row);
    }
}

void machine_set_power_key(machine_t *machine, bool down) {
    if (down && !machine->power_key) machine->interrupt_status |= INTERRUPT_POWER_KEY;
    machine->power_key = down;
}

void machine_release_keys(machine_t *machine) {
    memset(machine->keys, 0, sizeof machine->keys);
    machine->power_key = false;
}

const uint8_t *machine_screen(machine_t *machine) {
    uint8_t *memory = page_pointer(machine, machine->display_page);
    if (memory) memcpy(machine->screen, memory, sizeof machine->screen);
    else memset(machine->screen, 0, sizeof machine->screen);
    return machine->screen;
}

machine_lcd_t machine_lcd(machine_t *machine) {
    machine_lcd_t lcd = { true, 0x20, false };
    if (!machine->lcd_control_written) return lcd;
    lcd.on = (machine->lcd_control & 0x80) && !(machine->lcd_control & 0x40) && (machine->ports[0x20] & 1);
    lcd.contrast = machine->lcd_control & 0x3f;
    lcd.backlight = (machine->lcd_control >> 8) & 1;
    return lcd;
}

bool machine_pop_sound(machine_t *machine, machine_sound_event_t *event) {
    if (machine->sound_tail == machine->sound_head) return false;
    *event = machine->sound_queue[machine->sound_tail];
    machine->sound_tail = (machine->sound_tail + 1) % SOUND_QUEUE_SIZE;
    return true;
}

uint64_t machine_cycles(machine_t *machine) {
    return machine->cpu.cyc;
}

static void capture_state(machine_t *machine, saved_state_t *state) {
    state->low_window_page = machine->low_window_page;
    state->high_window_page = machine->high_window_page;
    state->display_page = machine->display_page;
    state->lcd_control = machine->lcd_control;
    state->lcd_control_written = machine->lcd_control_written;
    state->interrupt_status = machine->interrupt_status;
    state->interrupt_mask = machine->interrupt_mask;
    state->column_select_low = machine->column_select_low;
    state->column_select_high = machine->column_select_high;
    memcpy(state->ports, machine->ports, sizeof state->ports);
    state->cycles_into_tick = machine->cycles_into_tick;
    state->rtc = machine->rtc;
    state->flash_state = machine->flash_state;
}

static void restore_state(machine_t *machine, const saved_state_t *state) {
    machine->low_window_page = state->low_window_page;
    machine->high_window_page = state->high_window_page;
    machine->display_page = state->display_page;
    machine->lcd_control = state->lcd_control;
    machine->lcd_control_written = state->lcd_control_written;
    machine->interrupt_status = state->interrupt_status;
    machine->interrupt_mask = state->interrupt_mask;
    machine->column_select_low = state->column_select_low;
    machine->column_select_high = state->column_select_high;
    memcpy(machine->ports, state->ports, sizeof machine->ports);
    machine->cycles_into_tick = state->cycles_into_tick;
    machine->rtc = state->rtc;
    machine->flash_state = state->flash_state;
    machine->uart.transmit_busy = false;
    machine->uart.receive_ready_at = 0;
    machine->uart.transmit_done_at = 0;
}

bool machine_save(machine_t *machine, const char *path, int64_t host_time) {
    FILE *file = fopen(path, "wb");
    if (!file) return false;
    saved_state_t state;
    memset(&state, 0, sizeof state);
    capture_state(machine, &state);
    uint32_t sizes[3] = { (uint32_t)sizeof(z80), (uint32_t)sizeof state, (uint32_t)RAM_PAGES };
    size_t data_offset = (size_t)FLASH_FIRST_DATA_PAGE * PAGE_SIZE;
    bool ok = fwrite(SNAPSHOT_MAGIC, sizeof SNAPSHOT_MAGIC, 1, file) == 1 &&
              fwrite(sizes, sizeof sizes, 1, file) == 1 &&
              fwrite(&host_time, sizeof host_time, 1, file) == 1 &&
              fwrite(&machine->cpu, sizeof machine->cpu, 1, file) == 1 &&
              fwrite(&state, sizeof state, 1, file) == 1 &&
              fwrite(machine->ram, PAGE_SIZE, RAM_PAGES, file) == RAM_PAGES &&
              fwrite(machine->flash + data_offset, FLASH_SIZE - data_offset, 1, file) == 1;
    ok = fclose(file) == 0 && ok;
    return ok;
}

bool machine_load(machine_t *machine, const char *path, int64_t *host_time) {
    FILE *file = fopen(path, "rb");
    if (!file) return false;
    char magic[sizeof SNAPSHOT_MAGIC];
    uint32_t sizes[3];
    uint32_t expected[3] = { (uint32_t)sizeof(z80), (uint32_t)sizeof(saved_state_t), (uint32_t)RAM_PAGES };
    z80 cpu;
    saved_state_t state;
    size_t data_offset = (size_t)FLASH_FIRST_DATA_PAGE * PAGE_SIZE;
    uint8_t *ram = malloc((size_t)RAM_PAGES * PAGE_SIZE);
    uint8_t *data = malloc(FLASH_SIZE - data_offset);
    bool ok = ram && data &&
              fread(magic, sizeof magic, 1, file) == 1 && memcmp(magic, SNAPSHOT_MAGIC, sizeof magic) == 0 &&
              fread(sizes, sizeof sizes, 1, file) == 1 && memcmp(sizes, expected, sizeof sizes) == 0 &&
              fread(host_time, sizeof *host_time, 1, file) == 1 &&
              fread(&cpu, sizeof cpu, 1, file) == 1 &&
              fread(&state, sizeof state, 1, file) == 1 &&
              fread(ram, PAGE_SIZE, RAM_PAGES, file) == RAM_PAGES &&
              fread(data, FLASH_SIZE - data_offset, 1, file) == 1;
    fclose(file);
    if (ok) {
        machine_reset(machine);
        cpu.read_byte = read_byte;
        cpu.write_byte = write_byte;
        cpu.port_in = port_in;
        cpu.port_out = port_out;
        cpu.userdata = machine;
        machine->cpu = cpu;
        restore_state(machine, &state);
        memcpy(machine->ram, ram, (size_t)RAM_PAGES * PAGE_SIZE);
        memcpy(machine->flash + data_offset, data, FLASH_SIZE - data_offset);
        machine_release_keys(machine);
    }
    free(ram);
    free(data);
    return ok;
}

void machine_advance_clock(machine_t *machine, int64_t seconds) {
    for (int64_t second = 0; second < seconds; second++) rtc_advance_second(&machine->rtc);
}

uint16_t machine_pc(machine_t *machine) {
    return machine->cpu.pc;
}

bool machine_halted(machine_t *machine) {
    return machine->cpu.halted;
}

void machine_set_log(machine_t *machine, machine_log_fn log) {
    machine->log = log;
}

void machine_set_trace_ports(machine_t *machine, bool trace) {
    machine->trace_ports = trace;
}

void machine_set_pc_histogram(machine_t *machine, uint32_t *counts) {
    machine->pc_histogram = counts;
}

bool machine_read_page(machine_t *machine, uint16_t page, uint8_t *out, size_t length) {
    uint8_t *memory = page_pointer(machine, page);
    if (!memory) return false;
    memcpy(out, memory, length < PAGE_SIZE ? length : PAGE_SIZE);
    return true;
}

void machine_set_watch_pc(machine_t *machine, int pc) {
    machine->watch_pc = pc;
    machine->watch_hits = 0;
}

uint8_t machine_peek(machine_t *machine, uint16_t address) {
    return read_byte(machine, address);
}

int machine_free_addin_slot(machine_t *machine) {
    for (int slot = 0; slot < MACHINE_ADDIN_SLOTS; slot++) {
        size_t offset = (size_t)(ADDIN_FIRST_PAGE + slot * ADDIN_SLOT_PAGES) * PAGE_SIZE;
        uint8_t type = machine->flash[offset];
        if (type == 0xff || !(type & ADDIN_ACTIVE_BIT)) return slot;
    }
    return -1;
}

bool machine_write_addin_slot(machine_t *machine, int slot, const uint8_t *data, size_t length) {
    if (slot < 0 || slot >= MACHINE_ADDIN_SLOTS || length > (size_t)ADDIN_SLOT_PAGES * PAGE_SIZE) return false;
    size_t offset = (size_t)(ADDIN_FIRST_PAGE + slot * ADDIN_SLOT_PAGES) * PAGE_SIZE;
    memset(machine->flash + offset, 0xff, (size_t)ADDIN_SLOT_PAGES * PAGE_SIZE);
    memcpy(machine->flash + offset, data, length);
    return true;
}
