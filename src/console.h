#pragma once
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

void  console_write(const char *text, size_t len);
void  console_notice(const char *message);
void  console_submit(const char *line);
char *console_take_input(void);

#ifdef __cplusplus
}

void console_draw(void);
bool console_take_changed(void);
void console_focus(void);
void console_cancel(void);
#endif
