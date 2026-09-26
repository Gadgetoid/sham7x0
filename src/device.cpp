#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#define IMGUI_DEFINE_MATH_OPERATORS
#include "imgui.h"
#include "imgui_internal.h"
#include "imgui_impl_sdlrenderer3.h"

#include "device.h"
#include "keyboard_layout.h"
#include "key_shapes.h"
#include "keys.h"
#include "runtime.h"
#include "lcd.h"

namespace {

using Shape = std::vector<ImVec2>;

const float GRID_W = LCD_WIDTH + 2 * LCD_MARGIN;
const float GRID_H = LCD_HEIGHT + 2 * LCD_MARGIN;
const float REFERENCE_LCD_H = 282.0f;
const float LEFT_EXTENT = 202.0f;
const float RIGHT_EXTENT = 214.0f;
const float TOP_EXTENT = 91.0f;
const float BOTTOM_EXTENT = 48.0f;
const float PLAIN_BEZEL = 30.0f;
const float SCREEN_MARGIN = 8.0f;
const ImU32 SCRATCH_TINT = IM_COL32(214, 232, 224, 120);
const ImU32 CASE_SCRATCH_TINT = IM_COL32(246, 249, 251, 150);
const float FLUTE_MARGIN = 3.5f;
const float WELL_MARGIN = 4.0f;
const float ARROW_WELL_MARGIN = 4.0f;
const ImVec2 ARROW_CENTRE(181.1f, 113.5f);
const float ARROW_R = 80.5f;
const float ARROW_GAP_Y = 113.5f;
const float ARROW_HALF_GAP = 6.8f;
const float ARROW_EDGE_X = 179.2f;
const float ARROW_EDGE_R = 560.0f;
const float ARROW_CORNER = 11.0f;
const float ARROW_GAP_CORNER = 6.0f;
const float ARROW_WELL_TUCK = 3.0f;
const float ARROW_WELL_FLAT = -91.06f;
const float ARROW_KEY_FLAT = -86.06f;
const float ARROW_SOFTNESS = 6.0f;
const float ARROW_WELL_CORNER = 12.0f;
const float FLUTE_REACH = 0.45f;
const float SIDE_KEY_CORNER = 6.0f;
const uint64_t REPEAT_DELAY_MS = 400;
const uint64_t REPEAT_RATE_MS = 80;

const ImU32 BEZEL = IM_COL32(182, 190, 194, 255);
const ImU32 BEZEL_EDGE = IM_COL32(112, 120, 126, 255);
const ImU32 BEZEL_LIGHT = IM_COL32(226, 232, 236, 255);
const ImU32 FRAME = IM_COL32(196, 204, 208, 255);
const ImU32 LABEL = IM_COL32(236, 240, 244, 255);
const ImU32 PRINT = IM_COL32(52, 58, 64, 255);
const ImU32 ICON_BLUE = IM_COL32(96, 172, 226, 255);

struct ButtonStyle {
    ImU32 top;
    ImU32 bottom;
    int rim = 42;
    float gap = 1.6f;
    float shadow = 1.0f;
    float dome = 0.0f;
};

const ButtonStyle DARK_KEY = { IM_COL32(70, 80, 92, 255), IM_COL32(30, 37, 46, 255) };
const ButtonStyle DARK_DOMED_KEY = { IM_COL32(70, 80, 92, 255), IM_COL32(30, 37, 46, 255), 42, 1.6f, 1.0f, 1.0f };
const ButtonStyle BLUE_KEY = { IM_COL32(82, 126, 186, 255), IM_COL32(42, 80, 132, 255) };
const ButtonStyle TEAL_KEY = { IM_COL32(68, 150, 140, 255), IM_COL32(30, 102, 96, 255) };

const unsigned ICON_CALL = 0xe0b0;
const unsigned ICON_CALENDAR = 0xebcc;
const unsigned ICON_NOTE = 0xf1fc;
const unsigned ICON_LIGHT = 0xe518;
const unsigned ICON_POWER = 0xe8ac;

SDL_Texture *lcd_texture = nullptr;
SDL_Texture *grime_texture = nullptr;
SDL_Texture *scratch_texture = nullptr;
bool scratch_tried = false;
int scratch_w = 0, scratch_h = 0;
int grime_w = 0, grime_h = 0;
bool grime_wear = false;
bool wear_labels = false;
ImFont *label_font = nullptr;
std::string model_name = "OZ-750";
ImFont *kb_legend_font = nullptr;
ImFont *kb_label_font = nullptr;
ImFont *icon_font = nullptr;

struct KeyRepeat {
    bool active = false;
    uint64_t since = 0;
    uint64_t last = 0;
};

KeyRepeat repeats[2];

struct Frame {
    ImVec2 lcd_min;
    ImVec2 lcd_max;
    float u;

    ImVec2 at(float x, float y, bool right) const {
        return ImVec2((right ? lcd_max.x : lcd_min.x) + x * u, lcd_min.y + y * u);
    }
};

ImFont *text_font() {
    return label_font ? label_font : ImGui::GetFont();
}

uint32_t hash2(int x, int y, uint32_t seed) {
    uint32_t h = (uint32_t)x * 0x8da6b343u ^ (uint32_t)y * 0xd8163841u ^ seed * 0xcb1ab31fu;
    h ^= h >> 16;
    h *= 0x85ebca6bu;
    h ^= h >> 13;
    h *= 0xc2b2ae35u;
    h ^= h >> 16;
    return h;
}

float unit_noise(int x, int y, uint32_t seed) {
    return (hash2(x, y, seed) & 0xffff) / 65535.0f;
}

float smooth_noise(float x, float y, uint32_t seed) {
    int ix = (int)floorf(x), iy = (int)floorf(y);
    float fx = x - ix, fy = y - iy;
    fx = fx * fx * (3 - 2 * fx);
    fy = fy * fy * (3 - 2 * fy);
    float top = unit_noise(ix, iy, seed) * (1 - fx) + unit_noise(ix + 1, iy, seed) * fx;
    float bottom = unit_noise(ix, iy + 1, seed) * (1 - fx) + unit_noise(ix + 1, iy + 1, seed) * fx;
    return top * (1 - fy) + bottom * fy;
}

void build_grime(SDL_Renderer *renderer, int w, int h, float scale, bool wear) {
    if (grime_texture && grime_w == w && grime_h == h && grime_wear == wear) return;
    if (grime_texture) SDL_DestroyTexture(grime_texture);
    grime_w = w;
    grime_h = h;
    grime_wear = wear;
    float grain_strength = wear ? 0.13f : 0.10f;
    float dirt_threshold = 0.62f;
    float dirt_strength = wear ? 0.0f : 0.10f;
    uint32_t speck_mask = wear ? 0x7ff : 0x1fff;
    std::vector<uint32_t> pixels((size_t)w * h);
    float smudge = 90.0f * scale;
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            float grain = unit_noise(x, y, 1) - 0.5f;
            float blotch = smooth_noise(x / smudge, y / smudge, 2) * 0.6f + smooth_noise(x / (smudge * 0.35f), y / (smudge * 0.35f), 3) * 0.4f;
            float dirt = std::max(0.0f, blotch - dirt_threshold) * 2.2f;
            float speck = (hash2(x, y, 4) & speck_mask) == 0 ? 0.55f : 0.0f;
            float shade = grain * grain_strength - dirt * dirt_strength - speck;
            uint8_t value = shade >= 0 ? 255 : 0;
            uint8_t alpha = (uint8_t)std::min(255.0f, fabsf(shade) * 255.0f);
            pixels[(size_t)y * w + x] = (uint32_t)value | (uint32_t)value << 8 | (uint32_t)value << 16 | (uint32_t)alpha << 24;
        }
    }
    grime_texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, w, h);
    SDL_SetTextureBlendMode(grime_texture, SDL_BLENDMODE_BLEND);
    SDL_UpdateTexture(grime_texture, nullptr, pixels.data(), w * 4);
}

void load_scratches(SDL_Renderer *renderer) {
    if (scratch_tried) return;
    scratch_tried = true;
    std::string path = std::string(SDL_GetBasePath() ? SDL_GetBasePath() : "") + "assets/lcd_scratches.bin";
    FILE *file = fopen(path.c_str(), "rb");
    if (!file) return;
    uint16_t size[2];
    if (fread(size, sizeof size, 1, file) == 1) {
        std::vector<uint8_t> alpha((size_t)size[0] * size[1]);
        if (fread(alpha.data(), 1, alpha.size(), file) == alpha.size()) {
            std::vector<uint32_t> pixels(alpha.size());
            for (size_t i = 0; i < alpha.size(); i++) pixels[i] = 0x00ffffffu | (uint32_t)alpha[i] << 24;
            scratch_w = size[0];
            scratch_h = size[1];
            scratch_texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, scratch_w, scratch_h);
            SDL_SetTextureBlendMode(scratch_texture, SDL_BLENDMODE_BLEND);
            SDL_UpdateTexture(scratch_texture, nullptr, pixels.data(), scratch_w * 4);
        }
    }
    fclose(file);
}

void case_scratches(ImDrawList *draw, ImVec2 a, ImVec2 b, float rounding, float offset) {
    if (!scratch_texture) return;
    ImVec2 uv0(offset, offset * 0.5f), uv1(offset + 0.55f, offset * 0.5f + 0.55f * (b.y - a.y) / (b.x - a.x) * scratch_w / scratch_h);
    draw->AddImageRounded((ImTextureID)(intptr_t)scratch_texture, a, b, uv0, uv1, CASE_SCRATCH_TINT, rounding);
}

void upload_lcd(SDL_Renderer *renderer, float compose_seconds) {
    int w = lcd_compose_width(), h = lcd_compose_height();
    bool recreated = false;
    if (!lcd_texture || lcd_texture->w != w || lcd_texture->h != h) {
        if (lcd_texture) SDL_DestroyTexture(lcd_texture);
        lcd_texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STREAMING, w, h);
        SDL_SetTextureScaleMode(lcd_texture, SDL_SCALEMODE_NEAREST);
        recreated = true;
    }
    bool composed = compose_seconds > 0 && lcd_compose(compose_seconds);
    if (composed || recreated) SDL_UpdateTexture(lcd_texture, nullptr, lcd_compose_pixels(), w * 4);
}

ImVec2 text_size(ImFont *font, float size, const char *text) {
    return font->CalcTextSizeA(size, FLT_MAX, 0.0f, text);
}

ImU32 erase_colour = 0;
bool rub_mode = false;
float rub_amount = 0.45f;

bool wear_grime = false;


void rub_patch(ImDrawList *draw, ImVec2 a, ImVec2 b, ImU32 face, float amount, uint32_t seed) {
    if (!wear_labels || amount <= 0) return;
    ImVec2 size = b - a;
    ImVec2 centre = (a + b) * 0.5f;
    centre.x += ((hash2((int)seed, 3, 5) & 0xffff) / 65535.0f - 0.5f) * 0.35f * size.x;
    centre.y += ((hash2((int)seed, 5, 7) & 0xffff) / 65535.0f - 0.2f) * 0.3f * size.y;
    ImVec2 radius(size.x * 0.5f, size.y * 0.62f);
    ImU32 colour = face & ~IM_COL32_A_MASK;
    const int layers = 9;
    for (int layer = 0; layer < layers; layer++) {
        float t = (float)layer / layers;
        int alpha = (int)(std::min(1.0f, amount) * 30.0f);
        draw->AddEllipseFilled(centre, radius * (1.0f - t * 0.82f), colour | ((ImU32)alpha << IM_COL32_A_SHIFT), 0, 40);
    }
}

void wear_patch(ImDrawList *draw, ImVec2 a, ImVec2 b, uint32_t seed) {
    if (rub_mode) {
        rub_patch(draw, a, b, erase_colour, rub_amount, seed);
        return;
    }
    if (!wear_labels || !scratch_texture || !erase_colour) return;
    float span = std::min(0.5f, (b.x - a.x) / 900.0f + 0.05f);
    float aspect = (b.y - a.y) / std::max(1.0f, b.x - a.x) * scratch_w / (float)scratch_h;
    float span_v = std::min(0.9f, span * aspect);
    float u0 = (hash2((int)seed, 7, 11) & 0xffff) / 65535.0f * (1 - span);
    float v0 = (hash2((int)seed, 9, 13) & 0xffff) / 65535.0f * (1 - span_v);
    draw->AddImage((ImTextureID)(intptr_t)scratch_texture, a, b, ImVec2(u0, v0), ImVec2(u0 + span, v0 + span_v), erase_colour);
}

