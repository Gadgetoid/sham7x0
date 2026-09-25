#pragma once
#include <SDL3/SDL.h>

void browser_set_directory(const char *apps);
void browser_toggle(void);
bool browser_visible(void);
bool browser_focused(void);
void browser_draw(SDL_Renderer *renderer);
void browser_shutdown(void);
