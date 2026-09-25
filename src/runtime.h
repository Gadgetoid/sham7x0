#pragma once
#include "host.h"

#ifdef __cplusplus
extern "C" {
#endif

bool        runtime_init(const host_config_t *config);
void        runtime_step(void);
void        runtime_service(void);
void        runtime_request_reload(void);
void        runtime_interrupt(void);
bool        runtime_idle(void);
bool        runtime_repl_busy(void);
int         runtime_boots(void);
const char *runtime_get_resume(void);
void        runtime_set_resume(const char *name);
void        runtime_press_power(void);
void        runtime_initialize_memory(void);
void        runtime_deinit(void);

#ifdef __cplusplus
}
#endif