uint32_t text_seed(const char *text, ImVec2 at) {
    uint32_t seed = (uint32_t)(at.x * 7 + at.y * 13);
    for (const char *c = text; *c; c++) seed = seed * 31 + (uint8_t)*c;
    return seed;
}

void centred_text(ImDrawList *draw, ImVec2 centre, float size, ImU32 colour, const char *text) {
    ImVec2 extent = text_size(text_font(), size, text);
    draw->AddText(text_font(), size, centre - extent * 0.5f, colour, text);
    wear_patch(draw, centre - extent * 0.5f, centre + extent * 0.5f, text_seed(text, centre));
}

void icon(ImDrawList *draw, ImVec2 centre, float size, ImU32 colour, unsigned codepoint) {
    char utf8[4] = { (char)(0xe0 | (codepoint >> 12)), (char)(0x80 | ((codepoint >> 6) & 0x3f)),
                     (char)(0x80 | (codepoint & 0x3f)), 0 };
    ImFont *font = icon_font ? icon_font : ImGui::GetFont();
    ImVec2 extent = text_size(font, size, utf8);
    draw->AddText(font, size, centre - extent * 0.5f, colour, utf8);
    wear_patch(draw, centre - extent * 0.5f, centre + extent * 0.5f, codepoint * 2654435761u);
}

void add_arc(Shape &shape, ImVec2 centre, float radius, float from, float to, int segments) {
    for (int i = 0; i <= segments; i++) {
        float angle = from + (to - from) * i / segments;
        shape.push_back(centre + ImVec2(cosf(angle), sinf(angle)) * radius);
    }
}

Shape translated(const Shape &shape, ImVec2 offset) {
    Shape result = shape;
    for (ImVec2 &point : result) point += offset;
    return result;
}

Shape circle(ImVec2 centre, float radius) {
    Shape shape;
    add_arc(shape, centre, radius, 0, 2 * IM_PI, 48);
    shape.pop_back();
    return shape;
}

Shape pill(ImVec2 a, ImVec2 b) {
    float r = (b.y - a.y) * 0.5f;
    Shape shape;
    add_arc(shape, ImVec2(a.x + r, a.y + r), r, IM_PI * 0.5f, IM_PI * 1.5f, 40);
    add_arc(shape, ImVec2(b.x - r, a.y + r), r, IM_PI * 1.5f, IM_PI * 2.5f, 40);
    return shape;
}

Shape side_key(ImVec2 a, ImVec2 b, float corner) {
    float r = (b.y - a.y) * 0.5f;
    Shape shape;
    add_arc(shape, ImVec2(a.x + r, a.y + r), r, IM_PI * 0.5f, IM_PI * 1.5f, 32);
    add_arc(shape, ImVec2(b.x - corner, a.y + corner), corner, IM_PI * 1.5f, IM_PI * 2.0f, 10);
    add_arc(shape, ImVec2(b.x - corner, b.y - corner), corner, 0, IM_PI * 0.5f, 10);
    return shape;
}

Shape capsule(ImVec2 from, float from_r, ImVec2 to, float to_r) {
    ImVec2 axis = to - from;
    float distance = sqrtf(axis.x * axis.x + axis.y * axis.y);
    float theta = atan2f(axis.y, axis.x);
    float phi = asinf((from_r - to_r) / distance);
    float alpha = theta + IM_PI * 0.5f - phi;
    float beta = theta - IM_PI * 0.5f + phi;
    Shape shape;
    add_arc(shape, from, from_r, alpha, beta + 2 * IM_PI, 48);
    add_arc(shape, to, to_r, beta + 2 * IM_PI, alpha + 2 * IM_PI, 48);
    return shape;
}

Shape smooth(const Shape &shape, int passes) {
    Shape result = shape;
    for (int pass = 0; pass < passes; pass++) {
        Shape next;
        size_t count = result.size();
        for (size_t i = 0; i < count; i++) {
            ImVec2 a = result[i], b = result[(i + 1) % count];
            next.push_back(a * 0.75f + b * 0.25f);
            next.push_back(a * 0.25f + b * 0.75f);
        }
        result = next;
    }
    return result;
}

float cross(ImVec2 o, ImVec2 a, ImVec2 b) {
    return (a.x - o.x) * (b.y - o.y) - (a.y - o.y) * (b.x - o.x);
}

Shape hull(Shape points) {
    std::sort(points.begin(), points.end(), [](ImVec2 a, ImVec2 b) { return a.x < b.x || (a.x == b.x && a.y < b.y); });
    Shape result(points.size() * 2);
    size_t k = 0;
    for (size_t i = 0; i < points.size(); i++) {
        while (k >= 2 && cross(result[k - 2], result[k - 1], points[i]) <= 0) k--;
        result[k++] = points[i];
    }
    for (size_t i = points.size() - 1, t = k + 1; i > 0; i--) {
        while (k >= t && cross(result[k - 2], result[k - 1], points[i - 1]) <= 0) k--;
        result[k++] = points[i - 1];
    }
    result.resize(k - 1);
    return result;
}

Shape grown(const Shape &shape, float margin) {
    Shape result;
    size_t count = shape.size();
    for (size_t i = 0; i < count; i++) {
        ImVec2 before = shape[(i + count - 1) % count], after = shape[(i + 1) % count];
        ImVec2 tangent = after - before;
        float length = sqrtf(tangent.x * tangent.x + tangent.y * tangent.y);
        if (length == 0) continue;
        ImVec2 normal(tangent.y / length, -tangent.x / length);
        result.push_back(shape[i] - normal * margin);
    }
    return result;
}

float signed_area(const Shape &shape) {
    float area = 0;
    for (size_t i = 0; i < shape.size(); i++) area += cross(ImVec2(0, 0), shape[i], shape[(i + 1) % shape.size()]);
    return area;
}

Shape clip(const Shape &shape, ImVec2 point, ImVec2 outward) {
    Shape result;
    size_t count = shape.size();
    for (size_t i = 0; i < count; i++) {
        ImVec2 a = shape[i], b = shape[(i + 1) % count];
        float da = (a.x - point.x) * outward.x + (a.y - point.y) * outward.y;
        float db = (b.x - point.x) * outward.x + (b.y - point.y) * outward.y;
        if (da <= 0) result.push_back(a);
        if ((da <= 0) != (db <= 0)) result.push_back(a + (b - a) * (da / (da - db)));
    }
    return result;
}

Shape clip_convex(Shape shape, const Shape &window) {
    float orientation = signed_area(window) > 0 ? 1.0f : -1.0f;
    for (size_t i = 0; i < window.size() && !shape.empty(); i++) {
        ImVec2 a = window[i], b = window[(i + 1) % window.size()];
        ImVec2 edge = b - a;
        shape = clip(shape, a, ImVec2(edge.y, -edge.x) * orientation);
    }
    return shape;
}

Shape rounded(const Shape &shape, float radius) {
    float orientation = signed_area(shape) > 0 ? 1.0f : -1.0f;
    Shape inner = shape;
    for (size_t i = 0; i < shape.size() && !inner.empty(); i++) {
        ImVec2 a = shape[i], b = shape[(i + 1) % shape.size()];
        ImVec2 edge = b - a;
        float length = sqrtf(edge.x * edge.x + edge.y * edge.y);
        if (length < 1e-4f) continue;
        ImVec2 outward = ImVec2(edge.y, -edge.x) * (orientation / length);
        inner = clip(inner, a - outward * radius, outward);
    }
    if (inner.size() < 3) return shape;
    Shape result;
    size_t count = inner.size();
    for (size_t i = 0; i < count; i++) {
        ImVec2 before = inner[(i + count - 1) % count], at = inner[i], after = inner[(i + 1) % count];
        ImVec2 e0 = at - before, e1 = after - at;
        float from = atan2f(-e0.x * orientation, e0.y * orientation);
        float to = atan2f(-e1.x * orientation, e1.y * orientation);
        if (orientation > 0) { while (to < from) to += 2 * IM_PI; }
        else { while (to > from) to -= 2 * IM_PI; }
        int steps = std::max(1, (int)(fabsf(to - from) / 0.12f));
        for (int step = 0; step <= steps; step++) {
            float angle = from + (to - from) * step / steps;
            result.push_back(at + ImVec2(cosf(angle), sinf(angle)) * radius);
        }
    }
    return result;
}

Shape reference_circle(ImVec2 centre, float radius, int segments) {
    Shape shape;
    for (int i = 0; i < segments; i++) {
        float angle = 2 * IM_PI * i / segments;
        shape.push_back(centre + ImVec2(cosf(angle), sinf(angle)) * radius);
    }
    return shape;
}

float ease_flat(float x, float flat_x) {
    return flat_x + ARROW_SOFTNESS * logf(expf((x - flat_x) / ARROW_SOFTNESS) + 1.0f);
}

Shape arrow_region(bool up, float grow, float extend = 0) {
    Shape shape = reference_circle(ARROW_CENTRE, ARROW_R + grow, 180);
    float near_y = up ? ARROW_GAP_Y - ARROW_HALF_GAP + grow + extend : ARROW_GAP_Y + ARROW_HALF_GAP - grow - extend;
    shape = clip(shape, ImVec2(0, near_y), ImVec2(0, up ? 1.0f : -1.0f));
    Shape edge = reference_circle(ImVec2(ARROW_EDGE_X + grow - ARROW_EDGE_R, ARROW_GAP_Y), ARROW_EDGE_R, 720);
    shape = clip_convex(shape, edge);
    for (ImVec2 &point : shape) point.x = ease_flat(point.x, ARROW_CENTRE.x + ARROW_KEY_FLAT);
    return shape;
}

Shape arrow_key(bool up) {
    Shape shape = rounded(arrow_region(up, 0, ARROW_CORNER * 2), ARROW_CORNER);
    float gap_y = up ? ARROW_GAP_Y - ARROW_HALF_GAP : ARROW_GAP_Y + ARROW_HALF_GAP;
    shape = clip(shape, ImVec2(0, gap_y), ImVec2(0, up ? 1.0f : -1.0f));
    return rounded(shape, ARROW_GAP_CORNER);
}

Shape arrow_well() {
    float radius = ARROW_R + ARROW_WELL_MARGIN + 1.0f;
    float edge_x = ARROW_EDGE_X + ARROW_WELL_MARGIN + ARROW_WELL_TUCK + 10.0f;
    Shape shape;
    for (int i = 0; i <= 90; i++) {
        float angle = IM_PI * 0.5f + IM_PI * i / 90;
        ImVec2 point = ARROW_CENTRE + ImVec2(cosf(angle), sinf(angle)) * radius;
        point.x = ease_flat(point.x, ARROW_CENTRE.x + ARROW_WELL_FLAT);
        shape.push_back(point);
    }
    shape.push_back(ImVec2(edge_x, ARROW_CENTRE.y - radius));
    shape.push_back(ImVec2(edge_x, ARROW_CENTRE.y + radius));
    return rounded(shape, ARROW_WELL_CORNER);
}


Shape to_screen(const Frame &frame, const Shape &reference, bool right) {
    Shape shape;
    for (const ImVec2 &point : reference) shape.push_back(frame.at(point.x, point.y, right));
    return shape;
}

Shape traced(const Frame &frame, const char *name) {
    for (const TracedKey &key : traced_keys) {
        if (strcmp(key.name, name) != 0) continue;
        Shape shape;
        for (int i = 0; i < key.count; i++) shape.push_back(frame.at(key.points[i * 2], key.points[i * 2 + 1], key.right));
        return smooth(hull(shape), 2);
    }
    return Shape();
}

ImRect bounds(const Shape &shape) {
    ImRect box(shape[0], shape[0]);
    for (const ImVec2 &point : shape) box.Add(point);
    return box;
}


void fill(ImDrawList *draw, const Shape &shape, ImU32 top, ImU32 bottom) {
    ImRect box = bounds(shape);
    int start = draw->VtxBuffer.Size;
    draw->AddConvexPolyFilled(shape.data(), (int)shape.size(), top);
    ImGui::ShadeVertsLinearColorGradientKeepAlpha(draw, start, draw->VtxBuffer.Size, ImVec2(0, box.Min.y),
                                                  ImVec2(0, box.Max.y), top, bottom);
}



struct Span {
    float top;
    float bottom;
    bool valid;
};

