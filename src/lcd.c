#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "lcd.h"

#define GRID_W (LCD_WIDTH + 2 * LCD_MARGIN)
#define GRID_H (LCD_HEIGHT + 2 * LCD_MARGIN)

uint8_t lcd_framebuffer[LCD_WIDTH * LCD_HEIGHT];

static int clip_x0 = 0, clip_y0 = 0, clip_x1 = LCD_WIDTH, clip_y1 = LCD_HEIGHT;

static inline int min_int(int a, int b) { return a < b ? a : b; }
static inline int max_int(int a, int b) { return a > b ? a : b; }

void lcd_clear(uint8_t level) {
    memset(lcd_framebuffer, level & 3, sizeof lcd_framebuffer);
}

void lcd_set_clip(int x, int y, int w, int h) {
    clip_x0 = max_int(0, x);
    clip_y0 = max_int(0, y);
    clip_x1 = min_int(LCD_WIDTH, x + w);
    clip_y1 = min_int(LCD_HEIGHT, y + h);
}

void lcd_reset_clip(void) {
    lcd_set_clip(0, 0, LCD_WIDTH, LCD_HEIGHT);
}

void lcd_get_clip(int *x, int *y, int *w, int *h) {
    *x = clip_x0;
    *y = clip_y0;
    *w = max_int(0, clip_x1 - clip_x0);
    *h = max_int(0, clip_y1 - clip_y0);
}

void lcd_pixel(int x, int y, uint8_t level) {
    if (x < clip_x0 || x >= clip_x1 || y < clip_y0 || y >= clip_y1) return;
    lcd_framebuffer[y * LCD_WIDTH + x] = level & 3;
}

int lcd_get_pixel(int x, int y) {
    if (x < 0 || x >= LCD_WIDTH || y < 0 || y >= LCD_HEIGHT) return 0;
    return lcd_framebuffer[y * LCD_WIDTH + x];
}

void lcd_fill(int x, int y, int w, int h, uint8_t level) {
    int x0 = max_int(x, clip_x0), y0 = max_int(y, clip_y0);
    int x1 = min_int(x + w, clip_x1), y1 = min_int(y + h, clip_y1);
    if (x0 >= x1) return;
    for (int row = y0; row < y1; row++) {
        memset(&lcd_framebuffer[row * LCD_WIDTH + x0], level & 3, (size_t)(x1 - x0));
    }
}

void lcd_rect(int x, int y, int w, int h, uint8_t level) {
    if (w <= 0 || h <= 0) return;
    lcd_fill(x, y, w, 1, level);
    lcd_fill(x, y + h - 1, w, 1, level);
    lcd_fill(x, y, 1, h, level);
    lcd_fill(x + w - 1, y, 1, h, level);
}

void lcd_line(int x0, int y0, int x1, int y1, uint8_t level) {
    int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    int dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int error = dx + dy;
    for (;;) {
        lcd_pixel(x0, y0, level);
        if (x0 == x1 && y0 == y1) break;
        int doubled = 2 * error;
        if (doubled >= dy) { error += dy; x0 += sx; }
        if (doubled <= dx) { error += dx; y0 += sy; }
    }
}

void lcd_invert(int x, int y, int w, int h) {
    int x0 = max_int(x, clip_x0), y0 = max_int(y, clip_y0);
    int x1 = min_int(x + w, clip_x1), y1 = min_int(y + h, clip_y1);
    for (int row = y0; row < y1; row++) {
        uint8_t *pixel = &lcd_framebuffer[row * LCD_WIDTH];
        for (int col = x0; col < x1; col++) pixel[col] = 3 - pixel[col];
    }
}

static uint32_t utf8_next(const char **cursor, const char *end) {
    const uint8_t *p = (const uint8_t *)*cursor;
    uint32_t codepoint = *p++;
    int extra = 0;
    if (codepoint >= 0xf0) { codepoint &= 0x07; extra = 3; }
    else if (codepoint >= 0xe0) { codepoint &= 0x0f; extra = 2; }
    else if (codepoint >= 0xc0) { codepoint &= 0x1f; extra = 1; }
    while (extra-- > 0 && (const char *)p < end) {
        codepoint = (codepoint << 6) | (*p++ & 0x3f);
    }
    *cursor = (const char *)p;
    return codepoint;
}

