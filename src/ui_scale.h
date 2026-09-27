#pragma once
#include <SDL3/SDL.h>

#include "imgui.h"

inline float ui_scale(SDL_Window *window) {
    float density = SDL_GetWindowPixelDensity(window);
    float display = SDL_GetWindowDisplayScale(window);
    return density > 0 && display > 0 ? display / density : 1.0f;
}

inline void apply_ui_scale(SDL_Window *window, const ImGuiStyle &base) {
    float scale = ui_scale(window);
    ImGuiStyle &style = ImGui::GetStyle();
    style = base;
    style.ScaleAllSizes(scale);
    style.FontScaleDpi = scale;
}