Span convex_span(const Shape &shape, float x) {
    Span span = { FLT_MAX, -FLT_MAX, false };
    size_t count = shape.size();
    for (size_t i = 0; i < count; i++) {
        ImVec2 a = shape[i], b = shape[(i + 1) % count];
        if ((a.x <= x && b.x >= x) || (b.x <= x && a.x >= x)) {
            float y = fabsf(b.x - a.x) < 1e-5f ? a.y : a.y + (b.y - a.y) * (x - a.x) / (b.x - a.x);
            span.top = std::min(span.top, y);
            span.bottom = std::max(span.bottom, y);
            span.valid = true;
        }
    }
    return span;
}

ImU32 faded(ImU32 colour, float alpha) {
    int a = (int)(((colour >> IM_COL32_A_SHIFT) & 0xff) * std::max(0.0f, std::min(1.0f, alpha)));
    return (colour & ~IM_COL32_A_MASK) | ((ImU32)a << IM_COL32_A_SHIFT);
}

float smoothstep(float t) {
    t = std::max(0.0f, std::min(1.0f, t));
    return t * t * (3 - 2 * t);
}

struct RecessStyle {
    float outer;
    float inner;
    float flatness;
};

struct Mask {
    float from = 0;
    float to = 0;

    float at(float x) const {
        return from != to ? smoothstep((x - from) / (to - from)) : 1.0f;
    }
};

const RecessStyle FLUTE_RECESS = { 1.4f, 7.0f, 0.15f };
const RecessStyle KEY_WELL = { 1.1f, 5.5f, 0.3f };
const RecessStyle FLAT_WELL = { 0.9f, 4.5f, 1.0f };

struct RecessPalette {
    ImU32 shade;
    ImU32 lit;
    ImU32 bowl_top;
    ImU32 bowl_bottom;
    ImU32 floor;
};

const RecessPalette LID_RECESS = { IM_COL32(104, 112, 118, 255), IM_COL32(238, 242, 246, 255), IM_COL32(146, 154, 160, 255),
                                   IM_COL32(188, 195, 199, 255), IM_COL32(178, 186, 191, 255) };
const RecessPalette FINGER_SCOOP = { IM_COL32(66, 76, 84, 255), IM_COL32(214, 224, 230, 255), IM_COL32(84, 96, 104, 255),
                                     IM_COL32(138, 150, 158, 255), IM_COL32(120, 132, 140, 255) };
const ImU32 SLOPE_SHADE = IM_COL32(78, 86, 92, 255);
const ImU32 SLOPE_LIT = IM_COL32(236, 240, 242, 255);
const RecessStyle KEY_HOLE = { 0.6f, 1.4f, 1.0f };
const RecessPalette KEY_HOLE_PALETTE = { IM_COL32(112, 120, 124, 255), IM_COL32(236, 240, 242, 255), IM_COL32(150, 158, 160, 255),
                                         IM_COL32(160, 166, 166, 255), IM_COL32(152, 160, 160, 255) };
const RecessStyle SCOOP_RECESS = { 1.0f, 7.0f, 0.0f };

ImU32 mix(ImU32 a, ImU32 b, float t) {
    t = std::max(0.0f, std::min(1.0f, t));
    auto channel = [&](int shift) { return (int)(((a >> shift) & 0xff) * (1 - t) + ((b >> shift) & 0xff) * t); };
    return IM_COL32(channel(IM_COL32_R_SHIFT), channel(IM_COL32_G_SHIFT), channel(IM_COL32_B_SHIFT), channel(IM_COL32_A_SHIFT));
}

void draw_slope_ring(ImDrawList *draw, ImVec2 centre, float radius, float slope, ImU32 floor) {
    const int segments = 96;
    draw->PrimReserve(segments * 6, segments * 2);
    ImDrawIdx base = (ImDrawIdx)draw->_VtxCurrentIdx;
    ImVec2 uv = draw->_Data->TexUvWhitePixel;
    for (int i = 0; i < segments; i++) {
        float angle = 2 * IM_PI * i / segments;
        ImVec2 normal(cosf(angle), sinf(angle));
        ImU32 colour = normal.y < 0 ? mix(floor, SLOPE_SHADE, powf(-normal.y, 0.7f)) : mix(floor, SLOPE_LIT, powf(normal.y, 0.7f) * 0.85f);
        draw->PrimWriteVtx(centre + normal * radius, uv, colour);
        draw->PrimWriteVtx(centre + normal * (radius - slope), uv, colour);
    }
    for (int i = 0; i < segments; i++) {
        ImDrawIdx a = (ImDrawIdx)(base + i * 2), b = (ImDrawIdx)(base + ((i + 1) % segments) * 2);
        draw->PrimWriteIdx(a); draw->PrimWriteIdx(b); draw->PrimWriteIdx((ImDrawIdx)(b + 1));
        draw->PrimWriteIdx(a); draw->PrimWriteIdx((ImDrawIdx)(b + 1)); draw->PrimWriteIdx((ImDrawIdx)(a + 1));
    }
    Shape rim;
    for (int i = 0; i < segments; i++) {
        float angle = 2 * IM_PI * i / segments;
        rim.push_back(centre + ImVec2(cosf(angle), sinf(angle)) * radius);
    }
    draw->AddPolyline(rim.data(), (int)rim.size(), IM_COL32(80, 88, 94, 90), ImDrawFlags_Closed, 0.6f);
}

void draw_recess(ImDrawList *draw, const Shape &shape, const RecessStyle &style, float u, Mask mask = Mask(),
                 const RecessPalette &palette = LID_RECESS) {
    const int inner_rings = 6;
    size_t count = shape.size();
    ImRect box = bounds(shape);
    float outward = bounds(grown(shape, 1.0f)).GetWidth() > box.GetWidth() ? -1.0f : 1.0f;
    std::vector<ImVec2> normals(count);
    std::vector<float> facing(count);
    for (size_t i = 0; i < count; i++) {
        ImVec2 tangent = shape[(i + 1) % count] - shape[(i + count - 1) % count];
        float length = sqrtf(tangent.x * tangent.x + tangent.y * tangent.y);
        normals[i] = length > 0 ? ImVec2(tangent.y, -tangent.x) * (outward / length) : ImVec2(0, 0);
        facing[i] = smoothstep((normals[i].y + 1.0f) * 0.5f);
    }
    auto floor_colour = [&](ImVec2 point) {
        float t = box.GetHeight() > 0 ? (point.y - box.Min.y) / box.GetHeight() : 0.5f;
        return mix(mix(palette.bowl_top, palette.bowl_bottom, t), palette.floor, style.flatness);
    };
    auto shaded = [&](ImU32 colour, ImVec2 point) { return faded(colour, mask.at(point.x)); };

    const int rings = inner_rings + 2;
    float inner = style.inner * u;
    draw->PrimReserve((int)count * (rings - 1) * 6 + ((int)count - 2) * 3, (int)count * rings + (int)count);
    ImDrawIdx base = (ImDrawIdx)draw->_VtxCurrentIdx;
    ImVec2 uv = draw->_Data->TexUvWhitePixel;
    for (size_t i = 0; i < count; i++) {
        ImU32 wall = mix(palette.shade, palette.lit, facing[i]);
        ImVec2 edge = shape[i];
        ImVec2 outer_point = edge + normals[i] * (style.outer * u);
        draw->PrimWriteVtx(outer_point, uv, shaded(wall, outer_point) & ~IM_COL32_A_MASK);
        draw->PrimWriteVtx(edge, uv, shaded(wall, edge));
        for (int ring = 1; ring <= inner_rings; ring++) {
            float t = (float)ring / inner_rings;
            ImVec2 point = edge - normals[i] * (inner * t);
            draw->PrimWriteVtx(point, uv, shaded(mix(wall, floor_colour(point), smoothstep(t)), point));
        }
    }
    for (size_t i = 0; i < count; i++) {
        ImDrawIdx a = (ImDrawIdx)(base + i * rings), b = (ImDrawIdx)(base + ((i + 1) % count) * rings);
        for (int ring = 0; ring < rings - 1; ring++) {
            draw->PrimWriteIdx((ImDrawIdx)(a + ring)); draw->PrimWriteIdx((ImDrawIdx)(b + ring)); draw->PrimWriteIdx((ImDrawIdx)(b + ring + 1));
            draw->PrimWriteIdx((ImDrawIdx)(a + ring)); draw->PrimWriteIdx((ImDrawIdx)(b + ring + 1)); draw->PrimWriteIdx((ImDrawIdx)(a + ring + 1));
        }
    }
    ImDrawIdx centre = (ImDrawIdx)draw->_VtxCurrentIdx;
    for (size_t i = 0; i < count; i++) {
        ImVec2 point = shape[i] - normals[i] * inner;
        draw->PrimWriteVtx(point, uv, shaded(floor_colour(point), point));
    }
    for (size_t i = 1; i + 1 < count; i++) {
        draw->PrimWriteIdx(centre); draw->PrimWriteIdx((ImDrawIdx)(centre + i)); draw->PrimWriteIdx((ImDrawIdx)(centre + i + 1));
    }
}

float bezel_brightness(float x, const ImVec2 &lcd_min, const ImVec2 &lcd_max, float u) {
    static const float left_profile[][2] = { { -202, 0.80f }, { -192, 0.98f }, { -182, 1.07f }, { -150, 1.03f }, { -60, 0.97f }, { 0, 0.94f } };
    static const float right_profile[][2] = { { 0, 0.94f }, { 60, 0.97f }, { 170, 1.03f }, { 196, 1.07f }, { 206, 0.98f }, { 214, 0.80f } };
    const float (*profile)[2];
    int count = 6;
    float at;
    if (x < lcd_min.x) {
        profile = left_profile;
        at = (x - lcd_min.x) / u;
    } else if (x > lcd_max.x) {
        profile = right_profile;
        at = (x - lcd_max.x) / u;
    } else {
        return 0.94f;
    }
    if (at <= profile[0][0]) return profile[0][1];
    for (int i = 1; i < count; i++) {
        if (at <= profile[i][0]) {
            float t = (at - profile[i - 1][0]) / (profile[i][0] - profile[i - 1][0]);
            return profile[i - 1][1] + (profile[i][1] - profile[i - 1][1]) * t;
        }
    }
    return profile[count - 1][1];
}

void shade_body(ImDrawList *draw, ImVec2 device_min, ImVec2 device_max, float rounding, const ImVec2 &lcd_min,
                const ImVec2 &lcd_max, float u) {
    Shape body;
    add_arc(body, ImVec2(device_max.x - rounding, device_min.y + rounding), rounding, IM_PI * 1.5f, IM_PI * 2.0f, 16);
    add_arc(body, ImVec2(device_max.x - rounding, device_max.y - rounding), rounding, 0, IM_PI * 0.5f, 16);
    add_arc(body, ImVec2(device_min.x + rounding, device_max.y - rounding), rounding, IM_PI * 0.5f, IM_PI, 16);
    add_arc(body, ImVec2(device_min.x + rounding, device_min.y + rounding), rounding, IM_PI, IM_PI * 1.5f, 16);
    int columns = std::max(16, (int)((device_max.x - device_min.x) / 3));
    draw->PrimReserve(columns * 12, (columns + 1) * 3);
    ImDrawIdx base = (ImDrawIdx)draw->_VtxCurrentIdx;
    ImVec2 uv = draw->_Data->TexUvWhitePixel;
    int r = (BEZEL >> IM_COL32_R_SHIFT) & 0xff, g = (BEZEL >> IM_COL32_G_SHIFT) & 0xff, b = (BEZEL >> IM_COL32_B_SHIFT) & 0xff;
    for (int column = 0; column <= columns; column++) {
        float x = device_min.x + (device_max.x - device_min.x) * column / columns;
        Span span = convex_span(body, std::max(device_min.x + 0.01f, std::min(device_max.x - 0.01f, x)));
        float k = bezel_brightness(x, lcd_min, lcd_max, u);
        int cr = std::min(255, (int)(r * k)), cg = std::min(255, (int)(g * k)), cb = std::min(255, (int)(b * k));
        const float sheen = 0.16f;
        ImU32 top = IM_COL32(cr + (int)((255 - cr) * sheen), cg + (int)((255 - cg) * sheen), cb + (int)((255 - cb) * sheen), 255);
        ImU32 colour = IM_COL32(cr, cg, cb, 255);
        float middle = span.top + (device_max.y - device_min.y) * 0.45f;
        draw->PrimWriteVtx(ImVec2(x, span.top), uv, top);
        draw->PrimWriteVtx(ImVec2(x, std::min(middle, span.bottom)), uv, colour);
        draw->PrimWriteVtx(ImVec2(x, span.bottom), uv, colour);
    }
    for (int column = 0; column < columns; column++) {
        for (int row = 0; row < 2; row++) {
            ImDrawIdx i = (ImDrawIdx)(base + column * 3 + row);
            draw->PrimWriteIdx(i); draw->PrimWriteIdx((ImDrawIdx)(i + 3)); draw->PrimWriteIdx((ImDrawIdx)(i + 4));
            draw->PrimWriteIdx(i); draw->PrimWriteIdx((ImDrawIdx)(i + 4)); draw->PrimWriteIdx((ImDrawIdx)(i + 1));
        }
    }
}