static const lcd_glyph_t *find_glyph(const lcd_font_t *font, uint32_t codepoint) {
    uint32_t low = 0, high = font->count;
    while (low < high) {
        uint32_t mid = (low + high) / 2;
        uint32_t found = font->glyphs[mid].codepoint;
        if (found == codepoint) return &font->glyphs[mid];
        if (found < codepoint) low = mid + 1;
        else high = mid;
    }
    return NULL;
}

static int space_width(const lcd_font_t *font) {
    return (font->cell_width + 2) / 3;
}

int lcd_text(const lcd_font_t *font, const char *text, size_t len, int x, int y, uint8_t level, int scale) {
    const char *cursor = text, *end = text + len;
    int pen = x;
    while (cursor < end) {
        uint32_t codepoint = utf8_next(&cursor, end);
        if (codepoint == ' ') { pen += space_width(font) * scale; continue; }
        const lcd_glyph_t *glyph = find_glyph(font, codepoint);
        if (!glyph) glyph = find_glyph(font, '?');
        if (!glyph) continue;
        const uint8_t *bits = font->data + (size_t)(glyph - font->glyphs) * font->bytes_per_row * font->height;
        for (int row = 0; row < font->height; row++) {
            const uint8_t *row_bits = bits + row * font->bytes_per_row;
            for (int col = 0; col < glyph->width; col++) {
                if ((row_bits[col >> 3] >> (7 - (col & 7))) & 1) {
                    lcd_fill(pen + col * scale, y + row * scale, scale, scale, level);
                }
            }
        }
        pen += (glyph->width + 1) * scale;
    }
    return pen - x;
}

int lcd_measure(const lcd_font_t *font, const char *text, size_t len, int scale) {
    const char *cursor = text, *end = text + len;
    int width = 0;
    while (cursor < end) {
        uint32_t codepoint = utf8_next(&cursor, end);
        if (codepoint == ' ') { width += space_width(font) * scale; continue; }
        const lcd_glyph_t *glyph = find_glyph(font, codepoint);
        if (!glyph) glyph = find_glyph(font, '?');
        if (glyph) width += (glyph->width + 1) * scale;
    }
    return width;
}

typedef struct { float r, g, b; } colour_t;

typedef struct {
    colour_t glass;
    colour_t ink;
    float contrast;
    float shadow;
    float soft_shadow;
    float bloom;
} panel_t;

static const panel_t panel_lit = {
    .glass = { 56, 182, 151 },
    .ink = { 10, 42, 40 },
    .contrast = 0.92f,
    .shadow = 0.22f,
    .soft_shadow = 0.20f,
    .bloom = 0.30f,
};

static const panel_t panel_unlit = {
    .glass = { 148, 160, 126 },
    .ink = { 30, 38, 32 },
    .contrast = 0.90f,
    .shadow = 0.38f,
    .soft_shadow = 0.30f,
    .bloom = 0.0f,
};

static float shown[LCD_WIDTH * LCD_HEIGHT];
static float ink_grid[GRID_W * GRID_H];
static float glow_grid[GRID_W * GRID_H];
static float glow_scratch[GRID_W * GRID_H];
static float soft_grid[GRID_W * GRID_H];
static uint32_t *output = NULL;
static float *vignette_x = NULL, *vignette_y = NULL;
static float *grain = NULL;
static int cell = 0, output_w = 0, output_h = 0;
static bool force_compose = true;
static bool backlight = true;
static bool powered = true;

static int power_ons = 0;

int lcd_power_ons(void) { return power_ons; }

void lcd_set_power(bool on) {
    if (on && !powered) power_ons++;
    powered = on;
    force_compose = true;
}
static int contrast_level = 5;

void lcd_set_contrast(int level) {
    contrast_level = level < 0 ? 0 : level > 10 ? 10 : level;
    force_compose = true;
}

int lcd_get_contrast(void) { return contrast_level; }

enum { COLUMN_OK, COLUMN_OFF, COLUMN_ON, COLUMN_WEAK };
static uint8_t column_fault[LCD_WIDTH];
static bool dead_columns = false;

static void fault_region(uint8_t fault) {
    int count = 3 + rand() % 4;
    int x = rand() % LCD_WIDTH;
    for (int i = 0; i < count && x < LCD_WIDTH; i++) {
        column_fault[x] = fault;
        if (rand() % 100 < 6 && x + 1 < LCD_WIDTH) column_fault[x + 1] = fault;
        x += 2 + rand() % 4;
    }
}

