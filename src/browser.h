#pragma once
#include <SDL3/SDL.h>

void browser_set_directory(const char *apps);
void browser_toggle(void);
bool browser_visible(void);
bool browser_focused(void);
void browser_process_event(const SDL_Event *event);
void browser_draw(void);
void browser_shutdown(void);
