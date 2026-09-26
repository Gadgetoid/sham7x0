#pragma once

#include <cstdint>

enum { KB_SHAPE_PILL, KB_SHAPE_CURSOR, KB_SHAPE_SQUARE_END };
enum { KB_LIGHT, KB_DARK, KB_BLUE };
enum { KB_GREY, KB_PURPLE, KB_BADGE };
enum { KB_ICON_NONE, KB_ICON_BACKSPACE, KB_ICON_RETURN, KB_ICON_SHIFT, KB_ICON_BOX_DOWN, KB_ICON_TRIANGLE,
       KB_ICON_CASE_TOGGLE, KB_ICON_CHECK };
enum { KB_ACTION_KEY, KB_ACTION_SHIFT, KB_ACTION_SECOND };

struct KeyboardSecondary {
    const char *text;
    int colour;
    float size;
    float dx;
    int icon;
};

struct KeyboardKey {
    const char *id;
    float x, y, w, h;
    int shape;
    int round_side;
    int colour;
    const char *legend;
    int icon;
    float legend_size;
    float legend_dy;
    float stretch;
    uint32_t code;
    uint32_t shift_code;
    uint32_t second_code;
    int action;
    bool letter;
    bool ring;
    bool homing;
    float wear;
    int secondary_count;
    KeyboardSecondary secondary[2];
};

static const float KB_WIDTH = 1000.00f;
static const float KB_HEIGHT = 281.00f;
static const float KB_FRONT_DEPTH = 20.00f;
static const float KB_FRONT_SEAM = 11.00f;
static const float KB_TOP_RADIUS = 16.00f;
static const float KB_BOTTOM_RADIUS = 28.00f;
static const float KB_LIP = 9.00f;
static const float KB_FINGER_X = 498.50f;
static const float KB_FINGER_Y = 280.50f;
static const float KB_FINGER_W = 402.00f;
static const float KB_FINGER_H = 19.00f;
static const float KB_WELL_X = 878.65f;
static const float KB_WELL_Y = 226.00f;
static const float KB_WELL_R = 39.50f;
static const float KB_LABEL_CLEARANCE = 3.20f;
static const float KB_LEGEND_ALPHA = 0.88f;
static const float KB_LEGEND_STRETCH = 1.140f;
static const ImU32 KB_KEYBED = IM_COL32(172, 178, 176, 255);
static const ImU32 KB_FRONT_TOP = IM_COL32(150, 164, 172, 255);
static const ImU32 KB_FRONT_BOTTOM = IM_COL32(108, 122, 132, 255);
static const ImU32 KB_SEAM_DARK = IM_COL32(92, 104, 114, 255);
static const ImU32 KB_SEAM_LIGHT = IM_COL32(176, 188, 196, 255);
static const ImU32 KB_KEY_TOP[3] = { IM_COL32(194, 196, 200, 255), IM_COL32(92, 98, 108, 255), IM_COL32(86, 112, 166, 255) };
static const ImU32 KB_KEY_BOTTOM[3] = { IM_COL32(158, 160, 166, 255), IM_COL32(52, 56, 64, 255), IM_COL32(56, 80, 130, 255) };
static const ImU32 KB_KEY_LEGEND[3] = { IM_COL32(30, 32, 36, 255), IM_COL32(236, 240, 244, 255), IM_COL32(240, 242, 244, 255) };
static const ImU32 KB_SECONDARY[3] = { IM_COL32(58, 60, 62, 255), IM_COL32(96, 84, 156, 255), IM_COL32(104, 90, 170, 255) };
static const ImU32 KB_BADGE_TEXT = IM_COL32(240, 240, 248, 255);
static const uint32_t KB_KEY_CAPS = 0x129;

