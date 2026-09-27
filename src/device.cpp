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
#include "lid_layout.h"
#include "keys.h"
#include "runtime.h"
#include "lcd.h"

namespace {

using Shape = std::vector<ImVec2>;

static_assert(LID_LCD_MARGIN_X == LCD_MARGIN_X && LID_LCD_MARGIN_Y == LCD_MARGIN_Y, "make lid and lcd.h disagree on the LCD margin");
const float GRID_W = LCD_WIDTH + 2 * LCD_MARGIN_X;
const float GRID_H = LCD_HEIGHT + 2 * LCD_MARGIN_Y;
const float REFERENCE_LCD_H = 282.0f;
const float LEFT_EXTENT = LID_LEFT_EXTENT;
const float RIGHT_EXTENT = LID_RIGHT_EXTENT;
const float TOP_EXTENT = LID_TOP_EXTENT;
const float BOTTOM_EXTENT = LID_BOTTOM_EXTENT;
const float PLAIN_BEZEL = 30.0f;
const float SCREEN_MARGIN = 8.0f;
const float DEVICE_MARGIN = 8.0f;
const ImU32 SCRATCH_TINT = IM_COL32(214, 232, 224, 120);
const ImU32 CASE_SCRATCH_TINT = IM_COL32(246, 249, 251, 150);
const float WELL_MARGIN = 4.0f;
const uint64_t REPEAT_DELAY_MS = 400;
const uint64_t REPEAT_RATE_MS = 80;

const ImU32 BEZEL = IM_COL32(178, 189, 199, 255);
const ImU32 BEZEL_EDGE = IM_COL32(112, 120, 126, 255);
const ImU32 BEZEL_LIGHT = IM_COL32(226, 232, 236, 255);
const ImU32 FRAME = IM_COL32(192, 203, 213, 255);
const ImU32 TEAL_ICON = IM_COL32(176, 196, 196, 255);
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

