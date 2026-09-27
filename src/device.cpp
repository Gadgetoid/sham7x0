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

#include "case_raster.h"
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
const float LID_NOTCH_DEPTH = 12.0f;
const uint64_t REPEAT_DELAY_MS = 400;
const uint64_t REPEAT_RATE_MS = 80;

const ImU32 BEZEL = IM_COL32(178, 189, 199, 255);
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
SDL_Texture *scratch_texture = nullptr;
bool scratch_tried = false;
std::vector<uint8_t> scratch_alpha;
int scratch_w = 0, scratch_h = 0;
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
            scratch_alpha = alpha;
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



ImU32 faded(ImU32 colour, float alpha) {
    int a = (int)(((colour >> IM_COL32_A_SHIFT) & 0xff) * std::max(0.0f, std::min(1.0f, alpha)));
    return (colour & ~IM_COL32_A_MASK) | ((ImU32)a << IM_COL32_A_SHIFT);
}

float smoothstep(float t) {
    t = std::max(0.0f, std::min(1.0f, t));
    return t * t * (3 - 2 * t);
}

const float KEY_HOLE_GAP = 2.2f;
const float HOMING_DROP = 0.4f;
const float KEY_REFERENCE_H = 36.6f;
const float KEYCAP_LEGEND_ALPHA = 0.76f;
const float HOMING_HALF_W = 8.4f;
const float HOMING_HALF_H = 2.25f;
const ImU32 KEY_HOLE_DARK = IM_COL32(14, 16, 18, 255);

