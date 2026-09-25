#pragma once
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
    MENU_RELOAD,
    MENU_INTERRUPT,
    MENU_SHOW_REPL,
    MENU_FOCUS_REPL,
    MENU_BACKLIGHT,
    MENU_DEAD_COLUMNS,
    MENU_SOUND,
    MENU_KEY_CLICK,
    MENU_LAYOUT_NEXT,
    MENU_LAYOUT_FIRST,
    MENU_LAYOUT_END = MENU_LAYOUT_FIRST + 4,
    MENU_AFTER_LAYOUT = MENU_LAYOUT_END - 1,
    MENU_SCRATCHES,
    MENU_WEAR,
    MENU_TOUCHSCREEN,
    MENU_FPS_FIRST,
    MENU_FPS_END = MENU_FPS_FIRST + 6,
    MENU_RESPONSE_FIRST = MENU_FPS_END,
    MENU_RESPONSE_END = MENU_RESPONSE_FIRST + 5,
    MENU_INITIALIZE = MENU_RESPONSE_END,
    MENU_INSTALL_WZD,
    MENU_TEST_MODE,
    MENU_COUNT,
};

static const int MENU_FPS_VALUES[] = { 0, 60, 30, 20, 15, 10 };
static const float MENU_RESPONSE_VALUES[] = { 0.0f, 0.5f, 1.0f, 2.0f, 4.0f };

void menu_install(void);
void menu_ensure(void);
void menu_perform(int item);
int  menu_poll(void);
void menu_set_checked(int item, bool checked);

#ifdef __cplusplus
}
#endif