ImU32 lighten(ImU32 colour, int amount) {
    int r = std::min(255, (int)((colour >> IM_COL32_R_SHIFT) & 0xff) + amount);
    int g = std::min(255, (int)((colour >> IM_COL32_G_SHIFT) & 0xff) + amount);
    int b = std::min(255, (int)((colour >> IM_COL32_B_SHIFT) & 0xff) + amount);
    return IM_COL32(r, g, b, 255);
}

Shape inset(const Shape &shape, float distance) {
    Shape result = grown(shape, distance);
    if (bounds(result).GetWidth() > bounds(shape).GetWidth()) result = grown(shape, -distance);
    return result;
}

Shape outset(const Shape &shape, float distance) {
    Shape result = grown(shape, distance);
    if (bounds(result).GetWidth() < bounds(shape).GetWidth()) result = grown(shape, -distance);
    return result;
}


void draw_key(ImDrawList *draw, const Shape &shape, const ButtonStyle &style, bool pressed, float u) {
    erase_colour = faded(mix(style.top, style.bottom, 0.55f), 0.9f);
    rub_mode = true;
    const int layers = 4;
    for (int layer = layers; layer >= 1; layer--) {
        int alpha = (int)(((pressed ? 10 : 18) + (layers - layer) * 4) * style.shadow);
        fill(draw, translated(shape, ImVec2(0, layer * 0.8f * u)), IM_COL32(20, 26, 32, alpha), IM_COL32(20, 26, 32, alpha));
    }
    if (style.gap > 0) fill(draw, outset(shape, style.gap * u), IM_COL32(16, 20, 24, 215), IM_COL32(16, 20, 24, 170));
    Shape body = translated(shape, ImVec2(0, pressed ? 1.2f * u : 0));
    ImU32 face_top = pressed ? style.bottom : style.top;
    fill(draw, body, lighten(face_top, pressed ? style.rim / 3 : style.rim), lighten(style.bottom, 16));
    fill(draw, inset(body, 1.3f * u), face_top, style.bottom);
    if (style.dome > 0) {
        ImRect box = bounds(body);
        float depth = std::min(box.GetWidth(), box.GetHeight()) * 0.5f;
        ImVec2 offset = ImVec2(-0.08f, -0.14f) * depth;
        const int rings = 14;
        for (int ring = 0; ring < rings; ring++) {
            float t = (float)ring / rings;
            Shape layer = translated(inset(body, depth * (0.2f + 0.75f * t)), offset * t);
            fill(draw, layer, IM_COL32(255, 255, 255, (int)(4 * style.dome)), IM_COL32(255, 255, 255, (int)(2 * style.dome)));
        }
    }
}

ImVec2 hit_pad(0, 0);

bool hit(const char *id, const Shape &shape, bool &pressed) {
    ImRect box = bounds(shape);
    box.Min -= hit_pad;
    box.Max += hit_pad;
    ImGui::SetCursorScreenPos(box.Min);
    ImGui::InvisibleButton(id, box.GetSize());
    pressed = ImGui::IsItemActive();
    return ImGui::IsItemActivated();
}

bool second_arrow(uint32_t code, uint8_t mods, bool activated, DeviceState &state) {
    if (!state.second) return false;
    if (activated) {
        state.second = false;
        if (code == HOST_KEY_LEFT) keys_push(HOST_KEY_HOME, mods);
        else if (code == HOST_KEY_RIGHT) keys_push(HOST_KEY_END, mods);
        else keys_push(code, mods | HOST_MOD_SECOND);
    }
    return true;
}

void repeat_key(KeyRepeat &repeat, uint32_t code, bool activated, bool pressed, uint8_t mods = 0) {
    uint64_t now = SDL_GetTicks();
    if (activated) {
        keys_push(code, mods);
        keys_set_held(code, true);
        repeat = { true, now, now };
    } else if (pressed && repeat.active && now - repeat.since >= REPEAT_DELAY_MS && now - repeat.last >= REPEAT_RATE_MS) {
        keys_push(code, mods);
        repeat.last = now;
    } else if (!pressed && repeat.active) {
        keys_set_held(code, false);
        repeat.active = false;
    }
}

void finger_grime(ImDrawList *draw, const Shape &shape, float amount, float u) {
    if (!wear_grime || amount <= 0) return;
    const int rings = 8;
    float reach = 13.0f * u;
    for (int ring = rings; ring >= 1; ring--) {
        float t = (float)ring / rings;
        int alpha = (int)(amount * 34.0f * (1.0f - t * 0.6f));
        Shape halo = translated(outset(shape, reach * t), ImVec2(0, reach * 0.35f * t));
        fill(draw, halo, IM_COL32(58, 52, 44, alpha / 2), IM_COL32(58, 52, 44, alpha));
    }
}

enum { LID_SIDE, LID_LIGHT = 5, LID_MENU, LID_POWER, LID_UP, LID_DOWN, LID_ESC, LID_ENTER, LID_KEY_COUNT };

const char *const SIDE_NAMES[] = { "main", "tel", "cal", "memo", "prog" };

struct LidLayout {
    ImRect side_boxes[5];
    Shape side[5];
    ImRect light_box;
    Shape light;
    ImVec2 menu_centre, esc_centre, enter_centre;
    Shape menu, esc, enter, power, up, down;
    ImRect power_box;
};

LidLayout lid_layout(const Frame &frame) {
    float u = frame.u;
    LidLayout lid;
    for (int index = 0; index < 5; index++) {
        lid.side_boxes[index] = bounds(traced(frame, SIDE_NAMES[index]));
        lid.side[index] = side_key(lid.side_boxes[index].Min, lid.side_boxes[index].Max, SIDE_KEY_CORNER * u);
    }
    lid.light_box = bounds(traced(frame, "light"));
    lid.light = pill(lid.light_box.Min, lid.light_box.Max);
    lid.menu_centre = frame.at(fit_menu[0], fit_menu[1], true);
    lid.esc_centre = frame.at(fit_esc[0], fit_esc[1], true);
    lid.enter_centre = frame.at(fit_enter[0], fit_enter[1], true);
    lid.menu = circle(lid.menu_centre, fit_menu[2] * u);
    lid.esc = circle(lid.esc_centre, fit_esc[2] * u);
    lid.enter = circle(lid.enter_centre, fit_enter[2] * u);
    lid.power = pill(frame.at(fit_power[0] - fit_power[2], fit_power[1] - fit_power[3], true),
                     frame.at(fit_power[0] + fit_power[2], fit_power[1] + fit_power[3], true));
    lid.power_box = bounds(lid.power);
    lid.up = to_screen(frame, arrow_key(true), true);
    lid.down = to_screen(frame, arrow_key(false), true);
    return lid;
}

void input_lid_keys(const Frame &frame, DeviceState &state, uint8_t *down) {
    float u = frame.u;
    hit_pad = state.touch ? ImVec2(6.0f, 7.0f) * u : ImVec2(0, 0);
    bool live = state.powered;
    LidLayout lid = lid_layout(frame);
    const char *side_ids[] = { "key-main", "key-tel", "key-cal", "key-memo", "key-prog" };
    for (int index = 0; index < 5; index++) {
        bool pressed;
        if (hit(side_ids[index], lid.side[index], pressed) && live) keys_push(HOST_KEY_F1 + index, 0);
        down[LID_SIDE + index] = pressed;
    }
    bool pressed;
    if (hit("key-light", lid.light, pressed) && live) keys_push(HOST_KEY_F1 + 5, 0);
    down[LID_LIGHT] = pressed;
    if (hit("key-menu", lid.menu, pressed) && live) keys_push(HOST_KEY_TAB, HOST_MOD_LID);
    down[LID_MENU] = pressed;
    if (hit("key-power", lid.power, pressed)) runtime_press_power();
    down[LID_POWER] = pressed;
    const char *arrow_ids[] = { "key-up", "key-down" };
    const uint32_t arrow_codes[] = { HOST_KEY_UP, HOST_KEY_DOWN };
    for (int index = 0; index < 2; index++) {
        bool activated = hit(arrow_ids[index], index == 0 ? lid.up : lid.down, pressed);
        if (!(live && second_arrow(arrow_codes[index], HOST_MOD_LID, activated, state))) {
            repeat_key(repeats[index], arrow_codes[index], activated && live, pressed && live, HOST_MOD_LID);
        }
        down[LID_UP + index] = pressed;
    }
    if (hit("key-esc", lid.esc, pressed) && live) keys_push(HOST_KEY_ESC, HOST_MOD_LID);
    down[LID_ESC] = pressed;
    if (hit("key-enter", lid.enter, pressed) && live) keys_push(HOST_KEY_ENTER, HOST_MOD_LID);
    down[LID_ENTER] = pressed;
}

