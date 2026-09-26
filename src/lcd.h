#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define LCD_WIDTH  239
#define LCD_HEIGHT 80
#define LCD_MARGIN_X 3
#define LCD_MARGIN_Y 11

typedef struct {
    uint32_t codepoint;
    uint16_t width;
} lcd_glyph_t;

typedef struct {
    uint16_t cell_width;
    uint16_t height;
    uint32_t count;
    uint16_t bytes_per_row;
    const lcd_glyph_t *glyphs;
    const uint8_t *data;
} lcd_font_t;

extern uint8_t lcd_framebuffer[LCD_WIDTH * LCD_HEIGHT];

void lcd_clear(uint8_t level);
void lcd_set_clip(int x, int y, int w, int h);
void lcd_reset_clip(void);
void lcd_get_clip(int *x, int *y, int *w, int *h);
void lcd_pixel(int x, int y, uint8_t level);
int  lcd_get_pixel(int x, int y);
void lcd_fill(int x, int y, int w, int h, uint8_t level);
void lcd_rect(int x, int y, int w, int h, uint8_t level);
void lcd_line(int x0, int y0, int x1, int y1, uint8_t level);
void lcd_invert(int x, int y, int w, int h);
int  lcd_text(const lcd_font_t *font, const char *text, size_t len, int x, int y, uint8_t level, int scale);
int  lcd_measure(const lcd_font_t *font, const char *text, size_t len, int scale);

void      lcd_compose_setup(int cell);
bool      lcd_compose(float seconds);
bool      lcd_needs_compose(void);
uint32_t *lcd_compose_pixels(void);
int       lcd_compose_width(void);
int       lcd_compose_height(void);
void      lcd_set_backlight(bool on);
bool      lcd_get_backlight(void);
void      lcd_set_power(bool on);
int       lcd_power_ons(void);
void      lcd_set_response(float scale);
void      lcd_set_contrast(int level);
int       lcd_get_contrast(void);
void      lcd_set_dead_columns(bool on);
bool      lcd_get_dead_columns(void);

#ifdef __cplusplus
}
#endif
