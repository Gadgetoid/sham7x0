#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define WZD_SLOT_SIZE 0x8000

typedef struct {
    char     data_type[32];
    char     title[64];
    char     file_name[16];
    const uint8_t *icon;
    size_t   icon_length;
    const uint8_t *program;
    size_t   program_length;
} wzd_program_t;

bool   wzd_parse(const uint8_t *data, size_t length, wzd_program_t *program, char *error, size_t error_size);
size_t wzd_build_slot(const wzd_program_t *program, int slot_index, uint8_t *slot, char *error, size_t error_size);

#ifdef __cplusplus
}
#endif