void paint_lid_keys(ImDrawList *draw, const Frame &frame, ImVec2 device_min, ImVec2 device_max, const uint8_t *down) {
    float u = frame.u;
    ImVec2 press(0, 1.2f * u);
    auto dip = [&](bool pressed) { return pressed ? press : ImVec2(0, 0); };
    LidLayout lid = lid_layout(frame);

    draw->PushClipRect(device_min, device_max, true);
    for (int index = 0; index < 5; index++) {
        const ImRect &box = lid.side_boxes[index];
        float reach = box.GetHeight() * FLUTE_REACH;
        Shape scoop = side_key(ImVec2(box.Min.x - reach, box.Min.y - FLUTE_MARGIN * u),
                               ImVec2(box.Max.x + FLUTE_MARGIN * u, box.Max.y + FLUTE_MARGIN * u),
                               (SIDE_KEY_CORNER + FLUTE_MARGIN) * u);
        draw_recess(draw, scoop, FLUTE_RECESS, u, Mask{ box.Min.x - reach, box.Min.x + box.GetHeight() * 0.2f });
    }
    {
        float reach = lid.power_box.GetHeight() * FLUTE_REACH;
        Shape scoop = pill(lid.power_box.Min - ImVec2(FLUTE_MARGIN, FLUTE_MARGIN) * u,
                           ImVec2(lid.power_box.Max.x + reach, lid.power_box.Max.y + FLUTE_MARGIN * u));
        draw_recess(draw, scoop, FLUTE_RECESS, u, Mask{ lid.power_box.Max.x + reach, lid.power_box.Max.x - lid.power_box.GetHeight() * 0.2f });
    }
    {
        Shape well = to_screen(frame, arrow_well(), true);
        draw_recess(draw, well, KEY_WELL, u, Mask{ bounds(well).Max.x, frame.at(ARROW_EDGE_X - 4.0f, 0, true).x });
    }
    draw->PopClipRect();
    const float side_wear[] = { 0.7f, 0.45f, 0.45f, 0.5f, 1.0f };
    for (int index = 0; index < 5; index++) finger_grime(draw, lid.side[index], side_wear[index], u);
    finger_grime(draw, lid.menu, 1.0f, u);
    finger_grime(draw, lid.power, 0.4f, u);
    finger_grime(draw, lid.up, 0.8f, u);
    finger_grime(draw, lid.down, 0.85f, u);
    finger_grime(draw, lid.esc, 0.8f, u);
    finger_grime(draw, lid.enter, 0.9f, u);

    const char *side_text[] = { "MAIN", nullptr, nullptr, nullptr, "PROG" };
    const unsigned side_glyph[] = { 0, ICON_CALL, ICON_CALENDAR, ICON_NOTE, 0 };
    for (int index = 0; index < 5; index++) {
        bool pressed = down[LID_SIDE + index];
        draw_key(draw, lid.side[index], DARK_KEY, pressed, u);
        ImVec2 at = bounds(lid.side[index]).GetCenter() + ImVec2(2 * u, 0) + dip(pressed);
        if (side_text[index]) centred_text(draw, at, 17.0f * u, LABEL, side_text[index]);
        else icon(draw, at, 44.0f * u, ICON_BLUE, side_glyph[index]);
    }

    draw_recess(draw, pill(lid.light_box.Min - ImVec2(WELL_MARGIN, WELL_MARGIN) * u, lid.light_box.Max + ImVec2(WELL_MARGIN, WELL_MARGIN) * u), KEY_WELL, u);
    draw_key(draw, lid.light, TEAL_KEY, down[LID_LIGHT], u);
    icon(draw, lid.light_box.GetCenter() + dip(down[LID_LIGHT]), 34.0f * u, LABEL, ICON_LIGHT);

    erase_colour = faded(BEZEL, 0.9f);
    rub_mode = false;
    centred_text(draw, lid.menu_centre - ImVec2(0, 39 * u), 15.0f * u, PRINT, "MENU");
    draw_recess(draw, circle(lid.menu_centre, (fit_menu[2] + WELL_MARGIN + 2.5f) * u), KEY_WELL, u);
    draw_key(draw, lid.menu, DARK_DOMED_KEY, down[LID_MENU], u);

    erase_colour = faded(BEZEL, 0.9f);
    rub_mode = false;
    centred_text(draw, ImVec2(lid.power_box.GetCenter().x, lid.menu_centre.y - 39 * u), 15.0f * u, PRINT, "POWER");
    draw_key(draw, lid.power, TEAL_KEY, down[LID_POWER], u);
    icon(draw, lid.power_box.GetCenter() + dip(down[LID_POWER]), 32.0f * u, LABEL, ICON_POWER);

    for (int index = 0; index < 2; index++) {
        const Shape &shape = index == 0 ? lid.up : lid.down;
        bool pressed = down[LID_UP + index];
        draw_key(draw, shape, BLUE_KEY, pressed, u);
        ImRect box = bounds(shape);
        ImVec2 centre = ImVec2(box.GetCenter().x + 3.0f * u, box.GetCenter().y + (index == 0 ? 4.0f : -4.0f) * u) + dip(pressed);
        float dy = index == 0 ? 1.0f : -1.0f;
        ImVec2 chevron[3] = { centre + ImVec2(-19.0f * u, 8.0f * u * dy), centre + ImVec2(0, -8.0f * u * dy),
                              centre + ImVec2(19.0f * u, 8.0f * u * dy) };
        draw->AddPolyline(chevron, 3, IM_COL32(222, 228, 234, 235), 0, 2.6f * u);
    }

    draw_recess(draw, capsule(lid.esc_centre, (fit_esc[2] + 6) * u, lid.enter_centre, (fit_enter[2] + 6) * u), FLAT_WELL, u);
    draw_key(draw, lid.esc, DARK_KEY, down[LID_ESC], u);
    centred_text(draw, lid.esc_centre + dip(down[LID_ESC]), 15.0f * u, LABEL, "ESC");
    draw_key(draw, lid.enter, DARK_KEY, down[LID_ENTER], u);
    centred_text(draw, lid.enter_centre + dip(down[LID_ENTER]), 16.0f * u, LABEL, "ENTER");
}

const float HINGE = 30.0f;
const float KB_WIDTH_RATIO = 1.0f;
const float KB_PAD_X = 16.0f;
const float KEY_TRAVEL = 2.6f;
const float KEY_SHOULDER = 4.0f;
const float KB_SQUARE_CORNER = 0.22f;
const float KB_PAD_TOP = 12.0f;
const float KB_PAD_BOTTOM = 10.0f;
const float KB_BOTTOM_MARGIN = 6.0f;
const ImU32 KB_RING = IM_COL32(96, 84, 156, 255);
const ImU32 KB_HIGHLIGHT = IM_COL32(236, 240, 242, 255);

KeyRepeat keyboard_repeats[4];

ImFont *keyboard_font(bool label) {
    ImFont *font = label ? kb_label_font : kb_legend_font;
    return font ? font : text_font();
}

float glyph_middle(ImFont *font, float size, unsigned codepoint, float *bottom = nullptr) {
    ImFontBaked *baked = font->GetFontBaked(size);
    ImFontGlyph *glyph = baked->FindGlyph((ImWchar)codepoint);
    float scale = size / baked->Size;
    if (bottom) *bottom = glyph->Y1 * scale;
    return (glyph->Y0 + glyph->Y1) * 0.5f * scale;
}

float stretched_width(ImFont *font, float size, const char *text, float stretch) {
    return font->CalcTextSizeA(size, FLT_MAX, 0.0f, text).x * stretch;
}

void stretched_text(ImDrawList *draw, ImFont *font, float size, const char *text, float left, float top, ImU32 colour, float stretch) {
    int start = draw->VtxBuffer.Size;
    draw->AddText(font, size, ImVec2(left, top), colour, text);
    for (int i = start; i < draw->VtxBuffer.Size; i++) {
        draw->VtxBuffer[i].pos.x = left + (draw->VtxBuffer[i].pos.x - left) * stretch;
    }
}

ImRect centred_legend(ImDrawList *draw, ImFont *font, float size, const char *text, ImVec2 centre, ImU32 colour, float stretch) {
    unsigned first = 0;
    ImTextCharFromUtf8(&first, text, nullptr);
    bool own_glyph = text[1] == 0 || strcmp(text, "−") == 0;
    unsigned reference = own_glyph ? first : 'H';
    float middle = glyph_middle(font, size, reference);
    float width = stretched_width(font, size, text, stretch);
    float left = centre.x - width * 0.5f;
    float top = centre.y - middle;
    stretched_text(draw, font, size, text, left, top, colour, stretch);
    float half_height = glyph_middle(font, size, 'H') - font->GetFontBaked(size)->FindGlyph('H')->Y0 * size / font->GetFontBaked(size)->Size;
    return ImRect(left, centre.y - half_height, left + width, centre.y + half_height);
}

void label_on_baseline(ImDrawList *draw, ImFont *font, float size, const char *text, float cx, float baseline, ImU32 colour, float stretch) {
    float bottom = 0;
    glyph_middle(font, size, 'H', &bottom);
    float width = stretched_width(font, size, text, stretch);
    stretched_text(draw, font, size, text, cx - width * 0.5f, baseline - bottom, colour, stretch);
}

Shape rounded_box(ImVec2 a, ImVec2 b, float top_radius, float bottom_radius) {
    Shape shape;
    add_arc(shape, ImVec2(b.x - top_radius, a.y + top_radius), top_radius, -IM_PI * 0.5f, 0, 16);
    add_arc(shape, ImVec2(b.x - bottom_radius, b.y - bottom_radius), bottom_radius, 0, IM_PI * 0.5f, 16);
    add_arc(shape, ImVec2(a.x + bottom_radius, b.y - bottom_radius), bottom_radius, IM_PI * 0.5f, IM_PI, 16);
    add_arc(shape, ImVec2(a.x + top_radius, a.y + top_radius), top_radius, IM_PI, IM_PI * 1.5f, 16);
    return shape;
}

void cubic(Shape &shape, ImVec2 p0, ImVec2 p1, ImVec2 p2, ImVec2 p3, int steps) {
    for (int i = 1; i <= steps; i++) {
        float t = (float)i / steps, m = 1 - t;
        shape.push_back(p0 * (m * m * m) + p1 * (3 * m * m * t) + p2 * (3 * m * t * t) + p3 * (t * t * t));
    }
}

Shape cursor_outline(float length, float breadth) {
    const float width = 82.0f, height = 82.04f, corner = 9.33f, side_end = 18.87f;
    Shape half;
    add_arc(half, ImVec2(corner, corner), corner, IM_PI * 1.5f, IM_PI, 12);
    half.push_back(ImVec2(0, side_end));
    cubic(half, ImVec2(0, side_end), ImVec2(0, side_end + 37.81f), ImVec2(18.35f, height), ImVec2(width * 0.5f, height), 24);
    Shape outline = half;
    for (int i = (int)half.size() - 2; i >= 0; i--) outline.push_back(ImVec2(width - half[i].x, half[i].y));
    Shape local;
    for (const ImVec2 &point : outline) {
        local.push_back(ImVec2(-length * 0.5f + point.y / height * length, -breadth * 0.5f + point.x / width * breadth));
    }
    return local;
}

template <typename ColourAt>
void ring_mesh(ImDrawList *draw, const Shape &shape, const std::vector<float> &offsets, ColourAt colour_at) {
    size_t count = shape.size();
    float outward = bounds(grown(shape, 1.0f)).GetWidth() > bounds(shape).GetWidth() ? -1.0f : 1.0f;
    std::vector<ImVec2> normals(count);
    for (size_t i = 0; i < count; i++) {
        ImVec2 tangent = shape[(i + 1) % count] - shape[(i + count - 1) % count];
        float length = sqrtf(tangent.x * tangent.x + tangent.y * tangent.y);
        normals[i] = length > 0 ? ImVec2(tangent.y, -tangent.x) * (outward / length) : ImVec2(0, 0);
    }
    int rings = (int)offsets.size();
    draw->PrimReserve((int)count * (rings - 1) * 6, (int)count * rings);
    ImDrawIdx base = (ImDrawIdx)draw->_VtxCurrentIdx;
    ImVec2 uv = draw->_Data->TexUvWhitePixel;
    for (size_t i = 0; i < count; i++) {
        for (int ring = 0; ring < rings; ring++) {
            draw->PrimWriteVtx(shape[i] + normals[i] * offsets[ring], uv, colour_at(normals[i], ring));
        }
    }
    for (size_t i = 0; i < count; i++) {
        ImDrawIdx a = (ImDrawIdx)(base + i * rings), b = (ImDrawIdx)(base + ((i + 1) % count) * rings);
        for (int ring = 0; ring < rings - 1; ring++) {
            draw->PrimWriteIdx((ImDrawIdx)(a + ring)); draw->PrimWriteIdx((ImDrawIdx)(b + ring)); draw->PrimWriteIdx((ImDrawIdx)(b + ring + 1));
            draw->PrimWriteIdx((ImDrawIdx)(a + ring)); draw->PrimWriteIdx((ImDrawIdx)(b + ring + 1)); draw->PrimWriteIdx((ImDrawIdx)(a + ring + 1));
        }
    }
}

struct KeyboardFrame {
    ImVec2 origin;
    float kbu;

    ImVec2 at(float x, float y) const {
        return origin + ImVec2(x, y) * kbu;
    }

    Shape local(const Shape &shape, float x, float y) const {
        Shape result;
        for (const ImVec2 &point : shape) result.push_back(at(x + point.x, y + point.y));
        return result;
    }
};

Shape keyboard_key_shape(const KeyboardFrame &frame, const KeyboardKey &key) {
    if (key.shape == KB_SHAPE_CURSOR) {
        Shape local = cursor_outline(key.w, key.h);
        float angle = key.round_side * IM_PI * 0.5f;
        float c = cosf(angle), s = sinf(angle);
        for (ImVec2 &point : local) point = ImVec2(point.x * c - point.y * s, point.x * s + point.y * c);
        return frame.local(local, key.x, key.y);
    }
    ImVec2 a = frame.at(key.x - key.w * 0.5f, key.y - key.h * 0.5f), b = frame.at(key.x + key.w * 0.5f, key.y + key.h * 0.5f);
    if (key.shape == KB_SHAPE_SQUARE_END) {
        Shape shape = side_key(a, b, KB_SQUARE_CORNER * (b.y - a.y));
        if (key.round_side == 2) {
            for (ImVec2 &point : shape) point.x = a.x + b.x - point.x;
            std::reverse(shape.begin(), shape.end());
        }
        return shape;
    }
    return pill(a, b);
}

void keyboard_press(const KeyboardKey &key, DeviceState &state) {
    if (key.action == KB_ACTION_SECOND) {
        state.second = !state.second;
        return;
    }
    if (key.action == KB_ACTION_SHIFT && strcmp(key.id, "shift_right") == 0 && !state.second) {
        state.select = !state.select;
        return;
    }
    if (key.action == KB_ACTION_SHIFT) {
        if (state.second) {
            state.caps = !state.caps;
            state.second = false;
        } else {
            state.shift = !state.shift;
        }
        return;
    }
    state.select = false;
    uint32_t code = key.code;
    if (state.second) {
        code = key.second_code;
    } else if (key.shift_code && (state.shift || (state.caps && key.letter))) {
        code = key.shift_code;
    }
    state.second = false;
    state.shift = false;
    if (code == KB_KEY_CAPS) {
        state.caps = !state.caps;
        return;
    }
    if (code) keys_push(code, 0);
}