static const KeyboardKey keyboard_keys[] = {
    { "esc", 82.59f, 34.70f, 75.20f, 35.10f, KB_SHAPE_PILL, 0, KB_DARK, "ESC", KB_ICON_NONE, 18.50f, -1.00f, 1.00f, 0x1b, 0x0, 0x0, KB_ACTION_KEY, false, false, false, 0.50f, 0, { { nullptr, 0, 0.0f, 0.0f, 0 }, { nullptr, 0, 0.0f, 0.0f, 0 } } },
    { "digit_1", 171.43f, 34.70f, 62.90f, 35.10f, KB_SHAPE_PILL, 0, KB_DARK, "1", KB_ICON_NONE, 21.00f, -1.00f, 1.00f, 0x31, 0x21, 0x0, KB_ACTION_KEY, false, false, false, 0.25f, 1, { { "!", KB_GREY, 15.00f, 0.00f, KB_ICON_NONE }, { nullptr, 0, 0.0f, 0.0f, 0 } } },
    { "digit_2", 254.96f, 34.70f, 62.90f, 35.10f, KB_SHAPE_PILL, 0, KB_DARK, "2", KB_ICON_NONE, 21.00f, -1.00f, 1.00f, 0x32, 0x40, 0x0, KB_ACTION_KEY, false, false, false, 0.25f, 1, { { "@", KB_GREY, 15.00f, 0.00f, KB_ICON_NONE }, { nullptr, 0, 0.0f, 0.0f, 0 } } },
    { "digit_3", 338.49f, 34.70f, 62.90f, 35.10f, KB_SHAPE_PILL, 0, KB_DARK, "3", KB_ICON_NONE, 21.00f, -1.00f, 1.00f, 0x33, 0x23, 0x0, KB_ACTION_KEY, false, false, false, 0.25f, 1, { { "#", KB_GREY, 15.00f, 0.00f, KB_ICON_NONE }, { nullptr, 0, 0.0f, 0.0f, 0 } } },
    { "digit_4", 422.02f, 34.70f, 62.90f, 35.10f, KB_SHAPE_PILL, 0, KB_DARK, "4", KB_ICON_NONE, 21.00f, -1.00f, 1.00f, 0x34, 0x24, 0x0, KB_ACTION_KEY, false, false, false, 0.25f, 1, { { "$", KB_GREY, 15.00f, 0.00f, KB_ICON_NONE }, { nullptr, 0, 0.0f, 0.0f, 0 } } },
    { "digit_5", 505.55f, 34.70f, 62.90f, 35.10f, KB_SHAPE_PILL, 0, KB_DARK, "5", KB_ICON_NONE, 21.00f, -1.00f, 1.00f, 0x35, 0x25, 0x0, KB_ACTION_KEY, false, false, false, 0.25f, 1, { { "%", KB_GREY, 15.00f, 0.00f, KB_ICON_NONE }, { nullptr, 0, 0.0f, 0.0f, 0 } } },
    { "digit_6", 589.08f, 34.70f, 62.90f, 35.10f, KB_SHAPE_PILL, 0, KB_DARK, "6", KB_ICON_NONE, 21.00f, -1.00f, 1.00f, 0x36, 0x5e, 0x0, KB_ACTION_KEY, false, false, false, 0.25f, 1, { { "^", KB_GREY, 15.00f, 0.00f, KB_ICON_NONE }, { nullptr, 0, 0.0f, 0.0f, 0 } } },
    { "digit_7", 672.61f, 34.70f, 62.90f, 35.10f, KB_SHAPE_PILL, 0, KB_DARK, "7", KB_ICON_NONE, 21.00f, -1.00f, 1.00f, 0x37, 0x26, 0x0, KB_ACTION_KEY, false, false, false, 0.25f, 1, { { "&", KB_GREY, 15.00f, 0.00f, KB_ICON_NONE }, { nullptr, 0, 0.0f, 0.0f, 0 } } },
    { "digit_8", 756.14f, 34.70f, 62.90f, 35.10f, KB_SHAPE_PILL, 0, KB_DARK, "8", KB_ICON_NONE, 21.00f, -1.00f, 1.00f, 0x38, 0x2a, 0x0, KB_ACTION_KEY, false, false, false, 0.25f, 1, { { "*", KB_GREY, 15.00f, 0.00f, KB_ICON_NONE }, { nullptr, 0, 0.0f, 0.0f, 0 } } },
    { "digit_9", 839.67f, 34.70f, 62.90f, 35.10f, KB_SHAPE_PILL, 0, KB_DARK, "9", KB_ICON_NONE, 21.00f, -1.00f, 1.00f, 0x39, 0x28, 0x0, KB_ACTION_KEY, false, false, false, 0.25f, 1, { { "(", KB_GREY, 15.00f, 0.00f, KB_ICON_NONE }, { nullptr, 0, 0.0f, 0.0f, 0 } } },
    { "digit_0", 923.20f, 34.70f, 62.90f, 35.10f, KB_SHAPE_PILL, 0, KB_DARK, "0", KB_ICON_NONE, 21.00f, -1.00f, 1.00f, 0x30, 0x29, 0x0, KB_ACTION_KEY, false, false, false, 0.25f, 1, { { ")", KB_GREY, 15.00f, 0.00f, KB_ICON_NONE }, { nullptr, 0, 0.0f, 0.0f, 0 } } },
    { "q", 57.42f, 89.30f, 63.50f, 36.60f, KB_SHAPE_PILL, 0, KB_LIGHT, "Q", KB_ICON_NONE, 23.50f, -1.20f, 1.00f, 0x71, 0x51, 0x2b, KB_ACTION_KEY, true, false, false, 0.21f, 1, { { "+", KB_PURPLE, 16.00f, 0.00f, KB_ICON_NONE }, { nullptr, 0, 0.0f, 0.0f, 0 } } },
    { "w", 141.10f, 89.30f, 63.50f, 36.60f, KB_SHAPE_PILL, 0, KB_LIGHT, "W", KB_ICON_NONE, 23.50f, -1.20f, 1.00f, 0x77, 0x57, 0x2d, KB_ACTION_KEY, true, false, false, 0.36f, 1, { { "-", KB_PURPLE, 15.00f, 0.00f, KB_ICON_NONE }, { nullptr, 0, 0.0f, 0.0f, 0 } } },
    { "e", 224.79f, 89.30f, 63.50f, 36.60f, KB_SHAPE_PILL, 0, KB_LIGHT, "E", KB_ICON_NONE, 23.50f, -1.20f, 1.00f, 0x65, 0x45, 0x2a, KB_ACTION_KEY, true, false, false, 1.00f, 1, { { "*", KB_PURPLE, 12.00f, 0.00f, KB_ICON_NONE }, { nullptr, 0, 0.0f, 0.0f, 0 } } },
    { "r", 308.47f, 89.30f, 63.50f, 36.60f, KB_SHAPE_PILL, 0, KB_LIGHT, "R", KB_ICON_NONE, 23.50f, -1.20f, 1.00f, 0x72, 0x52, 0x2f, KB_ACTION_KEY, true, false, false, 0.60f, 1, { { "/", KB_PURPLE, 15.00f, 0.00f, KB_ICON_NONE }, { nullptr, 0, 0.0f, 0.0f, 0 } } },
    { "t", 392.16f, 89.30f, 63.50f, 36.60f, KB_SHAPE_PILL, 0, KB_LIGHT, "T", KB_ICON_NONE, 23.50f, -1.20f, 1.00f, 0x74, 0x54, 0x3d, KB_ACTION_KEY, true, false, false, 0.81f, 1, { { "=", KB_PURPLE, 15.00f, 0.00f, KB_ICON_NONE }, { nullptr, 0, 0.0f, 0.0f, 0 } } },
    { "y", 475.84f, 89.30f, 63.50f, 36.60f, KB_SHAPE_PILL, 0, KB_LIGHT, "Y", KB_ICON_NONE, 23.50f, -1.20f, 1.00f, 0x79, 0x59, 0x0, KB_ACTION_KEY, true, false, false, 0.33f, 0, { { nullptr, 0, 0.0f, 0.0f, 0 }, { nullptr, 0, 0.0f, 0.0f, 0 } } },
    { "u", 559.52f, 89.30f, 63.50f, 36.60f, KB_SHAPE_PILL, 0, KB_LIGHT, "U", KB_ICON_NONE, 23.50f, -1.20f, 1.00f, 0x75, 0x55, 0x0, KB_ACTION_KEY, true, false, false, 0.39f, 0, { { nullptr, 0, 0.0f, 0.0f, 0 }, { nullptr, 0, 0.0f, 0.0f, 0 } } },
    { "i", 643.21f, 89.30f, 63.50f, 36.60f, KB_SHAPE_PILL, 0, KB_LIGHT, "I", KB_ICON_NONE, 23.50f, -1.20f, 1.00f, 0x69, 0x49, 0x3f, KB_ACTION_KEY, true, false, false, 0.67f, 1, { { "?", KB_PURPLE, 13.00f, 0.00f, KB_ICON_NONE }, { nullptr, 0, 0.0f, 0.0f, 0 } } },
    { "o", 726.89f, 89.30f, 63.50f, 36.60f, KB_SHAPE_PILL, 0, KB_LIGHT, "O", KB_ICON_NONE, 23.50f, -1.20f, 1.00f, 0x6f, 0x4f, 0x22, KB_ACTION_KEY, true, false, false, 0.70f, 1, { { "\"", KB_PURPLE, 16.00f, 0.00f, KB_ICON_NONE }, { nullptr, 0, 0.0f, 0.0f, 0 } } },
    { "p", 810.58f, 89.30f, 63.50f, 36.60f, KB_SHAPE_PILL, 0, KB_LIGHT, "P", KB_ICON_NONE, 23.50f, -1.20f, 1.00f, 0x70, 0x50, 0x3b, KB_ACTION_KEY, true, false, false, 0.33f, 1, { { ";", KB_PURPLE, 14.00f, 0.00f, KB_ICON_NONE }, { nullptr, 0, 0.0f, 0.0f, 0 } } },
    { "del", 918.13f, 89.30f, 88.70f, 38.00f, KB_SHAPE_PILL, 0, KB_DARK, "DEL", KB_ICON_BACKSPACE, 13.70f, -1.00f, 1.00f, 0x8, 0x0, 0x0, KB_ACTION_KEY, false, false, false, 0.85f, 0, { { nullptr, 0, 0.0f, 0.0f, 0 }, { nullptr, 0, 0.0f, 0.0f, 0 } } },
    { "a", 98.70f, 138.10f, 63.50f, 36.60f, KB_SHAPE_PILL, 0, KB_LIGHT, "A", KB_ICON_NONE, 23.50f, -1.20f, 1.00f, 0x61, 0x41, 0x0, KB_ACTION_KEY, true, false, false, 0.75f, 0, { { nullptr, 0, 0.0f, 0.0f, 0 }, { nullptr, 0, 0.0f, 0.0f, 0 } } },
    { "s", 182.51f, 138.10f, 63.50f, 36.60f, KB_SHAPE_PILL, 0, KB_LIGHT, "S", KB_ICON_NONE, 23.50f, -1.20f, 1.00f, 0x73, 0x53, 0x122, KB_ACTION_KEY, true, false, false, 0.62f, 1, { { "SEARCH", KB_PURPLE, 9.50f, 0.00f, KB_ICON_NONE }, { nullptr, 0, 0.0f, 0.0f, 0 } } },
    { "d", 266.32f, 138.10f, 63.50f, 36.60f, KB_SHAPE_PILL, 0, KB_LIGHT, "D", KB_ICON_NONE, 23.50f, -1.20f, 1.00f, 0x64, 0x44, 0x0, KB_ACTION_KEY, true, false, false, 0.49f, 0, { { nullptr, 0, 0.0f, 0.0f, 0 }, { nullptr, 0, 0.0f, 0.0f, 0 } } },
    { "f", 350.13f, 138.10f, 63.50f, 36.60f, KB_SHAPE_PILL, 0, KB_LIGHT, "F", KB_ICON_NONE, 23.50f, -1.20f, 1.00f, 0x66, 0x46, 0x123, KB_ACTION_KEY, true, false, true, 0.35f, 1, { { "A⇌A", KB_PURPLE, 9.50f, 0.00f, KB_ICON_CASE_TOGGLE }, { nullptr, 0, 0.0f, 0.0f, 0 } } },
    { "g", 433.95f, 138.10f, 63.50f, 36.60f, KB_SHAPE_PILL, 0, KB_LIGHT, "G", KB_ICON_NONE, 23.50f, -1.20f, 1.00f, 0x67, 0x47, 0x0, KB_ACTION_KEY, true, false, false, 0.33f, 0, { { nullptr, 0, 0.0f, 0.0f, 0 }, { nullptr, 0, 0.0f, 0.0f, 0 } } },
    { "h", 517.76f, 138.10f, 63.50f, 36.60f, KB_SHAPE_PILL, 0, KB_LIGHT, "H", KB_ICON_NONE, 23.50f, -1.20f, 1.00f, 0x68, 0x48, 0x0, KB_ACTION_KEY, true, false, false, 0.61f, 0, { { nullptr, 0, 0.0f, 0.0f, 0 }, { nullptr, 0, 0.0f, 0.0f, 0 } } },
    { "j", 601.57f, 138.10f, 63.50f, 36.60f, KB_SHAPE_PILL, 0, KB_LIGHT, "J", KB_ICON_NONE, 23.50f, -1.20f, 1.00f, 0x6a, 0x4a, 0x60, KB_ACTION_KEY, true, false, true, 0.21f, 1, { { "`", KB_PURPLE, 22.00f, 0.00f, KB_ICON_NONE }, { nullptr, 0, 0.0f, 0.0f, 0 } } },
    { "k", 685.38f, 138.10f, 63.50f, 36.60f, KB_SHAPE_PILL, 0, KB_LIGHT, "K", KB_ICON_NONE, 23.50f, -1.20f, 1.00f, 0x6b, 0x4b, 0xb4, KB_ACTION_KEY, true, false, false, 0.25f, 1, { { "´", KB_PURPLE, 22.00f, 0.00f, KB_ICON_NONE }, { nullptr, 0, 0.0f, 0.0f, 0 } } },
    { "l", 769.19f, 138.10f, 63.50f, 36.60f, KB_SHAPE_PILL, 0, KB_LIGHT, "L", KB_ICON_NONE, 23.50f, -1.20f, 1.00f, 0x6c, 0x4c, 0x7e, KB_ACTION_KEY, true, false, false, 0.47f, 1, { { "~", KB_PURPLE, 17.00f, 0.00f, KB_ICON_NONE }, { nullptr, 0, 0.0f, 0.0f, 0 } } },
    { "enter_wide", 894.23f, 138.10f, 137.70f, 36.90f, KB_SHAPE_PILL, 0, KB_DARK, nullptr, KB_ICON_RETURN, 19.50f, -1.00f, 1.00f, 0xd, 0x0, 0x0, KB_ACTION_KEY, false, false, false, 0.80f, 0, { { nullptr, 0, 0.0f, 0.0f, 0 }, { nullptr, 0, 0.0f, 0.0f, 0 } } },
    { "shift_left", 57.80f, 187.30f, 62.20f, 36.60f, KB_SHAPE_SQUARE_END, 2, KB_DARK, nullptr, KB_ICON_SHIFT, 19.50f, -1.00f, 1.00f, 0x0, 0x0, 0x129, KB_ACTION_SHIFT, false, false, false, 0.35f, 1, { { "CAPS", KB_PURPLE, 9.50f, 0.00f, KB_ICON_NONE }, { nullptr, 0, 0.0f, 0.0f, 0 } } },
    { "z", 139.78f, 187.30f, 63.50f, 36.60f, KB_SHAPE_PILL, 0, KB_LIGHT, "Z", KB_ICON_NONE, 23.50f, -1.20f, 1.00f, 0x7a, 0x5a, 0x0, KB_ACTION_KEY, true, false, false, 0.21f, 0, { { nullptr, 0, 0.0f, 0.0f, 0 }, { nullptr, 0, 0.0f, 0.0f, 0 } } },
    { "x", 223.81f, 187.30f, 63.50f, 36.60f, KB_SHAPE_PILL, 0, KB_LIGHT, "X", KB_ICON_NONE, 23.50f, -1.20f, 1.00f, 0x78, 0x58, 0x124, KB_ACTION_KEY, true, false, false, 0.21f, 1, { { "CUT", KB_PURPLE, 9.50f, 0.00f, KB_ICON_NONE }, { nullptr, 0, 0.0f, 0.0f, 0 } } },
    { "c", 307.84f, 187.30f, 63.50f, 36.60f, KB_SHAPE_PILL, 0, KB_LIGHT, "C", KB_ICON_NONE, 23.50f, -1.20f, 1.00f, 0x63, 0x43, 0x125, KB_ACTION_KEY, true, false, false, 0.39f, 1, { { "COPY", KB_PURPLE, 9.50f, 0.00f, KB_ICON_NONE }, { nullptr, 0, 0.0f, 0.0f, 0 } } },
    { "v", 391.87f, 187.30f, 63.50f, 36.60f, KB_SHAPE_PILL, 0, KB_LIGHT, "V", KB_ICON_NONE, 23.50f, -1.20f, 1.00f, 0x76, 0x56, 0x126, KB_ACTION_KEY, true, false, false, 0.27f, 1, { { "PASTE", KB_PURPLE, 9.50f, 0.00f, KB_ICON_NONE }, { nullptr, 0, 0.0f, 0.0f, 0 } } },
    { "b", 475.90f, 187.30f, 63.50f, 36.60f, KB_SHAPE_PILL, 0, KB_LIGHT, "B", KB_ICON_NONE, 23.50f, -1.20f, 1.00f, 0x62, 0x42, 0xa8, KB_ACTION_KEY, true, false, false, 0.30f, 1, { { "¨", KB_PURPLE, 16.00f, 0.00f, KB_ICON_NONE }, { nullptr, 0, 0.0f, 0.0f, 0 } } },
    { "n", 559.93f, 187.30f, 63.50f, 36.60f, KB_SHAPE_PILL, 0, KB_LIGHT, "N", KB_ICON_NONE, 23.50f, -1.20f, 1.00f, 0x6e, 0x4e, 0x5e, KB_ACTION_KEY, true, false, false, 0.65f, 1, { { "^", KB_PURPLE, 15.00f, 0.00f, KB_ICON_NONE }, { nullptr, 0, 0.0f, 0.0f, 0 } } },
    { "m", 643.96f, 187.30f, 63.50f, 36.60f, KB_SHAPE_PILL, 0, KB_LIGHT, "M", KB_ICON_NONE, 23.50f, -1.20f, 1.00f, 0x6d, 0x4d, 0xe7, KB_ACTION_KEY, true, false, false, 0.36f, 1, { { "ç", KB_PURPLE, 13.00f, 0.00f, KB_ICON_NONE }, { nullptr, 0, 0.0f, 0.0f, 0 } } },
    { "comma", 727.99f, 187.30f, 63.50f, 36.60f, KB_SHAPE_PILL, 0, KB_LIGHT, ",", KB_ICON_NONE, 30.00f, -1.20f, 1.00f, 0x2c, 0x27, 0xdf, KB_ACTION_KEY, false, false, false, 0.35f, 2, { { "ß", KB_PURPLE, 12.00f, -13.50f, KB_ICON_NONE }, { "'", KB_GREY, 14.00f, 10.00f, KB_ICON_NONE } } },
    { "period", 812.02f, 187.30f, 63.50f, 36.60f, KB_SHAPE_PILL, 0, KB_LIGHT, ".", KB_ICON_NONE, 30.00f, -1.20f, 1.00f, 0x2e, 0x3a, 0x20ac, KB_ACTION_KEY, false, false, false, 0.35f, 2, { { "€", KB_PURPLE, 13.00f, -15.50f, KB_ICON_NONE }, { ":", KB_GREY, 13.00f, 10.00f, KB_ICON_NONE } } },
    { "shift_right", 944.80f, 187.30f, 62.20f, 36.60f, KB_SHAPE_SQUARE_END, 0, KB_DARK, nullptr, KB_ICON_SHIFT, 19.50f, -1.00f, 1.00f, 0x0, 0x0, 0x0, KB_ACTION_SHIFT, false, false, false, 0.35f, 0, { { nullptr, 0, 0.0f, 0.0f, 0 }, { nullptr, 0, 0.0f, 0.0f, 0 } } },
    { "fn_2nd", 63.16f, 237.20f, 63.00f, 35.00f, KB_SHAPE_PILL, 0, KB_DARK, "2nd", KB_ICON_NONE, 19.50f, -1.00f, 1.00f, 0x0, 0x0, 0x0, KB_ACTION_SECOND, false, true, false, 0.90f, 0, { { nullptr, 0, 0.0f, 0.0f, 0 }, { nullptr, 0, 0.0f, 0.0f, 0 } } },
    { "menu", 146.92f, 237.20f, 63.00f, 35.00f, KB_SHAPE_PILL, 0, KB_DARK, "MENU", KB_ICON_NONE, 18.50f, -1.00f, 0.95f, 0x9, 0x0, 0x128, KB_ACTION_KEY, false, false, false, 0.85f, 1, { { "PC SYNC", KB_BADGE, 9.60f, 0.00f, KB_ICON_NONE }, { nullptr, 0, 0.0f, 0.0f, 0 } } },
    { "new", 240.80f, 237.20f, 82.60f, 35.00f, KB_SHAPE_PILL, 0, KB_DARK, "NEW", KB_ICON_NONE, 18.50f, -1.00f, 1.00f, 0x120, 0x0, 0x127, KB_ACTION_KEY, false, false, false, 0.35f, 1, { { "EDIT", KB_PURPLE, 9.50f, 0.00f, KB_ICON_NONE }, { nullptr, 0, 0.0f, 0.0f, 0 } } },
    { "smbl", 334.14f, 237.20f, 63.00f, 35.00f, KB_SHAPE_PILL, 0, KB_DARK, nullptr, KB_ICON_BOX_DOWN, 19.50f, -1.00f, 1.00f, 0x12a, 0x0, 0x121, KB_ACTION_KEY, false, false, false, 0.35f, 1, { { "SMBL", KB_PURPLE, 9.50f, 0.00f, KB_ICON_NONE }, { nullptr, 0, 0.0f, 0.0f, 0 } } },
    { "space", 481.14f, 237.20f, 184.40f, 35.00f, KB_SHAPE_PILL, 0, KB_LIGHT, "SPACE", KB_ICON_NONE, 18.50f, -1.20f, 1.00f, 0x20, 0x0, 0xd, KB_ACTION_KEY, false, false, false, 1.00f, 1, { { "✓", KB_PURPLE, 15.00f, 0.00f, KB_ICON_CHECK }, { nullptr, 0, 0.0f, 0.0f, 0 } } },
    { "minus", 627.80f, 237.20f, 63.00f, 35.00f, KB_SHAPE_PILL, 0, KB_DARK, "−", KB_ICON_NONE, 26.00f, -1.00f, 1.00f, 0x2d, 0x5f, 0x0, KB_ACTION_KEY, false, false, false, 0.35f, 1, { { "_", KB_GREY, 15.00f, 0.00f, KB_ICON_NONE }, { nullptr, 0, 0.0f, 0.0f, 0 } } },
    { "enter", 721.65f, 237.20f, 82.60f, 35.00f, KB_SHAPE_PILL, 0, KB_DARK, "ENTER", KB_ICON_NONE, 17.80f, -1.00f, 1.00f, 0xd, 0x0, 0x0, KB_ACTION_KEY, false, false, false, 0.80f, 0, { { nullptr, 0, 0.0f, 0.0f, 0 }, { nullptr, 0, 0.0f, 0.0f, 0 } } },
    { "up", 878.65f, 199.00f, 35.00f, 35.00f, KB_SHAPE_CURSOR, 3, KB_BLUE, nullptr, KB_ICON_TRIANGLE, 0.00f, -1.00f, 1.00f, 0x100, 0x0, 0x0, KB_ACTION_KEY, false, false, false, 0.70f, 0, { { nullptr, 0, 0.0f, 0.0f, 0 }, { nullptr, 0, 0.0f, 0.0f, 0 } } },
    { "left", 832.65f, 245.00f, 35.00f, 35.00f, KB_SHAPE_CURSOR, 2, KB_BLUE, nullptr, KB_ICON_TRIANGLE, 0.00f, -1.00f, 1.00f, 0x102, 0x0, 0x0, KB_ACTION_KEY, false, false, false, 0.60f, 0, { { nullptr, 0, 0.0f, 0.0f, 0 }, { nullptr, 0, 0.0f, 0.0f, 0 } } },
    { "down", 878.65f, 245.00f, 35.00f, 35.00f, KB_SHAPE_CURSOR, 1, KB_BLUE, nullptr, KB_ICON_TRIANGLE, 0.00f, -1.00f, 1.00f, 0x101, 0x0, 0x0, KB_ACTION_KEY, false, false, false, 0.75f, 0, { { nullptr, 0, 0.0f, 0.0f, 0 }, { nullptr, 0, 0.0f, 0.0f, 0 } } },
    { "right", 924.65f, 245.00f, 35.00f, 35.00f, KB_SHAPE_CURSOR, 0, KB_BLUE, nullptr, KB_ICON_TRIANGLE, 0.00f, -1.00f, 1.00f, 0x103, 0x0, 0x0, KB_ACTION_KEY, false, false, false, 0.60f, 0, { { nullptr, 0, 0.0f, 0.0f, 0 }, { nullptr, 0, 0.0f, 0.0f, 0 } } },
};
