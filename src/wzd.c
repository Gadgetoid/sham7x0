#include <stdio.h>
#include <string.h>

#include "wzd.h"

#define SLOT_TYPE_PROGRAM   0x40
#define SLOT_HEADER_SIZE    0x10
#define TITLE_LENGTH        20
#define FILE_NAME_LENGTH    12
#define FIRST_SLOT_ID       0x101

static const uint8_t *find(const uint8_t *data, size_t length, const char *needle) {
    size_t needle_length = strlen(needle);
    for (size_t i = 0; i + needle_length <= length; i++) {
        if (memcmp(data + i, needle, needle_length) == 0) return data + i;
    }
    return NULL;
}

static bool tag_text(const uint8_t *data, size_t length, const char *tag, char *out, size_t out_size) {
    char open[40];
    snprintf(open, sizeof open, "<%s>", tag);
    const uint8_t *start = find(data, length, open);
    if (!start) return false;
    start += strlen(open);
    while (start < data + length && (*start == '\r' || *start == '\n')) start++;
    size_t used = 0;
    while (start < data + length && *start != '\r' && *start != '\n' && *start != '<' && used + 1 < out_size) {
        out[used++] = (char)*start++;
    }
    out[used] = 0;
    return true;
}

bool wzd_parse(const uint8_t *data, size_t length, wzd_program_t *program, char *error, size_t error_size) {
    memset(program, 0, sizeof *program);
    if (!find(data, length, "<SHARP WZD DATA>")) {
        snprintf(error, error_size, "not a .wzd file");
        return false;
    }
    tag_text(data, length, "DATA TYPE", program->data_type, sizeof program->data_type);
    tag_text(data, length, "TITLE", program->title, sizeof program->title);
    char data_line[64] = "";
    tag_text(data, length, "DATA", data_line, sizeof data_line);
    const uint8_t *bin = find(data, length, "<BIN>\r\n");
    if (!bin || strncmp(data_line, "PFILE:", 6) != 0) {
        snprintf(error, error_size, "%s .wzd files are not programs", program->data_type[0] ? program->data_type : "untyped");
        return false;
    }
    snprintf(program->file_name, sizeof program->file_name, "%s", data_line + 6);
    bin += strlen("<BIN>\r\n");
    size_t bin_length = (size_t)(data + length - bin);
    if (bin_length < 2 || (size_t)bin[0] + 1 >= bin_length) {
        snprintf(error, error_size, "program data is too short");
        return false;
    }
    program->icon = bin;
    program->icon_length = (size_t)bin[0] + 1;
    program->program = bin + program->icon_length;
    program->program_length = bin_length - program->icon_length;
    return true;
}

static void put_word(uint8_t *at, size_t value) {
    at[0] = (uint8_t)(value & 0xff);
    at[1] = (uint8_t)(value >> 8);
}

size_t wzd_build_slot(const wzd_program_t *program, int slot_index, uint8_t *slot, char *error, size_t error_size) {
    memset(slot, 0xff, WZD_SLOT_SIZE);
    memset(slot, 0x00, SLOT_HEADER_SIZE);
    size_t cursor = SLOT_HEADER_SIZE;
    memcpy(slot + cursor, program->icon, program->icon_length);
    cursor += program->icon_length;

    size_t title_record = cursor;
    size_t title_length = strlen(program->title);
    if (title_length > TITLE_LENGTH - 1) title_length = TITLE_LENGTH - 1;
    slot[cursor++] = (uint8_t)(title_length + 1);
    memcpy(slot + cursor, program->title, title_length);
    cursor += title_length;
    slot[cursor++] = 0;

    size_t name_record = cursor;
    slot[cursor++] = 0;
    slot[cursor++] = FILE_NAME_LENGTH;
    memset(slot + cursor, 0, FILE_NAME_LENGTH);
    memcpy(slot + cursor, program->file_name, strnlen(program->file_name, FILE_NAME_LENGTH));
    cursor += FILE_NAME_LENGTH;

    size_t program_record = cursor;
    if (program_record + 3 + program->program_length > WZD_SLOT_SIZE) {
        snprintf(error, error_size, "program is %zu bytes, more than a 32KB slot holds", program->program_length);
        return 0;
    }
    slot[cursor++] = 0;
    put_word(slot + cursor, program->program_length);
    cursor += 2;
    memcpy(slot + cursor, program->program, program->program_length);
    cursor += program->program_length;

    slot[0] = SLOT_TYPE_PROGRAM;
    put_word(slot + 0x08, name_record);
    put_word(slot + 0x0a, title_record);
    put_word(slot + 0x0c, program_record);
    put_word(slot + 0x0e, FIRST_SLOT_ID + (size_t)slot_index);
    return cursor;
}