void lcd_set_dead_columns(bool on) {
    dead_columns = on;
    memset(column_fault, COLUMN_OK, sizeof column_fault);
    if (on) {
        int regions = 1 + rand() % 3;
        for (int i = 0; i < regions; i++) fault_region(rand() % 4 == 0 ? COLUMN_WEAK : COLUMN_OFF);
        if (rand() % 4 == 0) column_fault[rand() % LCD_WIDTH] = COLUMN_ON;
    }
    force_compose = true;
}

bool lcd_get_dead_columns(void) { return dead_columns; }

void lcd_set_backlight(bool on) { backlight = on; force_compose = true; }
bool lcd_get_backlight(void) { return backlight; }
uint32_t *lcd_compose_pixels(void) { return output; }
int lcd_compose_width(void) { return output_w; }
int lcd_compose_height(void) { return output_h; }

#define BEZEL_TOP    0.42f
#define BEZEL_LEFT   0.30f
#define BEZEL_RIGHT  0.10f
#define BEZEL_BOTTOM 0.08f
#define GRAIN_FINE   0.11f
#define GRAIN_COARSE 0.08f

static float hash_noise(int x, int y) {
    uint32_t h = (uint32_t)x * 0x8da6b343u ^ (uint32_t)y * 0xd8163841u;
    h ^= h >> 16;
    h *= 0x85ebca6bu;
    h ^= h >> 13;
    h *= 0xc2b2ae35u;
    h ^= h >> 16;
    return (h & 0xffff) / 65535.0f;
}

static float value_noise(float x, float y, int seed) {
    int ix = (int)floorf(x), iy = (int)floorf(y);
    float tx = x - ix, ty = y - iy;
    tx = tx * tx * (3 - 2 * tx);
    ty = ty * ty * (3 - 2 * ty);
    int ox = ix + seed * 1013, oy = iy + seed * 7919;
    float top = hash_noise(ox, oy) * (1 - tx) + hash_noise(ox + 1, oy) * tx;
    float bottom = hash_noise(ox, oy + 1) * (1 - tx) + hash_noise(ox + 1, oy + 1) * tx;
    return top * (1 - ty) + bottom * ty;
}

void lcd_compose_setup(int new_cell) {
    if (new_cell < 2) new_cell = 2;
    if (new_cell == cell) return;
    cell = new_cell;
    output_w = GRID_W * cell;
    output_h = GRID_H * cell;
    free(output);
    free(vignette_x);
    free(vignette_y);
    free(grain);
    output = malloc((size_t)output_w * output_h * sizeof(uint32_t));
    vignette_x = malloc((size_t)output_w * sizeof(float));
    vignette_y = malloc((size_t)output_h * sizeof(float));
    for (int x = 0; x < output_w; x++) {
        float u = (x + 0.5f) / output_w * 2.0f - 1.0f;
        float from_left = (float)x / cell, from_right = (float)(output_w - 1 - x) / cell;
        vignette_x[x] = (1.0f - 0.07f * u * u * u * u)
                      * (1.0f - BEZEL_LEFT * expf(-from_left / 1.1f))
                      * (1.0f - BEZEL_RIGHT * expf(-from_right / 0.7f));
    }
    for (int y = 0; y < output_h; y++) {
        float v = (y + 0.5f) / output_h * 2.0f - 1.0f;
        float from_top = (float)y / cell, from_bottom = (float)(output_h - 1 - y) / cell;
        vignette_y[y] = (1.0f - 0.10f * v * v)
                      * (1.0f - BEZEL_TOP * expf(-from_top / 1.5f))
                      * (1.0f - BEZEL_BOTTOM * expf(-from_bottom / 0.7f));
    }
    grain = malloc((size_t)output_w * output_h * sizeof(float));
    for (int y = 0; y < output_h; y++) {
        for (int x = 0; x < output_w; x++) {
            float fine = hash_noise(x, y) - 0.5f;
            float mottle = value_noise(x / (output_w * 0.32f), y / (output_w * 0.32f), 11) * 0.65f
                         + value_noise(x / (output_w * 0.13f), y / (output_w * 0.13f), 23) * 0.35f;
            grain[(size_t)y * output_w + x] = 1.0f + fine * GRAIN_FINE + (mottle - 0.5f) * GRAIN_COARSE;
        }
    }
    force_compose = true;
}

