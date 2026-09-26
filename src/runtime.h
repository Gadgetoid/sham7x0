#pragma once
#include "host.h"

#ifdef __cplusplus
extern "C" {
#endif

bool        runtime_init(const host_config_t *config);
bool        runtime_switch_firmware(const char *rom_path, int model, const char *state_name);
void        runtime_step(void);
void        runtime_service(void);
void        runtime_request_reload(void);
void        runtime_interrupt(void);
bool        runtime_idle(void);
bool        runtime_console_busy(void);
int         runtime_boots(void);
const char *runtime_get_resume(void);
void        runtime_set_resume(const char *name);
void        runtime_press_power(void);
bool        runtime_keyboard_key(const char *id, bool down);
bool        runtime_keyboard_latched(const char *id);
void        runtime_initialize_memory(void);
void        runtime_enter_test_mode(void);
bool        runtime_install_wzd(const char *path);
bool        runtime_set_serial(const char *target);
const char *runtime_serial_target(void);
bool        runtime_transfer_progress(float *fraction, const char **description, int *waiting);
void        runtime_deinit(void);

#ifdef __cplusplus
}
#endif