ImU32 mix(ImU32 a, ImU32 b, float t) {
    t = std::max(0.0f, std::min(1.0f, t));
    auto channel = [&](int shift) { return (int)(((a >> shift) & 0xff) * (1 - t) + ((b >> shift) & 0xff) * t); };
    return IM_COL32(channel(IM_COL32_R_SHIFT), channel(IM_COL32_G_SHIFT), channel(IM_COL32_B_SHIFT), channel(IM_COL32_A_SHIFT));
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

void paint_lid_keys(ImDrawList *draw, const Frame &frame, const uint8_t *down) {
    float u = frame.u;
    ImVec2 press(0, 1.2f * u);
    auto dip = [&](bool pressed) { return pressed ? press : ImVec2(0, 0); };
    LidLayout lid = lid_layout(frame);
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

    draw_key(draw, lid.light, TEAL_KEY, down[LID_LIGHT], u);
    fill_lid_icon(draw, frame, LID_SHAPE_LIGHT_ICON, dip(down[LID_LIGHT]), TEAL_ICON);

    erase_colour = faded(BEZEL, 0.9f);
    rub_mode = false;
    centred_text(draw, lid.menu_centre - ImVec2(0, 39 * u), 15.0f * u, PRINT, "MENU");
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

    draw_key(draw, lid.esc, DARK_KEY, down[LID_ESC], u);
    centred_text(draw, lid.esc_centre + dip(down[LID_ESC]), 15.0f * u, LABEL, "ESC");
    draw_key(draw, lid.enter, DARK_KEY, down[LID_ENTER], u);
    centred_text(draw, lid.enter_centre + dip(down[LID_ENTER]), 16.0f * u, LABEL, "ENTER");
}

const float KEY_TRAVEL = 2.6f;
const float KEY_SHOULDER = 4.0f;
const float KB_SQUARE_CORNER = 0.22f;
const ImU32 KB_RING = IM_COL32(96, 84, 156, 255);

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

void paint_keyboard(ImDrawList *draw, const KeyboardFrame &frame, float u, const DeviceState &state, const uint8_t *down) {
    float k = frame.kbu;
    for (const KeyboardKey &key : keyboard_keys) {
        if (!key.ring) continue;
        Shape ring = outset(keyboard_key_shape(frame, key), 1.6f * u + 4.6f * k + 0.75f * k);
        draw->AddPolyline(ring.data(), (int)ring.size(), faded(KB_RING, 0.95f), ImDrawFlags_Closed, 1.5f * k);
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
    if (!layout.compact) return body;
    ImRect box = bounds(body);
    square_corners(body, false, box.Max.y, LID_BODY_RADIUS * layout.u);
    for (ImVec2 &point : body) {
        if (point.y > box.Max.y - LID_NOTCH_DEPTH * layout.u) point.y = box.Max.y;
    }
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


struct CaseKey {
    ImVec2 origin, size;
    float scale;
    bool show_keys, has_keyboard, wear, focused, compact;
    std::string model;

    bool operator==(const CaseKey &other) const {
        return origin.x == other.origin.x && origin.y == other.origin.y && size.x == other.size.x && size.y == other.size.y &&
               scale == other.scale && show_keys == other.show_keys && has_keyboard == other.has_keyboard && wear == other.wear &&
               focused == other.focused && compact == other.compact && model == other.model;
    }
};

struct OverlayKey {
    CaseKey base;
    std::vector<uint8_t> down;

    bool operator==(const OverlayKey &other) const {
        return base == other.base && down == other.down;
    }
};

struct Bake {
    SDL_Texture *texture = nullptr;
    ImDrawList *list = nullptr;
    bool pending = false;
    ImVec2 min, size;
    float scale = 1.0f;
};

Bake overlay_bake;
CaseKey baked_case;
OverlayKey baked_overlay, pending_overlay;

bool bake_current(const Bake &bake) {
    return bake.texture && !bake.pending;
}

ImDrawList *begin_bake(Bake &bake, ImDrawList *window, ImVec2 origin, ImVec2 size, float scale) {
    if (!bake.list) bake.list = IM_NEW(ImDrawList)(ImGui::GetDrawListSharedData());
    bake.list->_ResetForNewFrame();
    bake.list->Flags = window->Flags;
    bake.list->_SetPixelDensity(window->_InvFringeScale);
    bake.min = ImVec2(floorf(origin.x * scale), floorf(origin.y * scale)) / scale;
    bake.size = origin + size - bake.min;
    bake.scale = scale;
    bake.list->PushClipRect(bake.min, origin + size);
    bake.list->PushTexture(ImGui::GetIO().Fonts->TexRef);
    return bake.list;
}

void replay(ImDrawList *into, const ImDrawList *from) {
    const unsigned batch = 3 * 8192;
    for (const ImDrawCmd &command : from->CmdBuffer) {
        if (command.ElemCount == 0 || command.UserCallback) continue;
        into->PushClipRect(ImVec2(command.ClipRect.x, command.ClipRect.y), ImVec2(command.ClipRect.z, command.ClipRect.w), true);
        into->PushTexture(command.TexRef);
        for (unsigned start = 0; start < command.ElemCount; start += batch) {
            unsigned count = std::min(batch, command.ElemCount - start);
            into->PrimReserve((int)count, (int)count);
            for (unsigned i = 0; i < count; i++) {
                const ImDrawVert &vertex = from->VtxBuffer[command.VtxOffset + from->IdxBuffer[command.IdxOffset + start + i]];
                into->PrimWriteIdx((ImDrawIdx)into->_VtxCurrentIdx);
                into->PrimWriteVtx(vertex.pos, vertex.uv, vertex.col);
            }
        }
        into->PopTexture();
        into->PopClipRect();
    }
}

void finish_bake(Bake &bake, ImDrawList *window) {
    replay(window, bake.list);
    bake.pending = true;
}

void show_bake(const Bake &bake, ImDrawList *window) {
    window->AddImage((ImTextureID)(intptr_t)bake.texture, bake.min, bake.min + ImVec2((float)bake.texture->w, (float)bake.texture->h) / bake.scale);
}

void flush_bake(Bake &bake, SDL_Renderer *renderer) {
    if (!bake.pending || !bake.list) return;
    bake.pending = false;
    int width = (int)ceilf(bake.size.x * bake.scale), height = (int)ceilf(bake.size.y * bake.scale);
    if (width <= 0 || height <= 0) return;
    if (!bake.texture || bake.texture->w != width || bake.texture->h != height) {
        if (bake.texture) SDL_DestroyTexture(bake.texture);
        bake.texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_TARGET, width, height);
        if (!bake.texture) return;
        SDL_SetTextureScaleMode(bake.texture, SDL_SCALEMODE_NEAREST);
        SDL_SetTextureBlendMode(bake.texture, SDL_BLENDMODE_BLEND_PREMULTIPLIED);
    }
    for (ImDrawVert &vertex : bake.list->VtxBuffer) vertex.pos -= bake.min;
    for (ImDrawCmd &command : bake.list->CmdBuffer) command.ClipRect -= ImVec4(bake.min.x, bake.min.y, bake.min.x, bake.min.y);

    ImDrawData data;
    data.Valid = true;
    data.CmdLists.push_back(bake.list);
    data.CmdListsCount = 1;
    data.TotalVtxCount = bake.list->VtxBuffer.Size;
    data.TotalIdxCount = bake.list->IdxBuffer.Size;
    data.DisplayPos = ImVec2(0, 0);
    data.DisplaySize = bake.size;
    data.FramebufferScale = ImVec2(bake.scale, bake.scale);

    SDL_Texture *previous = SDL_GetRenderTarget(renderer);
    SDL_SetRenderTarget(renderer, bake.texture);
    SDL_SetRenderScale(renderer, bake.scale, bake.scale);
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 0);
    SDL_RenderClear(renderer);
    ImGui_ImplSDLRenderer3_RenderDrawData(&data, renderer);
    SDL_SetRenderTarget(renderer, previous);
}

void destroy_bake(Bake &bake) {
    if (bake.texture) SDL_DestroyTexture(bake.texture);
    bake.texture = nullptr;
    if (bake.list) IM_DELETE(bake.list);
    bake.list = nullptr;
}

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

const float CASE_MARGIN = 8.0f;
const ImU32 KB_FACE_TOP = IM_COL32(177, 187, 194, 255);
const ImU32 KB_FACE_BOTTOM = IM_COL32(161, 172, 179, 255);
const ImU32 FOCUS_RING = IM_COL32(90, 200, 180, 160);
const float RING_MARGIN = 3.0f;
const float RING_WIDTH = 2.0f;
const float RING_BRIDGE = 20.0f;
const float SHADOW_OFFSET = 4.0f;
const float SHADOW_BLUR = 3.0f;
const float SHADOW_ALPHA = 0.35f;
const float HINGE_ROUNDNESS = 0.5f;
const float CAP_END_ROUNDING = 16.0f;
const float CAP_GROOVE_WIDTH = 2.4f;
const float CAP_GROOVE_DEPTH = 3.0f;
const float CAP_GROOVE_SHADE = 0.35f;
const float KB_FRONT_RELIEF = 1.5f;
const float KB_FRONT_EDGE = 1.5f;
const float KB_SEAM_WIDTH = 1.1f;
const float KB_SEAM_DEPTH = 0.8f;
const float KB_FACE_RELIEF = 1.2f;
const float KB_FACE_EDGE = 3.0f;
const float ROLL_JOIN = 0.2f;
const float ROLL_KEY_CLEARANCE = 4.0f;
const float ARCH_GROOVE_WIDTH = 6.0f;
const float ARCH_GROOVE_DEPTH = 6.0f;
const float ARCH_GROOVE_TAPER = 14.0f;
const float ARCH_GROOVE_SHADE = 0.0f;
const float JOINT_GROOVE_WIDTH = 1.6f;
const float JOINT_GROOVE_DEPTH = 1.5f;
const float LID_EDGE = 2.5f;
const float LID_RELIEF = 1.0f;
const float LID_DISH_WIDTH = 18.0f;
const float LID_DISH_DEPTH = 1.2f;
const float LID_DISH_TAPER = 40.0f;
const float LID_SHEEN = 0.12f;
const float LID_SHEEN_REACH = 0.45f;
const float PLAIN_EDGE = 2.0f;
const float PLAIN_RELIEF = 0.8f;
const float SCREEN_WALL = 6.0f;
const float SCREEN_DEPTH = 3.0f;
const ImU32 LCD_SURROUND = IM_COL32(58, 64, 68, 255);
const ImU32 HINGE_GAP = IM_COL32(24, 28, 30, 255);
const float FLUTE_WALL = 0.5f;
const float FLUTE_DEPTH = 7.0f;
const float KEY_WELL_WALL = 5.5f;
const float KEY_WELL_DEPTH = 3.0f;
const float FLAT_WELL_WALL = 4.5f;
const float FLAT_WELL_DEPTH = 4.5f;
const ImU32 WELL_TOP = IM_COL32(166, 174, 180, 255);
const ImU32 WELL_BOTTOM = IM_COL32(184, 191, 196, 255);
const ImU32 WELL_FLOOR = IM_COL32(178, 186, 191, 255);
const float WELL_TINT = 0.6f;
const float SCOOP_DEPTH = 4.0f;
const ImU32 SCOOP_TOP = IM_COL32(84, 96, 104, 255);
const ImU32 SCOOP_BOTTOM = IM_COL32(138, 150, 158, 255);
const float SCOOP_TINT = 0.7f;
const float CURSOR_WELL_DEPTH = 5.0f;
const float KEY_HOLE_CHAMFER = 0.9f;
const float KEY_HOLE_DEPTH = 5.0f;

SDL_Texture *case_texture = nullptr;
ImVec2 case_texture_min;
float case_texture_scale = 1.0f;
std::vector<uint32_t> case_pixels;

CaseLayer case_layer(CaseLayerKind kind, const Shape &outline, ImU32 colour = 0) {
    CaseLayer layer;
    layer.kind = kind;
    layer.outline = outline;
    layer.top_colour = colour;
    layer.bottom_colour = colour;
    return layer;
}

void shade_vertically(CaseLayer &layer, ImU32 top, ImU32 bottom) {
    ImRect box = bounds(layer.outline);
    layer.top_colour = top;
    layer.bottom_colour = bottom;
    layer.gradient_top = box.Min.y;
    layer.gradient_bottom = box.Max.y;
}

void set_roll(CaseLayer &layer, const DeviceLayout &layout) {
    KeyboardFrame frame = keyboard_frame(layout);
    float key_top = FLT_MAX;
    for (const KeyboardKey &key : keyboard_keys) key_top = std::min(key_top, key.y - key.h * 0.5f);
    layer.roll_join = ROLL_JOIN;
    layer.roll_end = frame.at(0, key_top - ROLL_KEY_CLEARANCE).y;
    layer.roll_level = (KB_FRONT_RELIEF + KB_FACE_RELIEF) * frame.kbu;
}

void add_hinge(CaseScene &scene, const DeviceLayout &layout) {
    float u = layout.u;
    Shape left, right, middle;
    ImRect span;
    hinge_span(layout, left, right, middle, span);
    CaseLayer barrel = case_layer(CASE_CYLINDER, middle, KB_FACE_TOP);
    barrel.axis_top = span.Min.y;
    barrel.axis_bottom = span.Max.y;
    barrel.height = span.GetHeight() * HINGE_ROUNDNESS;
    barrel.grime = false;
    set_roll(barrel, layout);
    scene.layers.push_back(barrel);
    for (const Shape *cap : { &left, &right }) {
        ImRect box = bounds(*cap);
        CaseLayer end = barrel;
        end.outline = *cap;
        end.roll_end = 0;
        bool on_left = cap == &left;
        end.radius = CAP_END_ROUNDING * u;
        end.edges = on_left ? ImRect(-FLT_MAX, -FLT_MAX, box.GetCenter().x, FLT_MAX) : ImRect(box.GetCenter().x, -FLT_MAX, FLT_MAX, FLT_MAX);
        scene.layers.push_back(end);
        CaseLayer groove = case_layer(CASE_GROOVE, *cap);
        float inside_top = box.Min.y + 0.5f * u, inside_bottom = box.Max.y - 0.5f * u;
        groove.edges = on_left ? ImRect(box.GetCenter().x, inside_top, box.Max.x + u, inside_bottom)
                               : ImRect(box.Min.x - u, inside_top, box.GetCenter().x, inside_bottom);
        groove.radius = CAP_GROOVE_WIDTH * u;
        groove.height = CAP_GROOVE_DEPTH * u;
        groove.tint = CAP_GROOVE_SHADE;
        scene.layers.push_back(groove);
    }
}

void add_keyboard(CaseScene &scene, const DeviceLayout &layout) {
    float u = layout.u;
    KeyboardFrame frame = keyboard_frame(layout);
    float k = frame.kbu;
    Shape face = keyboard_body(layout);
    ImRect face_box = bounds(face);
    CaseLayer front = case_layer(CASE_SOLID, translated(face, ImVec2(0, KB_FRONT_DEPTH * k)));
    shade_vertically(front, KB_FRONT_TOP, KB_FRONT_BOTTOM);
    front.height = KB_FRONT_RELIEF * k;
    front.radius = KB_FRONT_EDGE * k;
    scene.layers.push_back(front);
    CaseLayer seam = case_layer(CASE_GROOVE, translated(face, ImVec2(0, KB_FRONT_SEAM * k)));
    seam.edges = ImRect(face_box.Min.x - k, face_box.Max.y + (KB_FRONT_SEAM - KB_BOTTOM_RADIUS) * k, face_box.Max.x + k, FLT_MAX);
    seam.radius = KB_SEAM_WIDTH * k;
    seam.height = KB_SEAM_DEPTH * k;
    scene.layers.push_back(seam);
    CaseLayer top = case_layer(CASE_SOLID, face);
    shade_vertically(top, KB_FACE_TOP, KB_FACE_BOTTOM);
    top.height = (KB_FRONT_RELIEF + KB_FACE_RELIEF) * k;
    top.radius = KB_FACE_EDGE * k;
    scene.layers.push_back(top);
    if (layout.compact) {
        CaseLayer joint = case_layer(CASE_GROOVE, face);
        joint.edges = ImRect(face_box.Min.x - k, -FLT_MAX, face_box.Max.x + k, face_box.Min.y + k);
        joint.radius = JOINT_GROOVE_WIDTH * u;
        joint.height = JOINT_GROOVE_DEPTH * u;
        scene.layers.push_back(joint);
        return;
    }
    Shape left, right, middle;
    ImRect span;
    hinge_span(layout, left, right, middle, span);
    ImRect arch(bounds(middle).Min.x, -FLT_MAX, bounds(middle).Max.x, span.Max.y - 0.5f * u);
    CaseLayer rise = case_layer(CASE_RAISE, face);
    rise.axis_top = span.Min.y;
    rise.axis_bottom = span.Max.y;
    rise.height = span.GetHeight() * HINGE_ROUNDNESS;
    set_roll(rise, layout);
    scene.layers.push_back(rise);
    CaseLayer groove = case_layer(CASE_GROOVE, face);
    groove.edges = arch;
    groove.radius = ARCH_GROOVE_WIDTH * u;
    groove.height = ARCH_GROOVE_DEPTH * u;
    groove.tint = ARCH_GROOVE_SHADE;
    groove.taper = ARCH_GROOVE_TAPER * u;
    scene.layers.push_back(groove);
}

Shape rounded_rect(ImVec2 a, ImVec2 b, float radius) {
    Shape shape;
    add_arc(shape, ImVec2(b.x - radius, a.y + radius), radius, IM_PI * 1.5f, IM_PI * 2.0f, 16);
    add_arc(shape, ImVec2(b.x - radius, b.y - radius), radius, 0, IM_PI * 0.5f, 16);
    add_arc(shape, ImVec2(a.x + radius, b.y - radius), radius, IM_PI * 0.5f, IM_PI, 16);
    add_arc(shape, ImVec2(a.x + radius, a.y + radius), radius, IM_PI, IM_PI * 1.5f, 16);
    return shape;
}

CaseLayer shell_layer(const Shape &outline, float edge, float relief) {
    CaseLayer shell = case_layer(CASE_SOLID, outline);
    ImRect box = bounds(outline);
    shell.top_colour = mix(BEZEL, IM_COL32_WHITE, LID_SHEEN);
    shell.bottom_colour = BEZEL;
    shell.gradient_top = box.Min.y;
    shell.gradient_bottom = box.Min.y + box.GetHeight() * LID_SHEEN_REACH;
    shell.height = relief;
    shell.radius = edge;
    return shell;
}

void add_screen(CaseScene &scene, const DeviceLayout &layout, bool show_keys) {
    float u = layout.u;
    if (show_keys) {
        Frame frame{ layout.image_min, layout.image_max, u };
        Shape outline = rounded_rect(frame.lid(LID_FRAME[0], LID_FRAME[1]), frame.lid(LID_FRAME[2], LID_FRAME[3]), LID_FRAME_RADIUS * u);
        CaseLayer recess = case_layer(CASE_RECESS, outline, scaled_colour(BEZEL, 0.94f));
        recess.radius = SCREEN_WALL * u;
        recess.height = SCREEN_DEPTH * u;
        recess.tint = 1.0f;
        scene.layers.push_back(recess);
    }
    CaseLayer surround = case_layer(CASE_RECESS, rounded_rect(layout.image_min - ImVec2(2, 2), layout.image_max + ImVec2(2, 2), 3.0f), LCD_SURROUND);
    surround.shape = CASE_SHARP;
    surround.radius = 1.0f;
    surround.height = 1.0f;
    surround.tint = 1.0f;
    surround.grime = false;
    scene.layers.push_back(surround);
}

void add_lid(CaseScene &scene, const DeviceLayout &layout) {
    float u = layout.u;
    Shape body = lid_body(layout);
    ImRect box = bounds(body);
    scene.layers.push_back(shell_layer(body, LID_EDGE * u, LID_RELIEF * u));
    CaseLayer dish = case_layer(CASE_DISH, body);
    dish.edges = ImRect(-FLT_MAX, -FLT_MAX, FLT_MAX, box.Max.y - LID_DISH_WIDTH * u);
    dish.radius = LID_DISH_WIDTH * u;
    dish.height = LID_DISH_DEPTH * u;
    dish.taper = LID_DISH_TAPER * u;
    scene.layers.push_back(dish);
}

void add_gap(CaseScene &scene, const DeviceLayout &layout) {
    Shape left, right, middle;
    ImRect span;
    hinge_span(layout, left, right, middle, span);
    ImRect notch = bounds(lid_body(layout));
    float top = notch.Max.y - LID_NOTCH_DEPTH * layout.u;
    CaseLayer gap = case_layer(CASE_SOLID, rounded_rect(ImVec2(bounds(middle).Min.x, top), ImVec2(bounds(middle).Max.x, span.Min.y + 4.0f * layout.u), 0.5f), HINGE_GAP);
    gap.grime = false;
    scene.layers.push_back(gap);
}

CaseLayer recess_layer(const Shape &outline, CaseRecessShape shape, float wall, float depth, ImU32 top, ImU32 bottom, float tint) {
    CaseLayer recess = case_layer(CASE_RECESS, outline);
    shade_vertically(recess, top, bottom);
    recess.shape = shape;
    recess.radius = wall;
    recess.height = depth;
    recess.tint = tint;
    return recess;
}

void add_lid_wells(CaseScene &scene, const DeviceLayout &layout) {
    float u = layout.u;
    LidLayout lid = lid_layout(Frame{ layout.image_min, layout.image_max, u });
    for (int index = 0; index < 6; index++) {
        const Shape &flute = index < 5 ? lid.flute[index] : lid.power_flute;
        ImRect scoop = bounds(flute);
        CaseLayer recess = recess_layer(flute, CASE_TROUGH, scoop.GetHeight() * FLUTE_WALL, FLUTE_DEPTH * u, WELL_TOP, WELL_BOTTOM, WELL_TINT);
        if (index < 5) {
            const ImRect &key = lid.side_boxes[index];
            recess.fade_from = scoop.Min.x;
            recess.fade_to = key.Min.x + key.GetHeight() * 0.2f;
        } else {
            recess.fade_from = scoop.Max.x;
            recess.fade_to = lid.power_box.Max.x - lid.power_box.GetHeight() * 0.2f;
        }
        scene.layers.push_back(recess);
    }
    Shape light_well = pill(lid.light_box.Min - ImVec2(WELL_MARGIN, WELL_MARGIN) * u, lid.light_box.Max + ImVec2(WELL_MARGIN, WELL_MARGIN) * u);
    for (const Shape *well : { &lid.arrow_well, &light_well, &lid.menu_well }) {
        scene.layers.push_back(recess_layer(*well, CASE_BOWL, KEY_WELL_WALL * u, KEY_WELL_DEPTH * u, WELL_TOP, WELL_BOTTOM, WELL_TINT));
    }
    scene.layers.push_back(recess_layer(lid.esc_well, CASE_BOWL, FLAT_WELL_WALL * u, FLAT_WELL_DEPTH * u, WELL_FLOOR, WELL_FLOOR, WELL_TINT));
}

void add_keyboard_wells(CaseScene &scene, const DeviceLayout &layout) {
    KeyboardFrame frame = keyboard_frame(layout);
    float k = frame.kbu;
    ImRect face = bounds(keyboard_body(layout));
    float finger_y = (face.Max.y - frame.origin.y) / k + KB_FINGER_Y - KB_HEIGHT;
    Shape scoop = pill(frame.at(KB_FINGER_X - KB_FINGER_W * 0.5f, finger_y - KB_FINGER_H * 0.5f),
                       frame.at(KB_FINGER_X + KB_FINGER_W * 0.5f, finger_y + KB_FINGER_H * 0.5f));
    CaseLayer notch = recess_layer(scoop, CASE_TROUGH, KB_FINGER_H * 0.5f * k, SCOOP_DEPTH * k, SCOOP_TOP, SCOOP_BOTTOM, SCOOP_TINT);
    notch.level_floor = true;
    scene.layers.push_back(notch);
    Shape well;
    add_arc(well, frame.at(KB_WELL_X, KB_WELL_Y), KB_WELL_R * k, 0, IM_PI * 2.0f, 96);
    well.pop_back();
    scene.layers.push_back(recess_layer(well, CASE_BOWL, KB_WELL_SLOPE * k, CURSOR_WELL_DEPTH * k, 0, 0, 0.0f));
    for (const KeyboardKey &key : keyboard_keys) {
        Shape shape = keyboard_key_shape(frame, key);
        scene.layers.push_back(recess_layer(outset(shape, KEY_HOLE_GAP * k), CASE_SHARP, KEY_HOLE_CHAMFER * k, KEY_HOLE_CHAMFER * k, 0, 0, 0.0f));
        CaseLayer hole = recess_layer(outset(shape, (KEY_HOLE_GAP - KEY_HOLE_CHAMFER) * k), CASE_SHARP, KEY_HOLE_CHAMFER * k, KEY_HOLE_DEPTH * k,
                                      KEY_HOLE_DARK, KEY_HOLE_DARK, 1.0f);
        hole.grime = false;
        scene.layers.push_back(hole);
    }
}

CaseScene case_scene(const DeviceLayout &layout, const DeviceState &state, float scale) {
    CaseScene scene;
    scene.scale = scale;
    if (state.show_keys) {
        if (layout.has_keyboard && !layout.compact) add_gap(scene, layout);
        if (layout.has_keyboard && !layout.compact) add_hinge(scene, layout);
        if (layout.has_keyboard) add_keyboard(scene, layout);
        if (layout.has_keyboard) add_keyboard_wells(scene, layout);
        add_lid(scene, layout);
        add_lid_wells(scene, layout);
    } else {
        scene.layers.push_back(shell_layer(rounded_rect(layout.device_min, layout.device_max, layout.rounding), PLAIN_EDGE, PLAIN_RELIEF));
    }
    add_screen(scene, layout, state.show_keys);
    ImRect box(layout.device_min, layout.device_max);
    for (const CaseLayer &layer : scene.layers) box.Add(bounds(layer.outline));
    ImVec2 margin(CASE_MARGIN, CASE_MARGIN);
    scene.origin = ImVec2(floorf((box.Min.x - margin.x) * scale), floorf((box.Min.y - margin.y) * scale)) / scale;
    scene.width = (int)ceilf((box.Max.x + margin.x - scene.origin.x) * scale);
    scene.height = (int)ceilf((box.Max.y + margin.y - scene.origin.y) * scale);
    scene.ring = state.focused && !state.borderless;
    scene.ring_colour = FOCUS_RING;
    scene.ring_margin = RING_MARGIN;
    scene.ring_width = RING_WIDTH;
    scene.ring_bridge = state.show_keys ? RING_BRIDGE * layout.u : 0.0f;
    scene.shadow_offset = SHADOW_OFFSET;
    scene.shadow_blur = SHADOW_BLUR;
    scene.shadow_alpha = SHADOW_ALPHA;
    scene.wear = state.wear;
    scene.grime_scale = scale * layout.u;
    scene.wear_area = box;
    scene.scratches = scratch_alpha.empty() ? nullptr : scratch_alpha.data();
    scene.scratch_width = scratch_w;
    scene.scratch_height = scratch_h;
    scene.scratch_tint = CASE_SCRATCH_TINT;
    return scene;
}

void raster_case(SDL_Renderer *renderer, const CaseScene &scene) {
    case_raster(scene, case_pixels);
    if (scene.width <= 0 || scene.height <= 0) return;
    if (!case_texture || case_texture->w != scene.width || case_texture->h != scene.height) {
        if (case_texture) SDL_DestroyTexture(case_texture);
        case_texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, scene.width, scene.height);
        if (!case_texture) return;
        SDL_SetTextureScaleMode(case_texture, SDL_SCALEMODE_NEAREST);
        SDL_SetTextureBlendMode(case_texture, SDL_BLENDMODE_BLEND_PREMULTIPLIED);
    }
    SDL_UpdateTexture(case_texture, nullptr, case_pixels.data(), scene.width * 4);
    case_texture_min = scene.origin;
    case_texture_scale = scene.scale;
}

