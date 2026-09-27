#pragma once
#include "menu.h"

typedef enum {
    MENU_ENTRY_MENU,
    MENU_ENTRY_SUBMENU,
    MENU_ENTRY_END,
    MENU_ENTRY_ITEM,
    MENU_ENTRY_SEPARATOR,
    MENU_ENTRY_FIRMWARE,
    MENU_ENTRY_SERIAL,
} menu_entry_kind_t;

enum {
    MENU_KEY_PRIMARY = 1,
    MENU_KEY_SHIFT = 2,
    MENU_KEY_CONTROL = 4,
};

typedef struct {
    menu_entry_kind_t kind;
    int tag;
    const char *title;
    char key;
    int modifiers;
} menu_entry_t;

static const menu_entry_t MENU_ENTRIES[] = {
    { MENU_ENTRY_MENU, 0, "Run", 0, 0 },
    { MENU_ENTRY_ITEM, MENU_INTERRUPT, "Power", 'c', MENU_KEY_CONTROL },
    { MENU_ENTRY_SEPARATOR, 0, NULL, 0, 0 },
    { MENU_ENTRY_ITEM, MENU_RELOAD, "Reset", 'r', MENU_KEY_PRIMARY },
    { MENU_ENTRY_ITEM, MENU_INITIALIZE, "Initialize Memory", 0, 0 },
    { MENU_ENTRY_ITEM, MENU_TEST_MODE, "Factory Test Mode", 0, 0 },
    { MENU_ENTRY_END, 0, NULL, 0, 0 },

    { MENU_ENTRY_MENU, 0, "Install", 0, 0 },
    { MENU_ENTRY_ITEM, MENU_INSTALL_WZD, "Install .wzd\xe2\x80\xa6", 'i', MENU_KEY_PRIMARY },
    { MENU_ENTRY_ITEM, MENU_APP_BROWSER, "App Browser", 'i', MENU_KEY_PRIMARY | MENU_KEY_SHIFT },
    { MENU_ENTRY_END, 0, NULL, 0, 0 },

    { MENU_ENTRY_MENU, 0, "View", 0, 0 },
    { MENU_ENTRY_ITEM, MENU_LAYOUT_FIRST + 0, "Screen Only", 0, 0 },
    { MENU_ENTRY_ITEM, MENU_LAYOUT_FIRST + 1, "Screen & Frame", 0, 0 },
    { MENU_ENTRY_ITEM, MENU_LAYOUT_FIRST + 2, "Screen & Buttons", 0, 0 },
    { MENU_ENTRY_ITEM, MENU_LAYOUT_FIRST + 3, "Screen & Keyboard", 0, 0 },
    { MENU_ENTRY_ITEM, MENU_LAYOUT_NEXT, "Next Layout", 'k', MENU_KEY_PRIMARY },
    { MENU_ENTRY_SEPARATOR, 0, NULL, 0, 0 },
    { MENU_ENTRY_ITEM, MENU_BORDERLESS, "Borderless", 'b', MENU_KEY_PRIMARY | MENU_KEY_SHIFT },
    { MENU_ENTRY_ITEM, MENU_COMPACT, "Compact", 0, 0 },
#ifdef SHAM_TOUCHSCREEN
    { MENU_ENTRY_ITEM, MENU_TOUCHSCREEN, "Touchscreen Mode", 't', MENU_KEY_PRIMARY | MENU_KEY_SHIFT },
#endif
    { MENU_ENTRY_SEPARATOR, 0, NULL, 0, 0 },
    { MENU_ENTRY_ITEM, MENU_SHOW_CONSOLE, "Show Console", 'j', MENU_KEY_PRIMARY },
    { MENU_ENTRY_ITEM, MENU_FOCUS_CONSOLE, "Focus Console", 'l', MENU_KEY_PRIMARY },
    { MENU_ENTRY_SEPARATOR, 0, NULL, 0, 0 },
    { MENU_ENTRY_SUBMENU, 0, "Realism", 0, 0 },
    { MENU_ENTRY_ITEM, MENU_DEAD_COLUMNS, "Dead Columns", 'd', MENU_KEY_PRIMARY },
    { MENU_ENTRY_ITEM, MENU_SCRATCHES, "Scratches", 0, 0 },
    { MENU_ENTRY_ITEM, MENU_WEAR, "Wear", 0, 0 },
    { MENU_ENTRY_ITEM, MENU_BACKLIGHT_TIMEOUT, "Backlight Timeout", 0, 0 },
    { MENU_ENTRY_END, 0, NULL, 0, 0 },
    { MENU_ENTRY_END, 0, NULL, 0, 0 },

    { MENU_ENTRY_MENU, 0, "Emulation", 0, 0 },
    { MENU_ENTRY_ITEM, MENU_BACKLIGHT, "Backlight", 'b', MENU_KEY_PRIMARY },
    { MENU_ENTRY_ITEM, MENU_SOUND, "Sound", 0, 0 },
    { MENU_ENTRY_FIRMWARE, 0, "Firmware", 0, 0 },
    { MENU_ENTRY_SERIAL, 0, "Serial Port", 0, 0 },
    { MENU_ENTRY_SEPARATOR, 0, NULL, 0, 0 },
    { MENU_ENTRY_SUBMENU, 0, "Frame Rate", 0, 0 },
    { MENU_ENTRY_ITEM, MENU_FPS_FIRST + 0, "Unlimited", 0, 0 },
    { MENU_ENTRY_ITEM, MENU_FPS_FIRST + 1, "60 fps", 0, 0 },
    { MENU_ENTRY_ITEM, MENU_FPS_FIRST + 2, "30 fps", 0, 0 },
    { MENU_ENTRY_ITEM, MENU_FPS_FIRST + 3, "20 fps", 0, 0 },
    { MENU_ENTRY_ITEM, MENU_FPS_FIRST + 4, "15 fps", 0, 0 },
    { MENU_ENTRY_ITEM, MENU_FPS_FIRST + 5, "10 fps", 0, 0 },
    { MENU_ENTRY_END, 0, NULL, 0, 0 },
    { MENU_ENTRY_SUBMENU, 0, "Response Time", 0, 0 },
    { MENU_ENTRY_ITEM, MENU_RESPONSE_FIRST + 0, "Instant", 0, 0 },
    { MENU_ENTRY_ITEM, MENU_RESPONSE_FIRST + 1, "Fast", 0, 0 },
    { MENU_ENTRY_ITEM, MENU_RESPONSE_FIRST + 2, "Normal", 0, 0 },
    { MENU_ENTRY_ITEM, MENU_RESPONSE_FIRST + 3, "Slow", 0, 0 },
    { MENU_ENTRY_ITEM, MENU_RESPONSE_FIRST + 4, "Very Slow", 0, 0 },
    { MENU_ENTRY_END, 0, NULL, 0, 0 },
    { MENU_ENTRY_END, 0, NULL, 0, 0 },
};

static const int MENU_ENTRY_COUNT = sizeof MENU_ENTRIES / sizeof MENU_ENTRIES[0];