static void box_blur(float *grid, float *scratch, int radius) {
    float norm = 1.0f / (2 * radius + 1);
    for (int y = 0; y < GRID_H; y++) {
        for (int x = 0; x < GRID_W; x++) {
            float sum = 0;
            for (int k = -radius; k <= radius; k++) {
                int sx = min_int(GRID_W - 1, max_int(0, x + k));
                sum += grid[y * GRID_W + sx];
            }
            scratch[y * GRID_W + x] = sum * norm;
        }
    }
    for (int y = 0; y < GRID_H; y++) {
        for (int x = 0; x < GRID_W; x++) {
            float sum = 0;
            for (int k = -radius; k <= radius; k++) {
                int sy = min_int(GRID_H - 1, max_int(0, y + k));
                sum += scratch[sy * GRID_W + x];
            }
            grid[y * GRID_W + x] = sum * norm;
        }
    }
}

static float response_scale = 1.0f;

void lcd_set_response(float scale) {
    response_scale = scale;
}

static bool settle_pixels(float seconds) {
    float darken = response_scale > 0 ? 1.0f - expf(-seconds / (0.0209f * response_scale)) : 1.0f;
    float lighten = response_scale > 0 ? 1.0f - expf(-seconds / (0.0387f * response_scale)) : 1.0f;
    bool changed = false;
    for (int i = 0; i < LCD_WIDTH * LCD_HEIGHT; i++) {
        float target = powered ? lcd_framebuffer[i] / 3.0f : 0.0f;
        if (powered) switch (column_fault[i % LCD_WIDTH]) {
            case COLUMN_OFF:  target = 0.0f; break;
            case COLUMN_ON:   target = 1.0f; break;
            case COLUMN_WEAK: target *= 0.35f; break;
            default: break;
        }
        float delta = target - shown[i];
        if (delta == 0.0f) continue;
        changed = true;
        if (fabsf(delta) < 0.01f) shown[i] = target;
        else shown[i] += delta * (delta > 0 ? darken : lighten);
    }
    return changed;
}

static inline uint8_t to_byte(float value) {
    if (value <= 0.0f) return 0;
    if (value >= 255.0f) return 255;
    return (uint8_t)(value + 0.5f);
}

static inline bool is_electrode(int sub_x, int sub_y, int gap) {
    return sub_x < cell - gap && sub_y < cell - gap;
}