void show_case(ImDrawList *draw) {
    if (!case_texture) return;
    draw->AddImage((ImTextureID)(intptr_t)case_texture, case_texture_min,
                   case_texture_min + ImVec2((float)case_texture->w, (float)case_texture->h) / case_texture_scale);
}

void paint_overlay(ImDrawList *draw, const DeviceLayout &layout, const DeviceState &state, const uint8_t *down) {
    ImVec2 device_min = layout.device_min, image_min = layout.image_min, image_max = layout.image_max;
    float u = layout.u;
    wear_labels = state.wear;
    wear_grime = state.wear;
    if (state.show_keys) {
        float brand = 24.0f * u;
        ImVec2 at = image_min + ImVec2(-4 * u, -50 * u);
        erase_colour = faded(BEZEL, 0.9f);
        rub_mode = false;
        draw->AddText(text_font(), brand, at, PRINT, "SHAM");
        wear_patch(draw, at, at + text_size(text_font(), brand, "SHAM"), 1);
        draw->AddText(ImGui::GetFont(), 17.0f * u, at + ImVec2(text_size(text_font(), brand, "SHAM").x + 18 * u, 5 * u), PRINT, model_name.c_str());
        paint_lid_keys(draw, Frame{ image_min, image_max, u }, down);
        if (layout.has_keyboard) paint_keyboard(draw, keyboard_frame(layout), u, state, down + LID_KEY_COUNT);
    } else {
        draw->AddText(device_min + ImVec2(PLAIN_BEZEL, 8), IM_COL32(60, 66, 72, 255), ("SHAM  " + model_name).c_str());
    }

}


}