void keyboard_icon(ImDrawList *draw, const KeyboardFrame &frame, const KeyboardKey &key, ImVec2 centre, ImU32 colour) {
    float k = frame.kbu;
    auto point = [&](float x, float y) { return centre + ImVec2(x, y) * k; };
    switch (key.icon) {
        case KB_ICON_BACKSPACE: {
            centred_legend(draw, keyboard_font(false), key.legend_size * k, key.legend, point(1.0f, -6.5f), colour, KB_LEGEND_STRETCH);
            ImVec2 base = point(1.0f, 7.5f);
            float left = -15.0f, right = 15.0f, head = 8.0f;
            ImVec2 arrow[7] = { base + ImVec2(left, 0) * k, base + ImVec2(left + head, -5.0f) * k, base + ImVec2(left + head, -1.5f) * k,
                                base + ImVec2(right, -1.5f) * k, base + ImVec2(right, 1.5f) * k, base + ImVec2(left + head, 1.5f) * k,
                                base + ImVec2(left + head, 5.0f) * k };
            draw->AddConcavePolyFilled(arrow, 7, colour);
            break;
        }
        case KB_ICON_RETURN: {
            float left = -30.0f, right = 28.5f, head = 8.0f, stem_top = -7.5f, bar = 3.0f;
            ImVec2 arrow[9] = { point(left, bar), point(left + head, bar - 5.0f), point(left + head, bar - 1.5f), point(right - 3.0f, bar - 1.5f),
                                point(right - 3.0f, stem_top), point(right, stem_top), point(right, bar + 1.5f), point(left + head, bar + 1.5f),
                                point(left + head, bar + 5.0f) };
            draw->AddConcavePolyFilled(arrow, 9, colour);
            break;
        }
        case KB_ICON_SHIFT: {
            ImVec2 outline[7] = { point(0, -11.0f), point(12.0f, 1.0f), point(5.8f, 1.0f), point(5.8f, 10.0f), point(-5.8f, 10.0f),
                                  point(-5.8f, 1.0f), point(-12.0f, 1.0f) };
            draw->AddPolyline(outline, 7, colour, ImDrawFlags_Closed, 1.7f * k);
            break;
        }
        case KB_ICON_BOX_DOWN: {
            draw->AddRect(point(-12.5f, -8.5f), point(12.5f, 8.5f), colour, 0, 0, 1.6f * k);
            draw->AddTriangleFilled(point(-7.0f, -4.5f), point(7.0f, -4.5f), point(0, 2.5f), colour);
            draw->AddRectFilled(point(-7.0f, 3.6f), point(7.0f, 5.2f), colour);
            break;
        }
        case KB_ICON_TRIANGLE: {
            float angle = key.round_side * IM_PI * 0.5f;
            ImVec2 corners[3];
            for (int corner = 0; corner < 3; corner++) {
                float theta = angle + corner * IM_PI * 2.0f / 3.0f;
                corners[corner] = centre + ImVec2(cosf(theta), sinf(theta)) * (9.0f * 0.62f * k);
            }
            draw->AddTriangleFilled(corners[0], corners[1], corners[2], colour);
            break;
        }
        default:
            break;
    }
}

void keyboard_secondary(ImDrawList *draw, const KeyboardFrame &frame, const KeyboardKey &key, const KeyboardSecondary &item) {
    float k = frame.kbu;
    ImFont *font = keyboard_font(true);
    float baseline = frame.at(0, key.y - key.h * 0.5f - KB_LABEL_CLEARANCE).y;
    float cx = frame.at(key.x + item.dx, 0).x;
    float size = item.size * k;
    ImU32 colour = faded(KB_SECONDARY[item.colour], KB_LEGEND_ALPHA);
    float cap_bottom = 0;
    float cap_middle = glyph_middle(font, size, 'H', &cap_bottom);
    float cap = (cap_bottom - cap_middle) * 2.0f;
    if (item.colour == KB_BADGE) {
        float width = stretched_width(font, size, item.text, KB_LEGEND_STRETCH);
        draw->AddRectFilled(ImVec2(cx - width * 0.5f - 1.2f * k, baseline - cap - 1.1f * k), ImVec2(cx + width * 0.5f + 1.2f * k, baseline + 1.1f * k),
                           KB_SECONDARY[KB_BADGE], 0.5f * k);
        label_on_baseline(draw, font, size, item.text, cx, baseline, KB_BADGE_TEXT, KB_LEGEND_STRETCH);
        return;
    }
    if (item.icon == KB_ICON_CHECK) {
        float s = 9.0f * k;
        ImVec2 middle(cx, baseline - cap * 0.45f);
        ImVec2 tick[3] = { middle + ImVec2(-0.45f, -0.15f) * s, middle + ImVec2(-0.05f, 0.25f) * s, middle + ImVec2(0.55f, -0.3f) * s };
        draw->AddPolyline(tick, 3, colour, 0, 0.12f * s);
        return;
    }
    if (item.icon == KB_ICON_CASE_TOGGLE) {
        float small_size = size * 0.72f;
        float small_width = stretched_width(font, small_size, "A", KB_LEGEND_STRETCH);
        float large_width = stretched_width(font, size, "A", KB_LEGEND_STRETCH);
        float arrows = 6.5f * k, spacing = 0.8f * k;
        float x = cx - (small_width + arrows + large_width + 2 * spacing) * 0.5f;
        label_on_baseline(draw, font, small_size, "A", x + small_width * 0.5f, baseline, colour, KB_LEGEND_STRETCH);
        float mid_x = x + small_width + spacing + arrows * 0.5f, mid_y = baseline - cap * 0.5f, half = arrows * 0.5f;
        ImVec2 upper[3] = { ImVec2(mid_x - half, mid_y - 1.3f * k), ImVec2(mid_x + half, mid_y - 1.3f * k), ImVec2(mid_x + half - 2.4f * k, mid_y - 3.3f * k) };
        ImVec2 lower[3] = { ImVec2(mid_x + half, mid_y + 1.3f * k), ImVec2(mid_x - half, mid_y + 1.3f * k), ImVec2(mid_x - half + 2.4f * k, mid_y + 3.3f * k) };
        draw->AddPolyline(upper, 3, colour, 0, 0.75f * k);
        draw->AddPolyline(lower, 3, colour, 0, 0.75f * k);
        label_on_baseline(draw, font, size, "A", x + small_width + arrows + 2 * spacing + large_width * 0.5f, baseline, colour, KB_LEGEND_STRETCH);
        return;
    }
    if (strcmp(item.text, "*") == 0) {
        ImVec2 middle(cx, baseline - cap * 0.62f);
        float radius = item.size * 0.36f * k;
        for (int spoke = 0; spoke < 3; spoke++) {
            float theta = IM_PI * 0.5f + spoke * IM_PI / 3.0f;
            ImVec2 arm(cosf(theta) * radius, sinf(theta) * radius);
            draw->AddLine(middle - arm, middle + arm, colour, item.size * 0.1f * k);
        }
        return;
    }
    if (strcmp(item.text, "^") == 0) {
        ImVec2 middle(cx, baseline - cap * 0.62f);
        float s = item.size * k;
        ImVec2 caret[3] = { middle + ImVec2(-0.24f, 0.22f) * s, middle + ImVec2(0, -0.24f) * s, middle + ImVec2(0.24f, 0.22f) * s };
        draw->AddPolyline(caret, 3, colour, 0, 0.075f * s);
        return;
    }
    if (strcmp(item.text, "¨") == 0) {
        for (float dx : { -3.2f, 3.2f }) draw->AddCircleFilled(ImVec2(cx + dx * k, baseline - cap * 0.55f), 1.1f * k, colour, 12);
        return;
    }
    const char *text = strcmp(item.text, "-") == 0 ? "−" : item.text;
    label_on_baseline(draw, font, size, text, cx, baseline, colour, KB_LEGEND_STRETCH);
}

void draw_keybed(ImDrawList *draw, const KeyboardFrame &frame, bool wear) {
    float k = frame.kbu;
    float left = -KB_PAD_X, top = -KB_PAD_TOP, right = KB_WIDTH + KB_PAD_X, bottom_edge = KB_HEIGHT + KB_PAD_BOTTOM;
    Shape body = rounded_box(frame.at(left - 3, top - 3), frame.at(right + 3, bottom_edge + KB_FRONT_DEPTH), (KB_TOP_RADIUS + 3) * k, (KB_BOTTOM_RADIUS + 4) * k);
    fill(draw, translated(body, ImVec2(0, 4 * k)), IM_COL32(0, 0, 0, 80), IM_COL32(0, 0, 0, 120));
    fill(draw, body, KB_FRONT_TOP, KB_FRONT_BOTTOM);

    Shape face = rounded_box(frame.at(left, top), frame.at(right, bottom_edge), KB_TOP_RADIUS * k, KB_BOTTOM_RADIUS * k);
    Shape seam;
    float bottom = frame.at(0, bottom_edge).y;
    for (const ImVec2 &point : face) {
        if (point.y > bottom - KB_BOTTOM_RADIUS * k - 0.01f) seam.push_back(point + ImVec2(0, KB_FRONT_SEAM * k));
    }
    std::sort(seam.begin(), seam.end(), [](ImVec2 a, ImVec2 b) { return a.x < b.x; });
    draw->AddPolyline(seam.data(), (int)seam.size(), KB_SEAM_DARK, 0, 0.8f * k);
    Shape seam_light = translated(seam, ImVec2(0, 0.8f * k));
    draw->AddPolyline(seam_light.data(), (int)seam_light.size(), faded(KB_SEAM_LIGHT, 0.6f), 0, 0.6f * k);

    fill(draw, face, lighten(KB_KEYBED, 10), mix(KB_KEYBED, IM_COL32(156, 164, 166, 255), 0.35f));
    if (grime_texture) {
        ImRect box = bounds(face);
        draw->AddImageRounded((ImTextureID)(intptr_t)grime_texture, box.Min, box.Max, ImVec2(0, 0), ImVec2(1, 1),
                              IM_COL32_WHITE, KB_TOP_RADIUS * k);
        if (wear) case_scratches(draw, box.Min, box.Max, KB_TOP_RADIUS * k, 0.45f);
    }
    const int lip_rings = 10;
    std::vector<float> offsets = { 0.6f * k };
    for (int ring = 0; ring <= lip_rings; ring++) offsets.push_back(-KB_LIP * k * ring / lip_rings);
    ImU32 front_edge = mix(KB_FRONT_TOP, IM_COL32(120, 134, 142, 255), 0.2f);
    ImU32 side_edge = mix(KB_KEYBED, IM_COL32(132, 142, 148, 255), 0.6f);
    ring_mesh(draw, face, offsets, [&](ImVec2 normal, int ring) {
        float facing_front = smoothstep(normal.y);
        ImU32 edge = mix(mix(KB_KEYBED, side_edge, fabsf(normal.x)), front_edge, facing_front);
        if (normal.y < 0) edge = mix(KB_KEYBED, BEZEL_LIGHT, -normal.y * 0.22f);
        if (ring == 0) return edge & ~IM_COL32_A_MASK;
        float t = (float)(ring - 1) / lip_rings;
        ImU32 colour = mix(edge, KB_KEYBED, smoothstep(t));
        float highlight = expf(-powf((t - 0.28f) / 0.12f, 2.0f)) * facing_front * 0.25f;
        colour = mix(colour, KB_HIGHLIGHT, highlight);
        return faded(colour, 1.0f - smoothstep((t - 0.7f) / 0.3f));
    });

    float finger_y = KB_FINGER_Y - KB_HEIGHT + bottom_edge;
    draw_recess(draw, pill(frame.at(KB_FINGER_X - KB_FINGER_W * 0.5f, finger_y - KB_FINGER_H * 0.5f),
                           frame.at(KB_FINGER_X + KB_FINGER_W * 0.5f, finger_y + KB_FINGER_H * 0.5f)),
                SCOOP_RECESS, k, Mask(), FINGER_SCOOP);
    {
        ImVec2 centre = frame.at(KB_WELL_X, KB_WELL_Y);
        ImU32 shell = mix(lighten(KB_KEYBED, 10), mix(KB_KEYBED, IM_COL32(156, 164, 166, 255), 0.35f), 0.8f);
        draw_slope_ring(draw, centre, KB_WELL_R * k, KB_WELL_SLOPE * k, shell);
    }
}