bool lcd_compose(float seconds) {
    if (!output) return false;
    bool changed = settle_pixels(seconds) || force_compose;
    force_compose = false;
    if (!changed) return false;

    const panel_t *panel = backlight && powered ? &panel_lit : &panel_unlit;
    int gap = cell >= 4 ? max_int(1, cell / 7) : 1;
    float gain = (0.55f + contrast_level * 0.06f) / 0.85f;
    float off_bias = contrast_level > 6 ? (contrast_level - 6) * 0.035f : 0.0f;
    int shadow_offset = max_int(1, cell / 3);
    float soft_offset = 0.75f;

    memset(ink_grid, 0, sizeof ink_grid);
    for (int y = 0; y < LCD_HEIGHT; y++) {
        for (int x = 0; x < LCD_WIDTH; x++) {
            ink_grid[(y + LCD_MARGIN) * GRID_W + x + LCD_MARGIN] = shown[y * LCD_WIDTH + x];
        }
    }

    if (panel->bloom > 0) {
        for (int i = 0; i < GRID_W * GRID_H; i++) glow_grid[i] = 1.0f - fminf(1.0f, ink_grid[i] * panel->contrast * gain);
        box_blur(glow_grid, glow_scratch, 2);
        box_blur(glow_grid, glow_scratch, 2);
    }
    memcpy(soft_grid, ink_grid, sizeof soft_grid);
    box_blur(soft_grid, glow_scratch, 1);

    for (int y = 0; y < output_h; y++) {
        int grid_y = y / cell, sub_y = y % cell;
        int shadow_y = y - shadow_offset;
        int shadow_grid_y = shadow_y >= 0 ? shadow_y / cell : -1;
        int shadow_sub_y = shadow_y >= 0 ? shadow_y % cell : 0;
        bool row_in_panel = grid_y >= LCD_MARGIN && grid_y < LCD_MARGIN + LCD_HEIGHT;

        float soft_v = (y + 0.5f) / cell - 0.5f - soft_offset;
        int soft_y0 = max_int(0, min_int(GRID_H - 1, (int)floorf(soft_v)));
        int soft_y1 = min_int(GRID_H - 1, soft_y0 + 1);
        float soft_fy = fminf(1.0f, fmaxf(0.0f, soft_v - soft_y0));

        float glow_v = (y + 0.5f) / cell - 0.5f;
        int glow_y0 = max_int(0, min_int(GRID_H - 1, (int)floorf(glow_v)));
        int glow_y1 = min_int(GRID_H - 1, glow_y0 + 1);
        float glow_fy = fminf(1.0f, fmaxf(0.0f, glow_v - glow_y0));

        uint32_t *out_row = &output[(size_t)y * output_w];
        for (int x = 0; x < output_w; x++) {
            int grid_x = x / cell, sub_x = x % cell;
            bool in_panel = row_in_panel && grid_x >= LCD_MARGIN && grid_x < LCD_MARGIN + LCD_WIDTH;
            bool electrode = in_panel && is_electrode(sub_x, sub_y, gap);
            float ink = electrode ? ink_grid[grid_y * GRID_W + grid_x] : 0.0f;

            float shadow = 0.0f;
            int shadow_x = x - shadow_offset;
            if (shadow_x >= 0 && shadow_grid_y >= 0 && is_electrode(shadow_x % cell, shadow_sub_y, gap)) {
                shadow = ink_grid[shadow_grid_y * GRID_W + shadow_x / cell];
            }

            float light = vignette_x[x] * vignette_y[y] * grain[(size_t)y * output_w + x];
            if (electrode) light *= 0.965f;
            light *= 1.0f - shadow * panel->shadow;

            float soft_u = (x + 0.5f) / cell - 0.5f - soft_offset;
            int soft_x0 = max_int(0, min_int(GRID_W - 1, (int)floorf(soft_u)));
            int soft_x1 = min_int(GRID_W - 1, soft_x0 + 1);
            float soft_fx = fminf(1.0f, fmaxf(0.0f, soft_u - soft_x0));
            float soft_top = soft_grid[soft_y0 * GRID_W + soft_x0] * (1 - soft_fx) + soft_grid[soft_y0 * GRID_W + soft_x1] * soft_fx;
            float soft_bottom = soft_grid[soft_y1 * GRID_W + soft_x0] * (1 - soft_fx) + soft_grid[soft_y1 * GRID_W + soft_x1] * soft_fx;
            light *= 1.0f - (soft_top * (1 - soft_fy) + soft_bottom * soft_fy) * panel->soft_shadow;

            float coverage = fminf(1.0f, ink * panel->contrast * gain + (electrode ? off_bias : 0.0f));
            float r = panel->glass.r * light * (1.0f - coverage) + panel->ink.r * coverage;
            float g = panel->glass.g * light * (1.0f - coverage) + panel->ink.g * coverage;
            float b = panel->glass.b * light * (1.0f - coverage) + panel->ink.b * coverage;

            if (panel->bloom > 0) {
                float glow_u = (x + 0.5f) / cell - 0.5f;
                int glow_x0 = max_int(0, min_int(GRID_W - 1, (int)floorf(glow_u)));
                int glow_x1 = min_int(GRID_W - 1, glow_x0 + 1);
                float glow_fx = fminf(1.0f, fmaxf(0.0f, glow_u - glow_x0));
                float top = glow_grid[glow_y0 * GRID_W + glow_x0] * (1 - glow_fx) + glow_grid[glow_y0 * GRID_W + glow_x1] * glow_fx;
                float bottom = glow_grid[glow_y1 * GRID_W + glow_x0] * (1 - glow_fx) + glow_grid[glow_y1 * GRID_W + glow_x1] * glow_fx;
                float glow = (top * (1 - glow_fy) + bottom * glow_fy) * panel->bloom * (0.35f + coverage);
                r += panel->glass.r * glow * 0.5f;
                g += panel->glass.g * glow * 0.5f;
                b += panel->glass.b * glow * 0.5f;
            }

            out_row[x] = (uint32_t)to_byte(r) | (uint32_t)to_byte(g) << 8 | (uint32_t)to_byte(b) << 16 | 0xff000000u;
        }
    }
    return true;
}