static float extra_lid_units(const DeviceState &state) {
    return state.show_keys && state.show_keyboard ? keyboard_extra_units(state.compact) : 0.0f;
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

    DeviceLayout layout = { device_min, device_max, image_min, image_max, u, rounding, has_keyboard, has_keyboard && state.compact };
    record_case(layout);
    ImGui::SetCursorScreenPos(image_min);
    ImGui::InvisibleButton("device", image_size);
    std::vector<uint8_t> down(LID_KEY_COUNT + KEYBOARD_KEY_COUNT, 0);
    if (state.show_keys) {
        input_lid_keys(Frame{ image_min, image_max, u }, state, down.data());
        if (has_keyboard) input_keyboard(keyboard_frame(layout), state, down.data() + LID_KEY_COUNT);
    }

    CaseKey case_key = { origin, ImVec2(avail_w, height), framebuffer_scale, state.show_keys, has_keyboard, state.wear, ring, layout.compact, model_name };
    OverlayKey overlay_key = { case_key, down };
    ImDrawList *draw = ImGui::GetWindowDrawList();
    if (!case_texture || !(case_key == baked_case)) {
        baked_case = case_key;
        load_scratches(renderer);
        raster_case(renderer, case_scene(layout, state, framebuffer_scale));
    }
    show_case(draw);
    if (bake_current(overlay_bake) && overlay_key == baked_overlay) {
        show_bake(overlay_bake, draw);
    } else {
        paint_overlay(begin_bake(overlay_bake, draw, origin, ImVec2(avail_w, height), framebuffer_scale), layout, state, down.data());
        finish_bake(overlay_bake, draw);
        pending_overlay = overlay_key;
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
    if (overlay_bake.pending) {
        flush_bake(overlay_bake, renderer);
        baked_overlay = pending_overlay;
    }
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
    if (scratch_texture) SDL_DestroyTexture(scratch_texture);
    scratch_texture = nullptr;
    if (case_texture) SDL_DestroyTexture(case_texture);
    case_texture = nullptr;
    destroy_bake(overlay_bake);
}