    ImVec2 lid(float x, float y) const {
        return ImVec2((lcd_min.x + lcd_max.x) * 0.5f + x * u, lcd_min.y + y * u);
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

float cross(ImVec2 o, ImVec2 a, ImVec2 b) {
    return (a.x - o.x) * (b.y - o.y) - (a.y - o.y) * (b.x - o.x);
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
const float KEY_HOLE_GAP = 2.2f;
const float HOMING_DROP = 0.4f;
const float KEY_REFERENCE_H = 36.6f;
const float KEYCAP_LEGEND_ALPHA = 0.76f;
const float HOMING_HALF_W = 8.4f;
const float HOMING_HALF_H = 2.25f;
const float KEY_HOLE_EDGE_WIDTH = 1.1f;
const ImU32 KEY_HOLE_DARK = IM_COL32(14, 16, 18, 255);
const ImU32 KEY_HOLE_EDGE = IM_COL32(236, 240, 242, 230);
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

std::vector<int> ear_clip(const Shape &polygon) {
    std::vector<int> result;
    std::vector<int> indices;
    for (int i = 0; i < (int)polygon.size(); i++) indices.push_back(i);
    float orientation = signed_area(polygon) > 0 ? 1.0f : -1.0f;
    auto turn = [&](ImVec2 a, ImVec2 b, ImVec2 c) { return ((b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x)) * orientation; };
    while (indices.size() > 3) {
        int count = (int)indices.size();
        int ear = -1;
        for (int k = 0; k < count && ear < 0; k++) {
            ImVec2 a = polygon[indices[(k + count - 1) % count]], b = polygon[indices[k]], c = polygon[indices[(k + 1) % count]];
            if (turn(a, b, c) <= 0) continue;
            bool empty = true;
            for (int j = 0; j < count && empty; j++) {
                int other = indices[j];
                if (j == k || j == (k + 1) % count || j == (k + count - 1) % count) continue;
                ImVec2 p = polygon[other];
                if ((p.x == a.x && p.y == a.y) || (p.x == b.x && p.y == b.y) || (p.x == c.x && p.y == c.y)) continue;
                empty = !(turn(a, b, p) >= 0 && turn(b, c, p) >= 0 && turn(c, a, p) >= 0);
            }
            if (empty) ear = k;
        }
        if (ear < 0) ear = 0;
        result.push_back(indices[(ear + count - 1) % count]);
        result.push_back(indices[ear]);
        result.push_back(indices[(ear + 1) % count]);
        indices.erase(indices.begin() + ear);
    }
    if (indices.size() == 3) result.insert(result.end(), indices.begin(), indices.end());
    return result;
}

void fill_polygon(ImDrawList *draw, const Shape &polygon, ImU32 colour, const std::vector<int> &indices = {}) {
    std::vector<int> triangles = indices.empty() ? ear_clip(polygon) : indices;
    draw->PrimReserve((int)triangles.size(), (int)polygon.size());
    ImVec2 uv = draw->_Data->TexUvWhitePixel;
    ImDrawIdx base = (ImDrawIdx)draw->_VtxCurrentIdx;
    for (const ImVec2 &point : polygon) draw->PrimWriteVtx(point, uv, colour);
    for (int index : triangles) draw->PrimWriteIdx((ImDrawIdx)(base + index));
    Shape edge = polygon;
    draw->AddPolyline(edge.data(), (int)edge.size(), colour, ImDrawFlags_Closed, 0.8f);
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
    Shape floor;
    for (size_t i = 0; i < count; i++) {
        ImVec2 point = shape[i] - normals[i] * inner;
        floor.push_back(point);
        draw->PrimWriteVtx(point, uv, shaded(floor_colour(point), point));
    }
    std::vector<int> triangles = ear_clip(floor);
    for (int index : triangles) draw->PrimWriteIdx((ImDrawIdx)(centre + index));
    for (size_t i = triangles.size(); i < (count - 2) * 3; i++) draw->PrimWriteIdx(centre);
}

float bezel_brightness(float x, const ImVec2 &lcd_min, const ImVec2 &lcd_max, float u) {
    const float left = LEFT_EXTENT, right = RIGHT_EXTENT;
    const float left_profile[][2] = { { -left, 0.80f }, { -left + 10, 0.98f }, { -left + 20, 1.07f }, { -left * 0.74f, 1.03f },
                                      { -left * 0.30f, 0.97f }, { 0, 0.94f } };
    const float right_profile[][2] = { { 0, 0.94f }, { right * 0.28f, 0.97f }, { right * 0.79f, 1.03f }, { right - 18, 1.07f },
                                       { right - 8, 0.98f }, { right, 0.80f } };
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

void shade_body(ImDrawList *draw, const Shape &body, const ImVec2 &lcd_min, const ImVec2 &lcd_max, float u) {
    ImRect box = bounds(body);
    ImVec2 device_min = box.Min, device_max = box.Max;
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

struct Region {
    ImRect box;
    float rounding;

    bool contains(ImVec2 point) const {
        if (!box.Contains(point)) return false;
        float radius = std::min(rounding, std::min(box.GetWidth(), box.GetHeight()) * 0.5f);
        ImVec2 nearest(ImClamp(point.x, box.Min.x + radius, box.Max.x - radius), ImClamp(point.y, box.Min.y + radius, box.Max.y - radius));
        return ImLengthSqr(point - nearest) <= radius * radius;
    }
};

std::vector<Region> case_regions;
std::vector<ImRect> control_regions;

bool hit(const char *id, const Shape &shape, bool &pressed) {
    ImRect box = bounds(shape);
    box.Min -= hit_pad;
    box.Max += hit_pad;
    control_regions.push_back(box);
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

struct LidLayout {
    ImRect side_boxes[5];
    Shape side[5], flute[5];
    ImRect light_box;
    Shape light;
    ImVec2 menu_centre, esc_centre, enter_centre;
    Shape menu, esc, enter, power, up, down, power_flute, menu_well, arrow_well, esc_well;
    ImRect power_box;
};

std::vector<Shape> lid_regions(const Frame &frame, const LidShape &shape, ImVec2 offset = ImVec2(0, 0)) {
    std::vector<Shape> regions;
    const float *point = shape.points;
    for (int region = 0; region < shape.regions; region++) {
        Shape polygon;
        for (int i = 0; i < shape.counts[region]; i++, point += 2) polygon.push_back(frame.lid(point[0], point[1]) + offset);
        regions.push_back(polygon);
    }
    return regions;
}

Shape lid_shape(const Frame &frame, const LidShape &shape) {
    return lid_regions(frame, shape)[0];
}

std::vector<int> lid_indices(const LidShape &shape) {
    return std::vector<int>(shape.indices, shape.indices + shape.index_count);
}

bool point_in(const Shape &polygon, ImVec2 point) {
    bool inside = false;
    for (size_t i = 0, j = polygon.size() - 1; i < polygon.size(); j = i++) {
        const ImVec2 &a = polygon[i], &b = polygon[j];
        if ((a.y > point.y) != (b.y > point.y) && point.x < (b.x - a.x) * (point.y - a.y) / (b.y - a.y) + a.x) inside = !inside;
    }
    return inside;
}

void fringe(ImDrawList *draw, const Shape &contour, float direction, ImU32 colour) {
    size_t count = contour.size();
    float width = draw->_FringeScale;
    float orientation = signed_area(contour) > 0 ? 1.0f : -1.0f;
    ImU32 clear = colour & ~IM_COL32_A_MASK;
    draw->PrimReserve((int)count * 6, (int)count * 2);
    ImVec2 uv = draw->_Data->TexUvWhitePixel;
    ImDrawIdx base = (ImDrawIdx)draw->_VtxCurrentIdx;
    for (size_t i = 0; i < count; i++) {
        ImVec2 before = contour[(i + count - 1) % count], at = contour[i], after = contour[(i + 1) % count];
        ImVec2 n0(before.y - at.y, at.x - before.x), n1(at.y - after.y, after.x - at.x);
        float l0 = sqrtf(n0.x * n0.x + n0.y * n0.y), l1 = sqrtf(n1.x * n1.x + n1.y * n1.y);
        ImVec2 normal = (l0 > 0 ? n0 / l0 : ImVec2(0, 0)) + (l1 > 0 ? n1 / l1 : ImVec2(0, 0));
        float length = sqrtf(normal.x * normal.x + normal.y * normal.y);
        normal = length > 0 ? normal * (orientation * direction / length) : ImVec2(0, 0);
        draw->PrimWriteVtx(at, uv, colour);
        draw->PrimWriteVtx(at + normal * width, uv, clear);
    }
    for (size_t i = 0; i < count; i++) {
        ImDrawIdx a = (ImDrawIdx)(base + i * 2), b = (ImDrawIdx)(base + ((i + 1) % count) * 2);
        draw->PrimWriteIdx(a); draw->PrimWriteIdx(b); draw->PrimWriteIdx((ImDrawIdx)(b + 1));
        draw->PrimWriteIdx(a); draw->PrimWriteIdx((ImDrawIdx)(b + 1)); draw->PrimWriteIdx((ImDrawIdx)(a + 1));
    }
}

template <typename Map>
void fill_triangles(ImDrawList *draw, const float *triangles, int count, const std::vector<Shape> &contours, Map map, ImU32 colour) {
    draw->PrimReserve(count * 3, count * 3);
    ImVec2 uv = draw->_Data->TexUvWhitePixel;
    ImDrawIdx base = (ImDrawIdx)draw->_VtxCurrentIdx;
    for (int i = 0; i < count * 3; i++) {
        draw->PrimWriteVtx(map(triangles[i * 2], triangles[i * 2 + 1]), uv, colour);
        draw->PrimWriteIdx((ImDrawIdx)(base + i));
    }
    for (size_t i = 0; i < contours.size(); i++) {
        int depth = 0;
        for (size_t j = 0; j < contours.size(); j++) {
            if (j != i && point_in(contours[j], contours[i][0])) depth++;
        }
        fringe(draw, contours[i], depth % 2 ? -1.0f : 1.0f, colour);
    }
}

void fill_lid_icon(ImDrawList *draw, const Frame &frame, const LidShape &shape, ImVec2 offset, ImU32 colour) {
    fill_triangles(draw, shape.triangles, shape.triangle_count, lid_regions(frame, shape, offset),
                   [&](float x, float y) { return frame.lid(x, y) + offset; }, colour);
}

LidLayout lid_layout(const Frame &frame) {
    float u = frame.u;
    LidLayout lid;
    const LidShape *sides[] = { &LID_SHAPE_SIDE_0, &LID_SHAPE_SIDE_1, &LID_SHAPE_SIDE_2, &LID_SHAPE_SIDE_3, &LID_SHAPE_SIDE_4 };
    const LidShape *flutes[] = { &LID_SHAPE_FLUTE_0, &LID_SHAPE_FLUTE_1, &LID_SHAPE_FLUTE_2, &LID_SHAPE_FLUTE_3, &LID_SHAPE_FLUTE_4 };
    for (int index = 0; index < 5; index++) {
        lid.side[index] = lid_shape(frame, *sides[index]);
        lid.side_boxes[index] = bounds(lid.side[index]);
        lid.flute[index] = lid_shape(frame, *flutes[index]);
    }
    lid.light = lid_shape(frame, LID_SHAPE_LIGHT);
    lid.light_box = bounds(lid.light);
    lid.menu = lid_shape(frame, LID_SHAPE_MENU);
    lid.esc = lid_shape(frame, LID_SHAPE_ESC);
    lid.enter = lid_shape(frame, LID_SHAPE_ENTER);
    lid.menu_centre = bounds(lid.menu).GetCenter();
    lid.esc_centre = bounds(lid.esc).GetCenter();
    lid.enter_centre = bounds(lid.enter).GetCenter();
    lid.power = lid_shape(frame, LID_SHAPE_POWER);
    lid.power_box = bounds(lid.power);
    lid.power_flute = lid_shape(frame, LID_SHAPE_POWER_FLUTE);
    lid.menu_well = lid_shape(frame, LID_SHAPE_MENU_WELL);
    lid.up = lid_shape(frame, LID_SHAPE_UP);
    lid.down = lid_shape(frame, LID_SHAPE_DOWN);
    lid.arrow_well = lid_shape(frame, LID_SHAPE_ARROW_WELL);
    lid.esc_well = lid_shape(frame, LID_SHAPE_ESC_WELL);
    (void)u;
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
        ImRect scoop = bounds(lid.flute[index]);
        draw_recess(draw, lid.flute[index], FLUTE_RECESS, u, Mask{ scoop.Min.x, box.Min.x + box.GetHeight() * 0.2f });
    }
    {
        ImRect scoop = bounds(lid.power_flute);
        draw_recess(draw, lid.power_flute, FLUTE_RECESS, u, Mask{ scoop.Max.x, lid.power_box.Max.x - lid.power_box.GetHeight() * 0.2f });
    }
    draw_recess(draw, lid.arrow_well, KEY_WELL, u);
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
    fill_lid_icon(draw, frame, LID_SHAPE_LIGHT_ICON, dip(down[LID_LIGHT]), TEAL_ICON);

    erase_colour = faded(BEZEL, 0.9f);
    rub_mode = false;
    centred_text(draw, lid.menu_centre - ImVec2(0, 39 * u), 15.0f * u, PRINT, "MENU");
    draw_recess(draw, lid.menu_well, KEY_WELL, u);
    draw_key(draw, lid.menu, DARK_DOMED_KEY, down[LID_MENU], u);

    erase_colour = faded(BEZEL, 0.9f);
    rub_mode = false;
    centred_text(draw, ImVec2(lid.power_box.GetCenter().x, lid.menu_centre.y - 39 * u), 15.0f * u, PRINT, "POWER");
    draw_key(draw, lid.power, TEAL_KEY, down[LID_POWER], u);
    fill_lid_icon(draw, frame, LID_SHAPE_POWER_ICON, dip(down[LID_POWER]), TEAL_ICON);

    for (int index = 0; index < 2; index++) {
        const Shape &shape = index == 0 ? lid.up : lid.down;
        bool pressed = down[LID_UP + index];
        draw_key(draw, shape, BLUE_KEY, pressed, u);
        fill_lid_icon(draw, frame, index == 0 ? LID_SHAPE_UP_ICON : LID_SHAPE_DOWN_ICON, dip(pressed), IM_COL32(222, 228, 234, 235));
    }

    draw_recess(draw, lid.esc_well, FLAT_WELL, u);
    draw_key(draw, lid.esc, DARK_KEY, down[LID_ESC], u);
    centred_text(draw, lid.esc_centre + dip(down[LID_ESC]), 15.0f * u, LABEL, "ESC");
    draw_key(draw, lid.enter, DARK_KEY, down[LID_ENTER], u);
    centred_text(draw, lid.enter_centre + dip(down[LID_ENTER]), 16.0f * u, LABEL, "ENTER");
}

const float KEY_TRAVEL = 2.6f;
const float KEY_SHOULDER = 4.0f;
const float KB_SQUARE_CORNER = 0.22f;
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

void ink_extent(ImFont *font, float size, const char *text, float &ink_left, float &ink_right) {
    ImFontBaked *baked = font->GetFontBaked(size);
    float scale = size / baked->Size;
    float pen = 0;
    ink_left = FLT_MAX;
    ink_right = 0;
    for (const char *cursor = text; *cursor;) {
        unsigned codepoint = 0;
        cursor += ImTextCharFromUtf8(&codepoint, cursor, nullptr);
        ImFontGlyph *glyph = baked->FindGlyph((ImWchar)codepoint);
        if (!glyph) continue;
        if (glyph->Visible) {
            ink_left = std::min(ink_left, pen + glyph->X0 * scale);
            ink_right = std::max(ink_right, pen + glyph->X1 * scale);
        }
        pen += glyph->AdvanceX * scale;
    }
    if (ink_left > ink_right) ink_left = ink_right = 0;
}

ImRect centred_legend(ImDrawList *draw, ImFont *font, float size, const char *text, ImVec2 centre, ImU32 colour, float stretch) {
    unsigned first = 0;
    ImTextCharFromUtf8(&first, text, nullptr);
    bool own_glyph = text[1] == 0 || strcmp(text, "−") == 0;
    unsigned reference = own_glyph ? first : 'H';
    float middle = glyph_middle(font, size, reference);
    float ink_left, ink_right;
    ink_extent(font, size, text, ink_left, ink_right);
    float width = (ink_right - ink_left) * stretch;
    float left = centre.x - (ink_left + ink_right) * 0.5f * stretch;
    float top = centre.y - middle;
    stretched_text(draw, font, size, text, left, top, colour, stretch);
    float half_height = glyph_middle(font, size, 'H') - font->GetFontBaked(size)->FindGlyph('H')->Y0 * size / font->GetFontBaked(size)->Size;
    return ImRect(centre.x - width * 0.5f, centre.y - half_height, centre.x + width * 0.5f, centre.y + half_height);
}

void label_on_baseline(ImDrawList *draw, ImFont *font, float size, const char *text, float cx, float baseline, ImU32 colour, float stretch) {
    float bottom = 0;
    glyph_middle(font, size, 'H', &bottom);
    float width = stretched_width(font, size, text, stretch);
    stretched_text(draw, font, size, text, cx - width * 0.5f, baseline - bottom, colour, stretch);
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
    if (key.outline_count) {
        Shape shape;
        for (int i = 0; i < key.outline_count; i++) shape.push_back(frame.at(key.outline[i * 2], key.outline[i * 2 + 1]));
        if (signed_area(shape) < 0) std::reverse(shape.begin(), shape.end());
        return shape;
    }
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

ImU32 scaled_colour(ImU32 colour, float k, float sheen = 0.0f) {
    int r = (colour >> IM_COL32_R_SHIFT) & 0xff, g = (colour >> IM_COL32_G_SHIFT) & 0xff, b = (colour >> IM_COL32_B_SHIFT) & 0xff;
    auto channel = [&](int c) { return std::min(255, (int)(c * k + (255 - c * k) * sheen)); };
    return IM_COL32(channel(r), channel(g), channel(b), 255);
}

ImU32 tube_colour(float t, ImU32 base, ImU32 top_colour, ImU32 bottom_colour) {
    float angle = (t - 0.5f) * IM_PI;
    float light = cosf(angle + 0.55f);
    float shade = 0.58f + 0.46f * std::max(0.0f, light);
    float sheen = 0.32f * powf(std::max(0.0f, cosf(angle + 0.7f)), 18.0f);
    ImU32 colour = scaled_colour(base, shade, sheen);
    if (t < 0.12f) colour = mix(top_colour, colour, t / 0.12f);
    if (t > 0.9f) colour = mix(colour, bottom_colour, (t - 0.9f) / 0.1f);
    return colour;
}

Shape clip_band(const Shape &polygon, float y0, float y1) {
    Shape shape = polygon;
    for (int side = 0; side < 2 && !shape.empty(); side++) {
        Shape out;
        float limit = side == 0 ? y0 : y1;
        auto inside = [&](ImVec2 p) { return side == 0 ? p.y >= limit : p.y <= limit; };
        for (size_t i = 0; i < shape.size(); i++) {
            ImVec2 a = shape[i], b = shape[(i + 1) % shape.size()];
            if (inside(a)) out.push_back(a);
            if (inside(a) != inside(b)) out.push_back(a + (b - a) * ((limit - a.y) / (b.y - a.y)));
        }
        shape = out;
    }
    return shape;
}

void tube(ImDrawList *draw, const Shape &shape, const std::vector<int> &indices, float y0, float y1, ImU32 base, ImU32 top_colour, ImU32 bottom_colour,
          float until = FLT_MAX) {
    const int bands = 24;
    std::vector<int> triangles = indices.empty() ? ear_clip(shape) : indices;
    ImVec2 uv = draw->_Data->TexUvWhitePixel;
    auto colour_at = [&](float y) { return tube_colour(std::max(0.0f, std::min(1.0f, (y - y0) / (y1 - y0))), base, top_colour, bottom_colour); };
    for (size_t t = 0; t + 2 < triangles.size(); t += 3) {
        Shape triangle = { shape[triangles[t]], shape[triangles[t + 1]], shape[triangles[t + 2]] };
        for (int band = -1; band <= bands; band++) {
            float a = band < 0 ? -FLT_MAX : y0 + (y1 - y0) * band / bands;
            float b = band == bands ? FLT_MAX : y0 + (y1 - y0) * (band + 1) / bands;
            b = std::min(b, until);
            if (a >= b) continue;
            Shape piece = clip_band(triangle, a, b);
            if (piece.size() < 3) continue;
            draw->PrimReserve((int)(piece.size() - 2) * 3, (int)piece.size());
            ImDrawIdx base_index = (ImDrawIdx)draw->_VtxCurrentIdx;
            for (const ImVec2 &point : piece) draw->PrimWriteVtx(point, uv, colour_at(point.y));
            for (size_t k = 1; k + 1 < piece.size(); k++) {
                draw->PrimWriteIdx(base_index);
                draw->PrimWriteIdx((ImDrawIdx)(base_index + k));
                draw->PrimWriteIdx((ImDrawIdx)(base_index + k + 1));
            }
        }
    }
}

void cap_shade(ImDrawList *draw, const Shape &shape, const std::vector<int> &indices, bool left, float width) {
    ImRect box = bounds(shape);
    int start = draw->VtxBuffer.Size;
    fill_polygon(draw, shape, IM_COL32_WHITE, indices);
    float edge = left ? box.Min.x : box.Max.x;
    for (int i = start; i < draw->VtxBuffer.Size; i++) {
        float t = std::min(1.0f, fabsf(draw->VtxBuffer[i].pos.x - edge) / width);
        draw->VtxBuffer[i].col = IM_COL32(24, 28, 34, (int)(110 * (1.0f - t) * (1.0f - t)));
    }
}

void draw_groove(ImDrawList *draw, const Shape &face, float from_x, float to_x, float below_y, float k) {
    Shape edge;
    ImRect box = bounds(face);
    for (const ImVec2 &point : face) {
        if (point.x > from_x && point.x < to_x && point.y < below_y && point.y < box.GetCenter().y) edge.push_back(point);
    }
    std::sort(edge.begin(), edge.end(), [](ImVec2 a, ImVec2 b) { return a.x < b.x; });
    if (edge.size() < 2) return;
    Shape shadow = translated(edge, ImVec2(0, -1.2f * k));
    draw->AddPolyline(shadow.data(), (int)shadow.size(), IM_COL32(30, 34, 40, 170), 0, 1.6f * k);
    Shape light = translated(edge, ImVec2(0, 0.6f * k));
    draw->AddPolyline(light.data(), (int)light.size(), IM_COL32(246, 250, 252, 220), 0, 1.0f * k);
}

void draw_keybed(ImDrawList *draw, const KeyboardFrame &frame, const Shape &face, ImRect hinge, float hinge_top, bool compact, bool wear) {
    float k = frame.kbu;
    Shape front = translated(face, ImVec2(0, KB_FRONT_DEPTH * k));
    fill(draw, translated(front, ImVec2(0, 4 * k)), IM_COL32(0, 0, 0, 80), IM_COL32(0, 0, 0, 120));
    fill(draw, front, KB_FRONT_TOP, KB_FRONT_BOTTOM);
    ImRect face_box = bounds(face);
    Shape seam;
    float bottom = face_box.Max.y;
    for (const ImVec2 &point : face) {
        if (point.y > bottom - KB_BOTTOM_RADIUS * k - 0.01f) seam.push_back(point + ImVec2(0, KB_FRONT_SEAM * k));
    }
    std::sort(seam.begin(), seam.end(), [](ImVec2 a, ImVec2 b) { return a.x < b.x; });
    draw->AddPolyline(seam.data(), (int)seam.size(), KB_SEAM_DARK, 0, 0.8f * k);
    Shape seam_light = translated(seam, ImVec2(0, 0.8f * k));
    draw->AddPolyline(seam_light.data(), (int)seam_light.size(), faded(KB_SEAM_LIGHT, 0.6f), 0, 0.6f * k);

    {
        std::vector<int> indices = lid_indices(LID_SHAPE_KEYBOARD_BODY);
        int start = draw->VtxBuffer.Size;
        fill_polygon(draw, face, IM_COL32_WHITE, indices);
        ImGui::ShadeVertsLinearColorGradientKeepAlpha(draw, start, draw->VtxBuffer.Size, ImVec2(0, face_box.Min.y), ImVec2(0, face_box.Max.y),
                                                      lighten(KB_KEYBED, 10), mix(KB_KEYBED, IM_COL32(152, 163, 171, 255), 0.35f));
        if (hinge.GetWidth() > 0) tube(draw, face, indices, hinge_top, hinge.Max.y, KB_KEYBED, scaled_colour(KB_KEYBED, 0.6f), lighten(KB_KEYBED, 10), hinge.Max.y);
    }
    if (grime_texture) {
        ImRect box = face_box;
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

    if (compact) draw_groove(draw, face, face_box.Min.x - 1, face_box.Max.x + 1, face_box.Min.y + 1.0f, k);
    else if (hinge.GetWidth() > 0) draw_groove(draw, face, hinge.Min.x, hinge.Max.x, hinge.Max.y, k);
    float finger_y = (face_box.Max.y - frame.origin.y) / k + KB_FINGER_Y - KB_HEIGHT;
    draw_recess(draw, pill(frame.at(KB_FINGER_X - KB_FINGER_W * 0.5f, finger_y - KB_FINGER_H * 0.5f),
                           frame.at(KB_FINGER_X + KB_FINGER_W * 0.5f, finger_y + KB_FINGER_H * 0.5f)),
                SCOOP_RECESS, k, Mask(), FINGER_SCOOP);
    {
        ImVec2 centre = frame.at(KB_WELL_X, KB_WELL_Y);
        ImU32 shell = mix(lighten(KB_KEYBED, 10), mix(KB_KEYBED, IM_COL32(152, 163, 171, 255), 0.35f), 0.8f);
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

Shape traced_points(const KeyboardFrame &frame, const float *points, int count, ImVec2 offset) {
    Shape shape;
    for (int i = 0; i < count; i++) shape.push_back(frame.at(points[i * 2], points[i * 2 + 1]) + offset);
    return shape;
}

void draw_key_hole(ImDrawList *draw, const Shape &hole, float k) {
    fill(draw, hole, KEY_HOLE_DARK, KEY_HOLE_DARK);
    size_t count = hole.size();
    for (size_t i = 0; i < count; i++) {
        ImVec2 a = hole[i], b = hole[(i + 1) % count];
        ImVec2 edge = b - a;
        float length = sqrtf(edge.x * edge.x + edge.y * edge.y);
        if (length <= 0) continue;
        float facing = -edge.x / length;
        if (facing <= 0) continue;
        draw->AddLine(a, b, faded(KEY_HOLE_EDGE, powf(facing, 1.5f)), KEY_HOLE_EDGE_WIDTH * k);
    }
}

void paint_keyboard(ImDrawList *draw, const KeyboardFrame &frame, const Shape &face, ImRect hinge, float hinge_top, bool compact, float u, const DeviceState &state,
                    const uint8_t *down) {
    float k = frame.kbu;
    draw_keybed(draw, frame, face, hinge, hinge_top, compact, state.wear);
    for (const KeyboardKey &key : keyboard_keys) {
        if (!key.ring) continue;
        Shape ring = outset(keyboard_key_shape(frame, key), 1.6f * u + 4.6f * k + 0.75f * k);
        draw->AddPolyline(ring.data(), (int)ring.size(), faded(KB_RING, 0.95f), ImDrawFlags_Closed, 1.5f * k);
    }

    for (const KeyboardKey &key : keyboard_keys) draw_key_hole(draw, outset(keyboard_key_shape(frame, key), KEY_HOLE_GAP * k), k);
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
            float bar_y = key.h * HOMING_DROP;
            Shape bar = key.locator_count ? traced_points(frame, key.locator, key.locator_count, dip)
                                          : pill(centre + ImVec2(-HOMING_HALF_W, bar_y - HOMING_HALF_H) * k, centre + ImVec2(HOMING_HALF_W, bar_y + HOMING_HALF_H) * k);
            ImU32 ridge_side = mix(style.bottom, IM_COL32(0, 0, 0, 255), 0.18f);
            for (int step = 3; step >= 1; step--) fill(draw, translated(bar, ImVec2(0, step * 0.35f * k)), ridge_side, ridge_side);
            fill(draw, bar, lighten(style.top, 6), mix(style.top, style.bottom, 0.45f));
        }
        ImU32 legend_colour = faded(KB_KEY_LEGEND[key.colour], KEYCAP_LEGEND_ALPHA);
        ImVec2 legend_centre = centre + ImVec2(0, key.legend_dy * k);
        ImRect legend_box(legend_centre - ImVec2(12, 9) * k, legend_centre + ImVec2(12, 9) * k);
        if (key.icon_regions) {
            if (key.legend) {
                float icon_left = FLT_MAX, icon_right = -FLT_MAX;
                for (int i = 0; i < key.icon_counts[0]; i++) {
                    icon_left = std::min(icon_left, key.icon_points[i * 2]);
                    icon_right = std::max(icon_right, key.icon_points[i * 2]);
                }
                float ink_left, ink_right;
                ink_extent(keyboard_font(false), key.legend_size * k, key.legend, ink_left, ink_right);
                float fitted = KB_LEGEND_STRETCH * (icon_right - icon_left) * k / std::max(1.0f, (ink_right - ink_left) * KB_LEGEND_STRETCH);
                ImVec2 at = ImVec2(frame.at((icon_left + icon_right) * 0.5f, 0).x + dip.x, centre.y - 6.5f * k * key.h / KEY_REFERENCE_H);
                centred_legend(draw, keyboard_font(false), key.legend_size * k, key.legend, at, legend_colour, fitted);
            }
            std::vector<Shape> contours;
            const float *point = key.icon_points;
            for (int region = 0; region < key.icon_regions; region++) {
                contours.push_back(traced_points(frame, point, key.icon_counts[region], dip));
                point += key.icon_counts[region] * 2;
            }
            fill_triangles(draw, key.icon_triangles, key.icon_triangle_count, contours,
                           [&](float x, float y) { return frame.at(x, y) + dip; }, legend_colour);
        } else if (key.icon == KB_ICON_NONE && key.legend) {
            legend_box = centred_legend(draw, keyboard_font(false), key.legend_size * k, key.legend, legend_centre, legend_colour,
                                        KB_LEGEND_STRETCH * key.stretch);
        } else {
            keyboard_icon(draw, frame, key, key.icon == KB_ICON_TRIANGLE ? centre : legend_centre, legend_colour);
        }
        if (state.wear) rub_patch(draw, legend_box.Min, legend_box.Max, face, key.wear, (uint32_t)(key.x * 31 + key.y * 17));
    }

}

struct DeviceLayout {
    ImVec2 device_min, device_max, image_min, image_max;
    float u;
    float rounding;
    bool has_keyboard;
    bool compact;
};

float keyboard_scale() {
    return LID_UNITS_PER_MM / KB_UNITS_PER_MM;
}

float lid_bottom_units() {
    return REFERENCE_LCD_H + LID_BOTTOM_EXTENT;
}

float keyboard_extra_units(bool compact) {
    return LID_KEYBOARD_BOTTOM + KB_FRONT_DEPTH * keyboard_scale() - lid_bottom_units() - (compact ? LID_COMPACT_SHIFT : 0.0f);
}

void square_corners(Shape &shape, bool top, float y_edge, float reach) {
    ImRect box = bounds(shape);
    for (ImVec2 &point : shape) {
        bool in_band = top ? point.y < y_edge + reach : point.y > y_edge - reach;
        if (!in_band) continue;
        if (point.x < box.Min.x + reach) point.x = box.Min.x;
        if (point.x > box.Max.x - reach) point.x = box.Max.x;
        if (top) point.y = std::max(point.y, y_edge);
    }
}

Shape lid_body(const DeviceLayout &layout) {
    Shape body = lid_shape(Frame{ layout.image_min, layout.image_max, layout.u }, LID_SHAPE_BODY);
    if (layout.compact) square_corners(body, false, bounds(body).Max.y, LID_BODY_RADIUS * layout.u);
    return body;
}

Shape keyboard_body(const DeviceLayout &layout) {
    Shape body = lid_shape(Frame{ layout.image_min, layout.image_max, layout.u }, LID_SHAPE_KEYBOARD_BODY);
    if (!layout.compact) return body;
    float lift = LID_COMPACT_SHIFT * layout.u;
    for (ImVec2 &point : body) point.y -= lift;
    square_corners(body, true, layout.device_max.y, LID_BODY_RADIUS * layout.u);
    return body;
}

void hinge_span(const DeviceLayout &layout, Shape &left, Shape &right, Shape &middle, ImRect &span) {
    Frame frame{ layout.image_min, layout.image_max, layout.u };
    left = lid_shape(frame, LID_SHAPE_HINGE_LEFT);
    right = lid_shape(frame, LID_SHAPE_HINGE_RIGHT);
    middle = lid_shape(frame, LID_SHAPE_KEYBOARD_HINGE);
    span = bounds(left);
    span.Add(bounds(right));
    span.Add(bounds(middle));
}

void draw_hinge_barrel(ImDrawList *draw, const DeviceLayout &layout) {
    Shape left, right, middle;
    ImRect span;
    hinge_span(layout, left, right, middle, span);
    fill(draw, translated(middle, ImVec2(0, 3 * layout.u)), IM_COL32(0, 0, 0, 60), IM_COL32(0, 0, 0, 90));
    ImU32 face = lighten(KB_KEYBED, 10);
    tube(draw, middle, lid_indices(LID_SHAPE_KEYBOARD_HINGE), span.Min.y, span.Max.y, KB_KEYBED, scaled_colour(KB_KEYBED, 0.6f), face);
}

void draw_hinge_caps(ImDrawList *draw, const DeviceLayout &layout, ImU32 lid_bottom) {
    Shape left, right, middle;
    ImRect span;
    hinge_span(layout, left, right, middle, span);
    float cap = bounds(left).GetWidth() * 0.45f;
    for (int side = 0; side < 2; side++) {
        const Shape &shape = side == 0 ? left : right;
        std::vector<int> indices = lid_indices(side == 0 ? LID_SHAPE_HINGE_LEFT : LID_SHAPE_HINGE_RIGHT);
        tube(draw, shape, indices, span.Min.y, span.Max.y, BEZEL, lid_bottom, scaled_colour(BEZEL, 0.52f));
        cap_shade(draw, shape, indices, side == 0, cap);
    }
    ImRect barrel = bounds(middle);
    float u = layout.u;
    for (float x : { barrel.Min.x, barrel.Max.x }) {
        ImVec2 top(x, span.Min.y + 1.0f), bottom(x, span.Max.y - 1.0f);
        draw->AddLine(top, bottom, IM_COL32(28, 32, 38, 200), 1.6f * u);
        float light = x == barrel.Min.x ? -1.2f * u : 1.2f * u;
        draw->AddLine(top + ImVec2(light, 0), bottom + ImVec2(light, 0), IM_COL32(236, 242, 246, 110), 0.8f * u);
    }
}


struct BakeKey {
    ImVec2 origin, size;
    float scale;
    bool show_keys, has_keyboard, wear, focused, compact;
    std::vector<uint8_t> down;
    std::string model;

    bool operator==(const BakeKey &other) const {
        return origin.x == other.origin.x && origin.y == other.origin.y && size.x == other.size.x && size.y == other.size.y &&
               scale == other.scale && show_keys == other.show_keys && has_keyboard == other.has_keyboard && wear == other.wear &&
               focused == other.focused && compact == other.compact && down == other.down && model == other.model;
    }
};

SDL_Texture *bake_texture = nullptr;
ImDrawList *bake_list = nullptr;
BakeKey baked, pending;
bool bake_pending = false;
ImVec2 bake_min, bake_size;
float bake_scale = 1.0f;

KeyboardFrame keyboard_frame(const DeviceLayout &layout) {
    Frame lid{ layout.image_min, layout.image_max, layout.u };
    ImVec2 origin = lid.lid((KB_MM_ORIGIN_X - LID_GLASS_MM_X) * LID_UNITS_PER_MM, (KB_MM_ORIGIN_Y - LID_GLASS_MM_Y) * LID_UNITS_PER_MM);
    if (layout.compact) origin.y -= LID_COMPACT_SHIFT * layout.u;
    return KeyboardFrame{ origin, layout.u * keyboard_scale() };
}

void record_case(const DeviceLayout &layout) {
    case_regions.push_back({ ImRect(layout.device_min, layout.device_max), layout.rounding });
    if (!layout.has_keyboard) return;
    Frame frame{ layout.image_min, layout.image_max, layout.u };
    if (!layout.compact) {
        ImRect hinge = bounds(lid_shape(frame, LID_SHAPE_KEYBOARD_HINGE));
        hinge.Add(bounds(lid_shape(frame, LID_SHAPE_HINGE_LEFT)));
        hinge.Add(bounds(lid_shape(frame, LID_SHAPE_HINGE_RIGHT)));
        case_regions.push_back({ hinge, hinge.GetHeight() * 0.5f });
    }
    ImRect keyboard = bounds(keyboard_body(layout));
    keyboard.Max.y += KB_FRONT_DEPTH * keyboard_frame(layout).kbu;
    case_regions.push_back({ keyboard, (KB_TOP_RADIUS + 3) * keyboard_frame(layout).kbu });
}

void paint_device(ImDrawList *draw, SDL_Renderer *renderer, float framebuffer_scale, const DeviceLayout &layout, const DeviceState &state,
                  const uint8_t *down) {
    ImVec2 device_min = layout.device_min, device_max = layout.device_max, image_min = layout.image_min, image_max = layout.image_max;
    ImVec2 device_size = device_max - device_min;
    float u = layout.u, rounding = layout.rounding;
    if (layout.has_keyboard && !layout.compact) {
        draw_hinge_barrel(draw, layout);
        draw_hinge_caps(draw, layout, scaled_colour(BEZEL, 0.9f));
    }
    Shape body = state.show_keys ? lid_body(layout) : Shape();
    if (state.focused && !state.borderless) {
        draw->AddRect(device_min - ImVec2(3, 3), device_max + ImVec2(3, 3), IM_COL32(90, 200, 180, 160), rounding + 4, 0, 2.0f);
    }
    if (state.show_keys) {
        fill(draw, translated(body, ImVec2(0, 4)), IM_COL32(0, 0, 0, 90), IM_COL32(0, 0, 0, 90));
        shade_body(draw, body, image_min, image_max, u);
    } else {
        draw->AddRectFilled(device_min + ImVec2(0, 4), device_max + ImVec2(0, 4), IM_COL32(0, 0, 0, 90), rounding);
        draw->AddRectFilled(device_min, device_max, BEZEL, rounding);
    }
    build_grime(renderer, (int)(device_size.x * framebuffer_scale), (int)(device_size.y * framebuffer_scale), framebuffer_scale * u, state.wear);
    wear_labels = state.wear;
    wear_grime = state.wear;
    draw->AddImageRounded((ImTextureID)(intptr_t)grime_texture, device_min, device_max, ImVec2(0, 0), ImVec2(1, 1),
                          IM_COL32_WHITE, rounding);
    load_scratches(renderer);
    if (state.wear) case_scratches(draw, device_min, device_max, rounding, 0.0f);
    if (!state.show_keys) draw->AddRectFilledMultiColor(device_min + ImVec2(rounding, 2), ImVec2(device_max.x - rounding, device_min.y + device_size.y * 0.45f),
                                  IM_COL32(255, 255, 255, 40), IM_COL32(255, 255, 255, 40), IM_COL32(255, 255, 255, 0), IM_COL32(255, 255, 255, 0));
    if (state.show_keys) {
        draw->AddPolyline(body.data(), (int)body.size(), BEZEL_EDGE, ImDrawFlags_Closed, 2.0f);
        Shape inner = inset(body, 2.0f);
        draw->AddPolyline(inner.data(), (int)inner.size(), BEZEL_LIGHT, ImDrawFlags_Closed, 1.0f);
    } else {
        draw->AddRect(device_min, device_max, BEZEL_EDGE, rounding, 0, 2.0f);
        draw->AddRect(device_min + ImVec2(2, 2), device_max - ImVec2(2, 2), BEZEL_LIGHT, rounding - 2, 0, 1.0f);
    }

    if (state.show_keys) {
        Frame lid_frame{ image_min, image_max, u };
        ImVec2 frame_min = lid_frame.lid(LID_FRAME[0], LID_FRAME[1]), frame_max = lid_frame.lid(LID_FRAME[2], LID_FRAME[3]);
        draw->AddRectFilled(frame_min, frame_max, FRAME, LID_FRAME_RADIUS * u);
        draw->AddRect(frame_min, frame_max, BEZEL_LIGHT, LID_FRAME_RADIUS * u, 0, 1.5f);
        draw->AddRect(frame_min + ImVec2(1, 1), frame_max + ImVec2(1, 1), BEZEL_EDGE, LID_FRAME_RADIUS * u, 0, 1.0f);
    }
    draw->AddRectFilled(image_min - ImVec2(2, 2), image_max + ImVec2(2, 2), IM_COL32(58, 64, 68, 255), 3.0f);
    if (state.show_keys) {
        float brand = 24.0f * u;
        ImVec2 at = image_min + ImVec2(-4 * u, -50 * u);
        erase_colour = faded(BEZEL, 0.9f);
        rub_mode = false;
        draw->AddText(text_font(), brand, at, PRINT, "SHAM");
        wear_patch(draw, at, at + text_size(text_font(), brand, "SHAM"), 1);
        draw->AddText(ImGui::GetFont(), 17.0f * u, at + ImVec2(text_size(text_font(), brand, "SHAM").x + 18 * u, 5 * u), PRINT, model_name.c_str());
        paint_lid_keys(draw, Frame{ image_min, image_max, u }, device_min, device_max, down);
        if (layout.has_keyboard) {
            Frame frame{ image_min, image_max, u };
            ImRect hinge = layout.compact ? ImRect() : bounds(lid_shape(frame, LID_SHAPE_KEYBOARD_HINGE));
            float hinge_top = hinge.Min.y;
            if (!layout.compact) {
                Shape left, right, middle;
                ImRect span;
                hinge_span(layout, left, right, middle, span);
                hinge = ImRect(hinge.Min.x, span.Min.y, hinge.Max.x, span.Max.y);
                hinge_top = span.Min.y;
            }
            paint_keyboard(draw, keyboard_frame(layout), keyboard_body(layout), hinge, hinge_top, layout.compact, u, state, down + LID_KEY_COUNT);
        }
    } else {
        draw->AddText(device_min + ImVec2(PLAIN_BEZEL, 8), IM_COL32(60, 66, 72, 255), ("SHAM  " + model_name).c_str());
    }

}


}

static float extra_lid_units(const DeviceState &state) {
    return state.show_keys && state.show_keyboard ? keyboard_extra_units(state.touch) : 0.0f;
}

int device_fit_cell(ImVec2 content, float framebuffer_scale, const DeviceState &state) {
    content -= ImVec2(2 * DEVICE_MARGIN, 2 * DEVICE_MARGIN);
    float image_h;
    if (state.screen_only) {
        image_h = std::min((content.x - 2 * SCREEN_MARGIN) * GRID_H / GRID_W, content.y - 2 * SCREEN_MARGIN);
    } else if (state.show_keys) {
        float by_width = content.x / (GRID_W / GRID_H + (LEFT_EXTENT + RIGHT_EXTENT) / REFERENCE_LCD_H);
        float by_height = content.y / (1.0f + (TOP_EXTENT + BOTTOM_EXTENT + extra_lid_units(state)) / REFERENCE_LCD_H);
        image_h = std::min(by_width, by_height);
    } else {
        image_h = std::min((content.x - 2 * PLAIN_BEZEL) * GRID_H / GRID_W, content.y - 2 * PLAIN_BEZEL);
    }
    return std::max(DEVICE_MIN_CELL, (int)floorf(image_h * framebuffer_scale / GRID_H + 0.001f));
}

ImVec2 device_content_size(int cell, float framebuffer_scale, const DeviceState &state) {
    ImVec2 image(cell * GRID_W / framebuffer_scale, cell * GRID_H / framebuffer_scale);
    ImVec2 margin(2 * DEVICE_MARGIN, 2 * DEVICE_MARGIN);
    if (state.screen_only) return image + ImVec2(2 * SCREEN_MARGIN, 2 * SCREEN_MARGIN) + margin;
    if (!state.show_keys) return image + ImVec2(2 * PLAIN_BEZEL, 2 * PLAIN_BEZEL) + margin;
    float u = image.y / REFERENCE_LCD_H;
    return ImVec2(image.x + (LEFT_EXTENT + RIGHT_EXTENT) * u, image.y + (TOP_EXTENT + BOTTOM_EXTENT + extra_lid_units(state)) * u) + margin;
}

float device_draw(SDL_Renderer *renderer, float framebuffer_scale, float height, float compose_seconds, DeviceState &state) {
    ImVec2 origin = ImGui::GetCursorScreenPos();
    float avail_w = ImGui::GetContentRegionAvail().x;
    case_regions.clear();
    control_regions.clear();
    bool ring = state.focused && !state.borderless;
    int cell = device_fit_cell(ImVec2(avail_w, height), framebuffer_scale, state);
    if (state.screen_only) {
        lcd_compose_setup(cell);
        upload_lcd(renderer, compose_seconds);
        ImVec2 size(cell * GRID_W / framebuffer_scale, cell * GRID_H / framebuffer_scale);
        ImVec2 min = origin + ImVec2((avail_w - size.x) * 0.5f, (height - size.y) * 0.5f);
        ImDrawList *draw = ImGui::GetWindowDrawList();
        if (ring) draw->AddRect(min - ImVec2(3, 3), min + size + ImVec2(3, 3), IM_COL32(90, 200, 180, 160), 4.0f, 0, 2.0f);
        case_regions.push_back({ ImRect(min, min + size), 0.0f });
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

    bool has_keyboard = state.show_keys && state.show_keyboard;
    float extra_units = extra_lid_units(state);
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

    DeviceLayout layout = { device_min, device_max, image_min, image_max, u, rounding, has_keyboard, has_keyboard && state.touch };
    record_case(layout);
    ImGui::SetCursorScreenPos(image_min);
    ImGui::InvisibleButton("device", image_size);
    std::vector<uint8_t> down(LID_KEY_COUNT + KEYBOARD_KEY_COUNT, 0);
    if (state.show_keys) {
        input_lid_keys(Frame{ image_min, image_max, u }, state, down.data());
        if (has_keyboard) input_keyboard(keyboard_frame(layout), state, down.data() + LID_KEY_COUNT);
    }

    BakeKey key = { origin, ImVec2(avail_w, height), framebuffer_scale, state.show_keys, has_keyboard, state.wear, ring, layout.compact, down, model_name };
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
        SDL_SetTextureBlendMode(bake_texture, SDL_BLENDMODE_BLEND_PREMULTIPLIED);
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
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 0);
    SDL_RenderClear(renderer);
    ImGui_ImplSDLRenderer3_RenderDrawData(&data, renderer);
    SDL_SetRenderTarget(renderer, previous);
    baked = pending;
}

bool device_draggable(float x, float y) {
    ImVec2 point(x, y);
    for (const ImRect &control : control_regions) {
        if (control.Contains(point)) return false;
    }
    for (const Region &region : case_regions) {
        if (region.contains(point)) return true;
    }
    return false;
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