const int KEYBOARD_KEY_COUNT = (int)(sizeof keyboard_keys / sizeof keyboard_keys[0]);

void input_keyboard(const KeyboardFrame &frame, DeviceState &state, uint8_t *down) {
    float k = frame.kbu;
    bool live = state.powered;
    int cursor_index = 0;
    for (int index = 0; index < KEYBOARD_KEY_COUNT; index++) {
        const KeyboardKey &key = keyboard_keys[index];
        Shape shape = keyboard_key_shape(frame, key);
        if (state.touch) hit_pad = key.shape == KB_SHAPE_CURSOR ? ImVec2(4.0f, 4.0f) * k : ImVec2(9.0f, 5.5f) * k;
        else hit_pad = ImVec2(0, 0);
        char id[32];
        snprintf(id, sizeof id, "kb-%s", key.id);
        bool pressed;
        bool activated = hit(id, shape, pressed);
        bool raw = live && runtime_keyboard_key(key.id, pressed);
        if (raw) {
        } else if (key.shape == KB_SHAPE_CURSOR) {
            KeyRepeat &repeat = keyboard_repeats[cursor_index++ % 4];
            uint8_t select_mods = state.select ? HOST_MOD_SHIFT : 0;
            if (!(live && second_arrow(key.code, select_mods, activated, state))) {
                repeat_key(repeat, key.code, activated && live, pressed && live, select_mods);
            }
        } else if (activated && live) {
            keyboard_press(key, state);
        }
        bool latched = raw ? runtime_keyboard_latched(key.id) : ((key.action == KB_ACTION_SECOND && state.second) ||
                                (key.action == KB_ACTION_SHIFT && strcmp(key.id, "shift_right") == 0 && state.select) ||
                                (key.action == KB_ACTION_SHIFT && (state.shift || state.caps)));
        down[index] = pressed || latched;
    }
}

void paint_keyboard(ImDrawList *draw, const KeyboardFrame &frame, float u, const DeviceState &state, const uint8_t *down) {
    float k = frame.kbu;
    draw_keybed(draw, frame, state.wear);
    for (const KeyboardKey &key : keyboard_keys) {
        if (!key.ring) continue;
        Shape ring = outset(keyboard_key_shape(frame, key), 1.6f * u + 4.6f * k + 0.75f * k);
        draw->AddPolyline(ring.data(), (int)ring.size(), faded(KB_RING, 0.95f), ImDrawFlags_Closed, 1.5f * k);
    }

    for (const KeyboardKey &key : keyboard_keys) {
        if (key.shape == KB_SHAPE_CURSOR) {
            Shape seat = keyboard_key_shape(frame, key);
            for (int ring = 4; ring >= 1; ring--) {
                int alpha = (int)((state.wear ? 34 : 24) * (1.0f - ring * 0.18f));
                fill(draw, translated(outset(seat, ring * 1.0f * k), ImVec2(0, 1.2f * k)), IM_COL32(20, 26, 32, alpha), IM_COL32(20, 26, 32, alpha));
            }
            continue;
        }
        draw_recess(draw, outset(keyboard_key_shape(frame, key), 2.0f * k), KEY_HOLE, k, Mask(), KEY_HOLE_PALETTE);
    }
    for (const KeyboardKey &key : keyboard_keys) finger_grime(draw, keyboard_key_shape(frame, key), key.wear, k);

    for (const KeyboardKey &key : keyboard_keys) {
        for (int index = 0; index < key.secondary_count; index++) keyboard_secondary(draw, frame, key, key.secondary[index]);
    }

    for (int index = 0; index < KEYBOARD_KEY_COUNT; index++) {
        const KeyboardKey &key = keyboard_keys[index];
        Shape shape = keyboard_key_shape(frame, key);
        bool key_down = down[index];
        ButtonStyle style = { KB_KEY_TOP[key.colour], KB_KEY_BOTTOM[key.colour], key.colour == KB_LIGHT ? 60 : 34, 1.6f, 1.0f,
                              0.0f };
        float lift = key_down ? 0.0f : KEY_TRAVEL * k;
        fill(draw, outset(shape, style.gap * u), IM_COL32(16, 20, 24, 215), IM_COL32(16, 20, 24, 170));
        style.gap = 0;
        if (lift > 0) {
            ImU32 side_top = mix(style.bottom, IM_COL32(0, 0, 0, 255), 0.25f);
            ImU32 side_bottom = mix(style.bottom, IM_COL32(0, 0, 0, 255), 0.45f);
            const int slices = 6;
            for (int slice = 0; slice <= slices; slice++) {
                float t = (float)slice / slices;
                fill(draw, translated(shape, ImVec2(0, -lift * t)), mix(side_bottom, side_top, t), side_bottom);
            }
        }
        {
            Shape body = translated(shape, ImVec2(0, -lift));
            ImU32 side_top = mix(style.bottom, IM_COL32(0, 0, 0, 255), 0.25f);
            ImU32 lit = lighten(style.top, style.rim / 2);
            const int rings = 7;
            float shoulder = KEY_SHOULDER * k;
            for (int ring = 0; ring <= rings; ring++) {
                float t = smoothstep((float)ring / rings);
                Shape layer = ring == 0 ? body : inset(body, shoulder * ring / rings);
                fill(draw, layer, mix(mix(side_top, lit, 0.7f), style.top, t), mix(side_top, style.bottom, t));
            }
        }
        rub_mode = false;
        ImVec2 dip = ImVec2(0, -lift);
        ImVec2 centre = frame.at(key.x, key.y) + dip;
        ImU32 face = mix(style.top, style.bottom, 0.5f);
        if (key.homing) {
            Shape bar = pill(centre + ImVec2(-6.75f, 12.5f - 1.8f) * k, centre + ImVec2(6.75f, 12.5f + 1.8f) * k);
            ImU32 ridge_side = mix(style.bottom, IM_COL32(0, 0, 0, 255), 0.18f);
            for (int step = 3; step >= 1; step--) fill(draw, translated(bar, ImVec2(0, step * 0.35f * k)), ridge_side, ridge_side);
            fill(draw, bar, lighten(style.top, 6), mix(style.top, style.bottom, 0.45f));
        }
        ImU32 legend_colour = faded(KB_KEY_LEGEND[key.colour], KB_LEGEND_ALPHA);
        ImVec2 legend_centre = centre + ImVec2(0, key.legend_dy * k);
        ImRect legend_box(legend_centre - ImVec2(12, 9) * k, legend_centre + ImVec2(12, 9) * k);
        if (key.icon == KB_ICON_NONE && key.legend) {
            legend_box = centred_legend(draw, keyboard_font(false), key.legend_size * k, key.legend, legend_centre, legend_colour,
                                        KB_LEGEND_STRETCH * key.stretch);
        } else {
            keyboard_icon(draw, frame, key, key.icon == KB_ICON_TRIANGLE ? centre : legend_centre, legend_colour);
        }
        if (state.wear) rub_patch(draw, legend_box.Min, legend_box.Max, face, key.wear, (uint32_t)(key.x * 31 + key.y * 17));
    }

}

float lid_width_units() {
    return LEFT_EXTENT + RIGHT_EXTENT + REFERENCE_LCD_H * GRID_W / GRID_H;
}

float keyboard_unit_in_lid_units() {
    return lid_width_units() * KB_WIDTH_RATIO / (KB_WIDTH + 2 * KB_PAD_X);
}

float keyboard_height_in_lid_units() {
    return HINGE + (KB_PAD_TOP + KB_HEIGHT + KB_PAD_BOTTOM + KB_FRONT_DEPTH + KB_BOTTOM_MARGIN) * keyboard_unit_in_lid_units();
}

void draw_hinge(ImDrawList *draw, ImVec2 device_min, ImVec2 device_max, float u) {
    float width = (device_max.x - device_min.x) * 0.9f;
    float centre = (device_min.x + device_max.x) * 0.5f;
    ImVec2 a(centre - width * 0.5f, device_max.y - 14 * u), b(centre + width * 0.5f, device_max.y + HINGE * u + 10 * u);
    Shape barrel = pill(a, b);
    fill(draw, translated(barrel, ImVec2(0, 3 * u)), IM_COL32(0, 0, 0, 70), IM_COL32(0, 0, 0, 70));
    fill(draw, barrel, IM_COL32(150, 158, 164, 255), IM_COL32(96, 104, 110, 255));
    Shape shine = pill(ImVec2(a.x + 20 * u, device_max.y + 2 * u), ImVec2(b.x - 20 * u, device_max.y + 10 * u));
    fill(draw, shine, IM_COL32(220, 226, 230, 90), IM_COL32(220, 226, 230, 0));
}


struct DeviceLayout {
    ImVec2 device_min, device_max, image_min, image_max;
    float u;
    float rounding;
    bool has_keyboard;
};

struct BakeKey {
    ImVec2 origin, size;
    float scale;
    bool show_keys, has_keyboard, wear, focused;
    std::vector<uint8_t> down;
    std::string model;

    bool operator==(const BakeKey &other) const {
        return origin.x == other.origin.x && origin.y == other.origin.y && size.x == other.size.x && size.y == other.size.y &&
               scale == other.scale && show_keys == other.show_keys && has_keyboard == other.has_keyboard && wear == other.wear &&
               focused == other.focused && down == other.down && model == other.model;
    }
};

SDL_Texture *bake_texture = nullptr;
ImDrawList *bake_list = nullptr;
BakeKey baked, pending;
bool bake_pending = false;
ImVec2 bake_min, bake_size;
float bake_scale = 1.0f;

KeyboardFrame keyboard_frame(const DeviceLayout &layout) {
    float kbu = keyboard_unit_in_lid_units() * layout.u;
    ImVec2 origin((layout.device_min.x + layout.device_max.x) * 0.5f - KB_WIDTH * kbu * 0.5f, layout.device_max.y + HINGE * layout.u + KB_PAD_TOP * kbu);
    return KeyboardFrame{ origin, kbu };
}

void paint_device(ImDrawList *draw, SDL_Renderer *renderer, float framebuffer_scale, const DeviceLayout &layout, const DeviceState &state,
                  const uint8_t *down) {
    ImVec2 device_min = layout.device_min, device_max = layout.device_max, image_min = layout.image_min, image_max = layout.image_max;
    ImVec2 device_size = device_max - device_min;
    float u = layout.u, rounding = layout.rounding;
    if (layout.has_keyboard) draw_hinge(draw, device_min, device_max, u);
    if (state.focused) {
        draw->AddRect(device_min - ImVec2(3, 3), device_max + ImVec2(3, 3), IM_COL32(90, 200, 180, 160), rounding + 4, 0, 2.0f);
    }
    draw->AddRectFilled(device_min + ImVec2(0, 4), device_max + ImVec2(0, 4), IM_COL32(0, 0, 0, 90), rounding);
    if (state.show_keys) shade_body(draw, device_min, device_max, rounding, image_min, image_max, u);
    else draw->AddRectFilled(device_min, device_max, BEZEL, rounding);
    build_grime(renderer, (int)(device_size.x * framebuffer_scale), (int)(device_size.y * framebuffer_scale), framebuffer_scale * u, state.wear);
    wear_labels = state.wear;
    wear_grime = state.wear;
    draw->AddImageRounded((ImTextureID)(intptr_t)grime_texture, device_min, device_max, ImVec2(0, 0), ImVec2(1, 1),
                          IM_COL32_WHITE, rounding);
    load_scratches(renderer);
    if (state.wear) case_scratches(draw, device_min, device_max, rounding, 0.0f);
    if (!state.show_keys) draw->AddRectFilledMultiColor(device_min + ImVec2(rounding, 2), ImVec2(device_max.x - rounding, device_min.y + device_size.y * 0.45f),
                                  IM_COL32(255, 255, 255, 40), IM_COL32(255, 255, 255, 40), IM_COL32(255, 255, 255, 0), IM_COL32(255, 255, 255, 0));
    draw->AddRect(device_min, device_max, BEZEL_EDGE, rounding, 0, 2.0f);
    draw->AddRect(device_min + ImVec2(2, 2), device_max - ImVec2(2, 2), BEZEL_LIGHT, rounding - 2, 0, 1.0f);

    if (state.show_keys) {
        ImVec2 frame_min = image_min - ImVec2(20, 18) * u, frame_max = image_max + ImVec2(20, 20) * u;
        draw->AddRectFilled(frame_min, frame_max, FRAME, 12.0f * u);
        draw->AddRect(frame_min, frame_max, BEZEL_LIGHT, 12.0f * u, 0, 1.5f);
        draw->AddRect(frame_min + ImVec2(1, 1), frame_max + ImVec2(1, 1), BEZEL_EDGE, 12.0f * u, 0, 1.0f);
    }
    draw->AddRectFilled(image_min - ImVec2(5, 5), image_max + ImVec2(5, 5), IM_COL32(58, 64, 68, 255), 5.0f);
    draw->AddRect(image_min - ImVec2(5, 5), image_max + ImVec2(5, 5), IM_COL32(210, 216, 220, 255), 5.0f, 0, 1.0f);
    if (state.show_keys) {
        float brand = 24.0f * u;
        ImVec2 at = image_min + ImVec2(-4 * u, -50 * u);
        erase_colour = faded(BEZEL, 0.9f);
        rub_mode = false;
        draw->AddText(text_font(), brand, at, PRINT, "SHAM");
        wear_patch(draw, at, at + text_size(text_font(), brand, "SHAM"), 1);
        draw->AddText(ImGui::GetFont(), 17.0f * u, at + ImVec2(text_size(text_font(), brand, "SHAM").x + 18 * u, 5 * u), PRINT, model_name.c_str());
        paint_lid_keys(draw, Frame{ image_min, image_max, u }, device_min, device_max, down);
        if (layout.has_keyboard) paint_keyboard(draw, keyboard_frame(layout), u, state, down + LID_KEY_COUNT);
    } else {
        draw->AddText(device_min + ImVec2(PLAIN_BEZEL, 8), IM_COL32(60, 66, 72, 255), ("SHAM  " + model_name).c_str());
    }

}


}

