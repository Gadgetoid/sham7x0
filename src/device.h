#pragma once
#include <SDL3/SDL.h>

struct ImFont;
struct ImVec2;

struct DeviceState {
    bool show_keys = true;
    bool focused = true;
    bool powered = true;
    bool scratches = true;
    bool wear = false;
    bool show_keyboard = true;
    bool screen_only = false;
    bool second = false;
    bool shift = false;
    bool caps = false;
    bool touch = false;
    bool select = false;
    bool borderless = false;
};

const int DEVICE_MIN_CELL = 2;

float  device_draw(SDL_Renderer *renderer, float framebuffer_scale, float height, float compose_seconds, DeviceState &state);
int    device_fit_cell(ImVec2 content, float framebuffer_scale, const DeviceState &state);
ImVec2 device_content_size(int cell, float framebuffer_scale, const DeviceState &state);
bool   device_draggable(float x, float y);
void   device_shutdown(void);
void   device_flush_bake(SDL_Renderer *renderer);
void   device_set_label_font(ImFont *font);
void   device_set_model(const char *model);
void   device_set_icon_font(ImFont *font);
void   device_set_keyboard_fonts(ImFont *legend, ImFont *label);