float device_fit_height(float width, const DeviceState &state) {
    bool show_keys = state.show_keys, show_keyboard = state.show_keyboard;
    float usable = width - 16.0f;
    if (state.screen_only) return (usable - 2 * SCREEN_MARGIN) * GRID_H / GRID_W + 2 * SCREEN_MARGIN + 28.0f;
    if (show_keys) {
        float image_h = usable / (GRID_W / GRID_H + (LEFT_EXTENT + RIGHT_EXTENT) / REFERENCE_LCD_H);
        float lid = REFERENCE_LCD_H + TOP_EXTENT + BOTTOM_EXTENT + (show_keyboard ? keyboard_height_in_lid_units() : 0.0f);
        return image_h * lid / REFERENCE_LCD_H + 24.0f;
    }
    float lcd_w = usable - 2 * PLAIN_BEZEL;
    return lcd_w * GRID_H / GRID_W + 2 * PLAIN_BEZEL + 28.0f;
}

float device_draw(SDL_Renderer *renderer, float framebuffer_scale, float height, float compose_seconds, DeviceState &state) {
    ImVec2 origin = ImGui::GetCursorScreenPos();
    float avail_w = ImGui::GetContentRegionAvail().x;
    if (state.screen_only) {
        float fit_h = std::min((avail_w - 2 * SCREEN_MARGIN) * GRID_H / GRID_W, height - 2 * SCREEN_MARGIN);
        int cell = std::max(2, (int)floorf(fit_h * framebuffer_scale / GRID_H));
        lcd_compose_setup(cell);
        upload_lcd(renderer, compose_seconds);
        ImVec2 size(cell * GRID_W / framebuffer_scale, cell * GRID_H / framebuffer_scale);
        ImVec2 min = origin + ImVec2((avail_w - size.x) * 0.5f, (height - size.y) * 0.5f);
        ImDrawList *draw = ImGui::GetWindowDrawList();
        if (state.focused) draw->AddRect(min - ImVec2(3, 3), min + size + ImVec2(3, 3), IM_COL32(90, 200, 180, 160), 4.0f, 0, 2.0f);
        if (lcd_texture) draw->AddImage((ImTextureID)(intptr_t)lcd_texture, min, min + size);
        load_scratches(renderer);
        if (scratch_texture && state.scratches) {
            float band = std::min(1.0f, scratch_w / (GRID_W / GRID_H) / scratch_h);
            draw->AddImage((ImTextureID)(intptr_t)scratch_texture, min, min + size, ImVec2(0, 0.5f - band * 0.5f),
                           ImVec2(1, 0.5f + band * 0.5f), SCRATCH_TINT);
        }
        ImGui::SetCursorScreenPos(min);
        ImGui::InvisibleButton("device", size);
        ImGui::SetCursorScreenPos(origin + ImVec2(0, height));
        ImGui::Dummy(ImVec2(0, 0));
        return size.y;
    }

    float image_h;
    bool has_keyboard = state.show_keys && state.show_keyboard;
    float extra_units = has_keyboard ? keyboard_height_in_lid_units() : 0.0f;
    if (state.show_keys) {
        float by_width = avail_w / (GRID_W / GRID_H + (LEFT_EXTENT + RIGHT_EXTENT) / REFERENCE_LCD_H);
        float by_height = height / (1.0f + (TOP_EXTENT + BOTTOM_EXTENT + extra_units) / REFERENCE_LCD_H);
        image_h = std::min(by_width, by_height);
    } else {
        image_h = std::min((avail_w - 2 * PLAIN_BEZEL) * GRID_H / GRID_W, height - 2 * PLAIN_BEZEL);
    }
    int cell = std::max(2, (int)floorf(image_h * framebuffer_scale / GRID_H));
    lcd_compose_setup(cell);
    upload_lcd(renderer, compose_seconds);

    ImVec2 image_size(cell * GRID_W / framebuffer_scale, cell * GRID_H / framebuffer_scale);
    float u = image_size.y / REFERENCE_LCD_H;
    ImVec2 pad_min = state.show_keys ? ImVec2(LEFT_EXTENT, TOP_EXTENT) * u : ImVec2(PLAIN_BEZEL, PLAIN_BEZEL);
    ImVec2 pad_max = state.show_keys ? ImVec2(RIGHT_EXTENT, BOTTOM_EXTENT) * u : ImVec2(PLAIN_BEZEL, PLAIN_BEZEL);
    ImVec2 device_size = image_size + pad_min + pad_max;
    float total_height = device_size.y + extra_units * u;
    ImVec2 device_min = origin + ImVec2((avail_w - device_size.x) * 0.5f, (height - total_height) * 0.5f);
    ImVec2 device_max = device_min + device_size;
    ImVec2 image_min = device_min + pad_min;
    ImVec2 image_max = image_min + image_size;
    float rounding = state.show_keys ? 34.0f * u : 18.0f;

    DeviceLayout layout = { device_min, device_max, image_min, image_max, u, rounding, has_keyboard };
    ImGui::SetCursorScreenPos(image_min);
    ImGui::InvisibleButton("device", image_size);
    std::vector<uint8_t> down(LID_KEY_COUNT + KEYBOARD_KEY_COUNT, 0);
    if (state.show_keys) {
        input_lid_keys(Frame{ image_min, image_max, u }, state, down.data());
        if (has_keyboard) input_keyboard(keyboard_frame(layout), state, down.data() + LID_KEY_COUNT);
    }

    BakeKey key = { origin, ImVec2(avail_w, height), framebuffer_scale, state.show_keys, has_keyboard, state.wear, state.focused, down, model_name };
    ImDrawList *draw = ImGui::GetWindowDrawList();
    if (bake_texture && !bake_pending && key == baked) {
        draw->AddImage((ImTextureID)(intptr_t)bake_texture, bake_min, bake_min + ImVec2((float)bake_texture->w, (float)bake_texture->h) / bake_scale);
    } else {
        paint_device(draw, renderer, framebuffer_scale, layout, state, down.data());
        if (!bake_list) bake_list = IM_NEW(ImDrawList)(ImGui::GetDrawListSharedData());
        bake_list->_ResetForNewFrame();
        bake_list->Flags = draw->Flags;
        bake_list->_SetPixelDensity(draw->_InvFringeScale);
        bake_list->PushClipRect(ImVec2(floorf(origin.x * framebuffer_scale), floorf(origin.y * framebuffer_scale)) / framebuffer_scale, origin + ImVec2(avail_w, height));
        bake_list->PushTexture(ImGui::GetIO().Fonts->TexRef);
        paint_device(bake_list, renderer, framebuffer_scale, layout, state, down.data());
        bake_min = ImVec2(floorf(origin.x * framebuffer_scale), floorf(origin.y * framebuffer_scale)) / framebuffer_scale;
        bake_size = origin + ImVec2(avail_w, height) - bake_min;
        bake_scale = framebuffer_scale;
        pending = key;
        bake_pending = true;
    }

    if (lcd_texture) draw->AddImage((ImTextureID)(intptr_t)lcd_texture, image_min, image_max);
    load_scratches(renderer);
    if (scratch_texture && state.scratches) {
        float band = std::min(1.0f, scratch_w / (GRID_W / GRID_H) / scratch_h);
        draw->AddImage((ImTextureID)(intptr_t)scratch_texture, image_min, image_max, ImVec2(0, 0.5f - band * 0.5f),
                       ImVec2(1, 0.5f + band * 0.5f), SCRATCH_TINT);
    }

    ImGui::SetCursorScreenPos(origin + ImVec2(0, height));
    ImGui::Dummy(ImVec2(0, 0));
    return total_height;
}

void device_flush_bake(SDL_Renderer *renderer) {
    if (!bake_pending || !bake_list) return;
    bake_pending = false;
    int width = (int)ceilf(bake_size.x * bake_scale), height = (int)ceilf(bake_size.y * bake_scale);
    if (width <= 0 || height <= 0) return;
    if (!bake_texture || bake_texture->w != width || bake_texture->h != height) {
        if (bake_texture) SDL_DestroyTexture(bake_texture);
        bake_texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_TARGET, width, height);
        if (!bake_texture) return;
        SDL_SetTextureScaleMode(bake_texture, SDL_SCALEMODE_NEAREST);
    }
    for (ImDrawVert &vertex : bake_list->VtxBuffer) vertex.pos -= bake_min;
    for (ImDrawCmd &command : bake_list->CmdBuffer) command.ClipRect -= ImVec4(bake_min.x, bake_min.y, bake_min.x, bake_min.y);

    ImDrawData data;
    data.Valid = true;
    data.CmdLists.push_back(bake_list);
    data.CmdListsCount = 1;
    data.TotalVtxCount = bake_list->VtxBuffer.Size;
    data.TotalIdxCount = bake_list->IdxBuffer.Size;
    data.DisplayPos = ImVec2(0, 0);
    data.DisplaySize = bake_size;
    data.FramebufferScale = ImVec2(bake_scale, bake_scale);

    SDL_Texture *previous = SDL_GetRenderTarget(renderer);
    SDL_SetRenderTarget(renderer, bake_texture);
    SDL_SetRenderScale(renderer, bake_scale, bake_scale);
    ImVec4 background = ImGui::GetStyleColorVec4(ImGuiCol_WindowBg);
    SDL_SetRenderDrawColorFloat(renderer, background.x, background.y, background.z, 1.0f);
    SDL_RenderClear(renderer);
    ImGui_ImplSDLRenderer3_RenderDrawData(&data, renderer);
    SDL_SetRenderTarget(renderer, previous);
    baked = pending;
}

void device_set_model(const char *model) {
    model_name = model;
}

void device_set_label_font(ImFont *font) {
    label_font = font;
}

void device_set_keyboard_fonts(ImFont *legend, ImFont *label) {
    kb_legend_font = legend;
    kb_label_font = label;
}

void device_set_icon_font(ImFont *font) {
    icon_font = font;
}

void device_shutdown(void) {
    if (lcd_texture) SDL_DestroyTexture(lcd_texture);
    lcd_texture = nullptr;
    if (grime_texture) SDL_DestroyTexture(grime_texture);
    grime_texture = nullptr;
    if (scratch_texture) SDL_DestroyTexture(scratch_texture);
    scratch_texture = nullptr;
    if (bake_texture) SDL_DestroyTexture(bake_texture);
    bake_texture = nullptr;
    if (bake_list) IM_DELETE(bake_list);
    bake_list = nullptr;
}
