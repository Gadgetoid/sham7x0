#include <algorithm>
#include <cmath>
#include <functional>
#include <thread>

#define IMGUI_DEFINE_MATH_OPERATORS

#include "case_raster.h"

namespace {

const float LIGHT_X = -0.32f;
const float LIGHT_Y = -0.78f;
const float LIGHT_Z = 1.0f;
const float AMBIENT = 0.55f;
const float DIFFUSE = 0.45f;
const float SPECULAR = 0.22f;
const float SHININESS = 28.0f;
const float DISTANCE_INFINITY = 1e20f;

struct Colour {
    float r, g, b;
};

Colour unpack(ImU32 colour) {
    return { (float)((colour >> IM_COL32_R_SHIFT) & 0xff), (float)((colour >> IM_COL32_G_SHIFT) & 0xff),
             (float)((colour >> IM_COL32_B_SHIFT) & 0xff) };
}

ImU32 pack(Colour colour) {
    auto channel = [](float value) { return (ImU32)std::max(0.0f, std::min(255.0f, value + 0.5f)); };
    return channel(colour.r) << IM_COL32_R_SHIFT | channel(colour.g) << IM_COL32_G_SHIFT | channel(colour.b) << IM_COL32_B_SHIFT;
}

Colour blend(Colour a, Colour b, float t) {
    return { a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t };
}

float clamp01(float value) {
    return std::max(0.0f, std::min(1.0f, value));
}

float smoothstep(float t) {
    t = clamp01(t);
    return t * t * (3 - 2 * t);
}

float quarter_round(float t) {
    t = clamp01(t);
    return sqrtf(1.0f - (1.0f - t) * (1.0f - t));
}

void parallel_rows(int rows, const std::function<void(int, int)> &work) {
    int threads = std::max(1, std::min((int)std::thread::hardware_concurrency(), rows / 64));
    if (threads == 1) {
        work(0, rows);
        return;
    }
    std::vector<std::thread> pool;
    for (int index = 0; index < threads; index++) {
        int from = rows * index / threads, to = rows * (index + 1) / threads;
        pool.emplace_back(work, from, to);
    }
    for (std::thread &thread : pool) thread.join();
}

struct Surface {
    int width = 0;
    int height = 0;
    std::vector<float> relief;
    std::vector<ImU32> albedo;
    std::vector<float> alpha;
    std::vector<float> grime;
};

struct Field {
    int left = 0, top = 0, width = 0, height = 0;
    float band = 0;
    std::vector<float> distance;
    std::vector<float> across;

    float at(int x, int y) const {
        int column = x - left, row = y - top;
        if (column < 0 || row < 0 || column >= width || row >= height) return -band;
        return distance[(size_t)row * width + column];
    }
};

void fill_inside(const std::vector<ImVec2> &outline, const ImRect *ends, Field &field) {
    std::vector<std::vector<float>> crossings(field.height), end_crossings(field.height);
    size_t count = outline.size();
    for (size_t i = 0; i < count; i++) {
        ImVec2 a = outline[i], b = outline[(i + 1) % count];
        if (a.y == b.y) continue;
        bool end = ends && ends->Contains((a + b) * 0.5f);
        int from = std::max(0, (int)ceilf(std::min(a.y, b.y) - 0.5f) - field.top);
        int to = std::min(field.height, (int)ceilf(std::max(a.y, b.y) - 0.5f) - field.top);
        for (int row = from; row < to; row++) {
            float y = field.top + row + 0.5f;
            float x = a.x + (y - a.y) * (b.x - a.x) / (b.y - a.y);
            crossings[row].push_back(x);
            if (end) end_crossings[row].push_back(x);
        }
    }
    if (ends) field.across.assign(field.distance.size(), FLT_MAX);
    for (int row = 0; row < field.height; row++) {
        std::vector<float> &line_crossings = crossings[row];
        std::sort(line_crossings.begin(), line_crossings.end());
        float *line = &field.distance[(size_t)row * field.width];
        for (size_t i = 0; i + 1 < line_crossings.size(); i += 2) {
            int from = std::max(0, (int)ceilf(line_crossings[i] - 0.5f) - field.left);
            int to = std::min(field.width, (int)ceilf(line_crossings[i + 1] - 0.5f) - field.left);
            for (int column = from; column < to; column++) {
                line[column] = field.band;
                if (end_crossings[row].empty()) continue;
                float x = field.left + column + 0.5f, nearest = FLT_MAX;
                for (float end : end_crossings[row]) nearest = std::min(nearest, fabsf(x - end));
                field.across[(size_t)row * field.width + column] = nearest;
            }
        }
    }
}

void measure_edges(const std::vector<ImVec2> &outline, const ImRect &edges, Field &field) {
    struct Segment {
        ImVec2 a, along;
        float length_squared;
        int x0, x1, y0, y1;
    };
    std::vector<Segment> segments;
    size_t count = outline.size();
    for (size_t i = 0; i < count; i++) {
        ImVec2 a = outline[i], b = outline[(i + 1) % count];
        if (!edges.Contains((a + b) * 0.5f)) continue;
        Segment segment;
        segment.a = a;
        segment.along = b - a;
        segment.length_squared = segment.along.x * segment.along.x + segment.along.y * segment.along.y;
        segment.x0 = std::max(field.left, (int)floorf(std::min(a.x, b.x) - field.band));
        segment.x1 = std::min(field.left + field.width, (int)ceilf(std::max(a.x, b.x) + field.band));
        segment.y0 = std::max(field.top, (int)floorf(std::min(a.y, b.y) - field.band));
        segment.y1 = std::min(field.top + field.height, (int)ceilf(std::max(a.y, b.y) + field.band));
        if (segment.x0 < segment.x1 && segment.y0 < segment.y1) segments.push_back(segment);
    }
    parallel_rows(field.height, [&](int from, int to) {
        for (const Segment &segment : segments) {
            int y0 = std::max(segment.y0, field.top + from), y1 = std::min(segment.y1, field.top + to);
            for (int y = y0; y < y1; y++) {
                float *line = &field.distance[(size_t)(y - field.top) * field.width - field.left];
                for (int x = segment.x0; x < segment.x1; x++) {
                    ImVec2 point(x + 0.5f - segment.a.x, y + 0.5f - segment.a.y);
                    float t = segment.length_squared > 0 ? clamp01((point.x * segment.along.x + point.y * segment.along.y) / segment.length_squared) : 0.0f;
                    ImVec2 offset = point - segment.along * t;
                    float distance_squared = offset.x * offset.x + offset.y * offset.y;
                    float &value = line[x];
                    if (distance_squared < value * value) value = value >= 0 ? sqrtf(distance_squared) : -sqrtf(distance_squared);
                }
            }
        }
    });
}

void soften_across(Field &field, int reach) {
    std::vector<float> column(field.height);
    for (int x = 0; x < field.width; x++) {
        for (int y = 0; y < field.height; y++) column[y] = field.across[(size_t)y * field.width + x];
        for (int y = 0; y < field.height; y++) {
            if (column[y] == FLT_MAX) continue;
            float total = 0;
            int samples = 0;
            for (int offset = -reach; offset <= reach; offset++) {
                int row = y + offset;
                if (row < 0 || row >= field.height || column[row] == FLT_MAX) continue;
                total += column[row];
                samples++;
            }
            field.across[(size_t)y * field.width + x] = total / samples;
        }
    }
}

Field signed_field(const std::vector<ImVec2> &outline, const ImRect &edges, float band, const Surface &surface, const ImRect *ends = nullptr) {
    Field field;
    field.band = band;
    ImRect box(outline[0], outline[0]);
    for (const ImVec2 &point : outline) box.Add(point);
    field.left = std::max(0, (int)floorf(box.Min.x - band));
    field.top = std::max(0, (int)floorf(box.Min.y - band));
    field.width = std::max(0, std::min(surface.width, (int)ceilf(box.Max.x + band)) - field.left);
    field.height = std::max(0, std::min(surface.height, (int)ceilf(box.Max.y + band)) - field.top);
    field.distance.assign((size_t)field.width * field.height, -band);
    fill_inside(outline, ends, field);
    if (ends) soften_across(field, 4);
    measure_edges(outline, edges, field);
    return field;
}

struct Placement {
    ImVec2 origin;
    float scale;

    ImVec2 pixel(ImVec2 point) const {
        return (point - origin) * scale;
    }

    float pixel_x(float x) const {
        return fabsf(x) >= FLT_MAX ? x : (x - origin.x) * scale;
    }

    float pixel_y(float y) const {
        return fabsf(y) >= FLT_MAX ? y : (y - origin.y) * scale;
    }

    float logical_x(int x) const {
        return origin.x + (x + 0.5f) / scale;
    }

    float logical_y(int y) const {
        return origin.y + (y + 0.5f) / scale;
    }
};

Colour layer_colour(const CaseLayer &layer, const Placement &place, int y) {
    Colour top = unpack(layer.top_colour), bottom = unpack(layer.bottom_colour);
    if (layer.gradient_bottom <= layer.gradient_top) return top;
    return blend(top, bottom, clamp01((place.logical_y(y) - layer.gradient_top) / (layer.gradient_bottom - layer.gradient_top)));
}

float fade_at(const CaseLayer &layer, const Placement &place, int x) {
    if (layer.fade_from == layer.fade_to) return 1.0f;
    return smoothstep((place.logical_x(x) - layer.fade_from) / (layer.fade_to - layer.fade_from));
}

float recess_profile(CaseRecessShape shape, float t) {
    if (shape == CASE_TROUGH) return quarter_round(t);
    if (shape == CASE_SHARP) return clamp01(t);
    return smoothstep(t);
}

float length_of(ImVec2 vector) {
    return sqrtf(vector.x * vector.x + vector.y * vector.y);
}

float turning(ImVec2 before, ImVec2 at, ImVec2 after) {
    ImVec2 a = at - before, b = after - at;
    float lengths = sqrtf((a.x * a.x + a.y * a.y) * (b.x * b.x + b.y * b.y));
    if (lengths <= 0) return 0;
    return acosf(std::max(-1.0f, std::min(1.0f, (a.x * b.x + a.y * b.y) / lengths)));
}

std::vector<ImVec2> smoothed(const std::vector<ImVec2> &outline, float spacing) {
    const float gentle = 0.6f;
    size_t count = outline.size();
    std::vector<ImVec2> result;
    for (size_t i = 0; i < count; i++) {
        ImVec2 p0 = outline[(i + count - 1) % count], p1 = outline[i], p2 = outline[(i + 1) % count], p3 = outline[(i + 2) % count];
        result.push_back(p1);
        if (turning(p0, p1, p2) > gentle || turning(p1, p2, p3) > gentle) continue;
        float before = length_of(p1 - p0), length = length_of(p2 - p1), after = length_of(p3 - p2);
        if (length > 3.0f * std::min(before, after) || length * 3.0f < std::max(before, after)) continue;
        int pieces = (int)(length / spacing);
        for (int piece = 1; piece < pieces; piece++) {
            float t = (float)piece / pieces, t2 = t * t, t3 = t2 * t;
            result.push_back((p1 * 2.0f + (p2 - p0) * t + (p0 * 2.0f - p1 * 5.0f + p2 * 4.0f - p3) * t2 + (p1 * 3.0f - p0 - p2 * 3.0f + p3) * t3) * 0.5f);
        }
    }
    return result;
}

float hinge_profile(const CaseLayer &layer, float y, float scale) {
    float centre = (layer.axis_top + layer.axis_bottom) * 0.5f, half = (layer.axis_bottom - layer.axis_top) * 0.5f;
    float base = layer.base * scale, height = layer.height * scale;
    float across = (y - centre) / half;
    float join = layer.roll_join;
    bool rolls = layer.roll_end > centre + join * half;
    if (!rolls || across <= join) {
        if (fabsf(across) < 1.0f) return base + height * sqrtf(1.0f - across * across);
        return base - (fabsf(across) - 1.0f) * height * 8.0f;
    }
    float y0 = centre + join * half, y1 = layer.roll_end, span = y1 - y0;
    float t = (y - y0) / span;
    float level = layer.roll_level * scale;
    if (t >= 1.0f) return level;
    float start = base + height * sqrtf(1.0f - join * join);
    float slope = -height * join / (sqrtf(1.0f - join * join) * half) * span;
    float t3 = t * t * t, t4 = t3 * t, t5 = t4 * t;
    float arrive = 10 * t3 - 15 * t4 + 6 * t5;
    return start * (1.0f - arrive) + slope * (t - 6 * t3 + 8 * t4 - 3 * t5) + level * arrive;
}

void apply_dish(const CaseLayer &layer, const Placement &place, Surface &surface) {
    const ImRect &box = layer.edges;
    float band = std::max(0.001f, layer.radius);
    float softness = band * 0.3f;
    float height = layer.height * place.scale;
    int x0 = std::max(0, (int)floorf(place.pixel_x(box.Min.x))), x1 = std::min(surface.width, (int)ceilf(place.pixel_x(box.Max.x)));
    int y0 = std::max(0, (int)floorf(place.pixel_y(box.Min.y))), y1 = std::min(surface.height, (int)ceilf(place.pixel_y(box.Max.y)));
    parallel_rows(std::max(0, y1 - y0), [&](int from, int to) {
        for (int y = y0 + from; y < y0 + to; y++) {
            float top = place.logical_y(y) - box.Min.y;
            for (int x = x0; x < x1; x++) {
                size_t index = (size_t)y * surface.width + x;
                if (surface.alpha[index] <= 0) continue;
                float left = place.logical_x(x) - box.Min.x, right = box.Max.x - place.logical_x(x);
                float nearest = std::min(top, std::min(left, right));
                float spread = expf((nearest - top) / softness) + expf((nearest - left) / softness) + expf((nearest - right) / softness);
                float distance = nearest - softness * logf(spread);
                float t = clamp01(distance / band);
                float falling = 1.0f - t;
                surface.relief[index] -= height * (1.0f - falling * falling * falling) * smoothstep(t / 0.15f);
            }
        }
    });
}

void apply_layer(const CaseLayer &layer, const Placement &place, Surface &surface) {
    if (layer.kind == CASE_DISH) {
        apply_dish(layer, place, surface);
        return;
    }
    if (layer.outline.size() < 3) return;
    std::vector<ImVec2> outline;
    for (const ImVec2 &point : layer.outline) outline.push_back(place.pixel(point));
    float radius = std::max(0.0f, layer.radius * place.scale);
    outline = smoothed(outline, std::max(1.5f, radius / 8.0f));
    ImRect edges(place.pixel_x(layer.edges.Min.x), place.pixel_y(layer.edges.Min.y), place.pixel_x(layer.edges.Max.x), place.pixel_y(layer.edges.Max.y));
    bool full_edges = layer.kind != CASE_GROOVE;
    ImRect everything(-FLT_MAX, -FLT_MAX, FLT_MAX, FLT_MAX);
    bool cylinder = layer.kind == CASE_CYLINDER;
    float band = cylinder || layer.kind == CASE_RAISE ? 0.0f : radius;
    Field field = signed_field(outline, full_edges ? everything : edges, band + 2.0f, surface, cylinder ? &edges : nullptr);
    float height = layer.height * place.scale, base = layer.base * place.scale;
    float axis_half = (layer.axis_bottom - layer.axis_top) * 0.5f;

    parallel_rows(field.height, [&](int from, int to) {
        for (int row = from; row < to; row++) {
            int y = field.top + row;
            Colour colour = layer_colour(layer, place, y);
            for (int column = 0; column < field.width; column++) {
                int x = field.left + column;
                float distance = field.distance[(size_t)row * field.width + column];
                size_t index = (size_t)y * surface.width + x;
                if (layer.kind == CASE_SOLID || layer.kind == CASE_CYLINDER) {
                    float cover = clamp01(distance + 0.5f);
                    if (cover <= 0) continue;
                    float level;
                    if (cylinder) {
                        float end = radius > 0 ? quarter_round(field.across[(size_t)row * field.width + column] / radius) : 1.0f;
                        level = std::max(0.0f, hinge_profile(layer, place.logical_y(y), place.scale)) * end;
                    } else {
                        level = base + height * (radius > 0 ? quarter_round(std::max(0.0f, distance) / radius) : 1.0f);
                    }
                    float covered = surface.alpha[index];
                    float uncovered = 1.0f - covered;
                    float replaced = covered > 0 ? std::max(0.0f, cover - uncovered) / covered : 0.0f;
                    float combined = std::min(1.0f, covered + cover);
                    surface.relief[index] = surface.relief[index] * (1.0f - replaced) + level * cover;
                    surface.albedo[index] = pack(blend(unpack(surface.albedo[index]), colour, cover / combined));
                    surface.alpha[index] = combined;
                    surface.grime[index] += ((layer.grime ? 1.0f : 0.0f) - surface.grime[index]) * cover;
                } else if (layer.kind == CASE_RECESS) {
                    if (distance <= -0.5f) continue;
                    float depth = recess_profile(layer.shape, radius > 0 ? (distance + 0.5f) / radius : 1.0f) * fade_at(layer, place, x);
                    if (depth <= 0) continue;
                    if (layer.level_floor) surface.relief[index] += (base - height * depth - surface.relief[index]) * std::min(1.0f, depth * 4.0f);
                    else surface.relief[index] -= height * depth;
                    surface.albedo[index] = pack(blend(unpack(surface.albedo[index]), colour, depth * layer.tint));
                    if (!layer.grime) surface.grime[index] *= 1.0f - clamp01(depth * 2.0f);
                } else if (layer.kind == CASE_GROOVE) {
                    float t = radius > 0 ? 1.0f - fabsf(distance) / radius : 0.0f;
                    if (t <= 0) continue;
                    float spread = fabsf(distance) / radius * 3.0f;
                    float depth = expf(-0.5f * spread * spread) * fade_at(layer, place, x);
                    if (layer.taper > 0) depth *= smoothstep((edges.Max.y - y - 0.5f) / (layer.taper * place.scale));
                    surface.relief[index] -= height * depth;
                    Colour under = unpack(surface.albedo[index]);
                    surface.albedo[index] = pack(blend(under, Colour{ 0, 0, 0 }, depth * layer.tint));
                } else if (layer.kind == CASE_RAISE) {
                    if (distance <= -1.5f || surface.alpha[index] <= 0 || axis_half <= 0) continue;
                    float logical_y = place.logical_y(y);
                    if (logical_y >= layer.roll_end) continue;
                    surface.relief[index] = std::max(surface.relief[index], hinge_profile(layer, logical_y, place.scale));
                }
            }
        }
    });
}

void distance_1d(const float *input, float *output, int count, std::vector<int> &hull, std::vector<float> &bounds) {
    int k = 0;
    hull[0] = 0;
    bounds[0] = -DISTANCE_INFINITY;
    bounds[1] = DISTANCE_INFINITY;
    for (int q = 1; q < count; q++) {
        float s;
        while (true) {
            int p = hull[k];
            s = ((input[q] + (float)q * q) - (input[p] + (float)p * p)) / (2.0f * (q - p));
            if (s <= bounds[k] && k > 0) k--;
            else break;
        }
        if (s <= bounds[k]) {
            hull[0] = q;
            bounds[0] = -DISTANCE_INFINITY;
            bounds[1] = DISTANCE_INFINITY;
            k = 0;
            continue;
        }
        k++;
        hull[k] = q;
        bounds[k] = s;
        bounds[k + 1] = DISTANCE_INFINITY;
    }
    k = 0;
    for (int q = 0; q < count; q++) {
        while (bounds[k + 1] < q) k++;
        int p = hull[k];
        output[q] = (float)(q - p) * (q - p) + input[p];
    }
}

void distance_transform(std::vector<float> &grid, int width, int height) {
    parallel_rows(width, [&](int from, int to) {
        std::vector<float> column(height), result(height);
        std::vector<int> hull(height);
        std::vector<float> bounds(height + 1);
        for (int x = from; x < to; x++) {
            for (int y = 0; y < height; y++) column[y] = grid[(size_t)y * width + x];
            distance_1d(column.data(), result.data(), height, hull, bounds);
            for (int y = 0; y < height; y++) grid[(size_t)y * width + x] = result[y];
        }
    });
    parallel_rows(height, [&](int from, int to) {
        std::vector<float> result(width);
        std::vector<int> hull(width);
        std::vector<float> bounds(width + 1);
        for (int y = from; y < to; y++) {
            float *line = &grid[(size_t)y * width];
            distance_1d(line, result.data(), width, hull, bounds);
            for (int x = 0; x < width; x++) line[x] = sqrtf(result[x]);
        }
    });
}

struct Grid {
    int width = 0, height = 0, pad = 0;
    float step = 1.0f;
    std::vector<float> values;

    float sample(float x, float y) const {
        float gx = x / step - 0.5f + pad, gy = y / step - 0.5f + pad;
        int x0 = std::max(0, std::min(width - 2, (int)floorf(gx))), y0 = std::max(0, std::min(height - 2, (int)floorf(gy)));
        float fx = clamp01(gx - x0), fy = clamp01(gy - y0);
        const float *row0 = &values[(size_t)y0 * width + x0], *row1 = row0 + width;
        float top = row0[0] + (row0[1] - row0[0]) * fx, bottom = row1[0] + (row1[1] - row1[0]) * fx;
        return top + (bottom - top) * fy;
    }
};

Grid outside_distance(const Surface &surface, float scale, int pad) {
    Grid grid;
    grid.step = 1.0f;
    grid.pad = pad;
    grid.width = surface.width + 2 * pad;
    grid.height = surface.height + 2 * pad;
    grid.values.assign((size_t)grid.width * grid.height, DISTANCE_INFINITY);
    for (int y = 0; y < surface.height; y++) {
        for (int x = 0; x < surface.width; x++) {
            float alpha = surface.alpha[(size_t)y * surface.width + x];
            if (alpha <= 0) continue;
            float gap = std::max(0.0f, 0.5f - alpha);
            grid.values[(size_t)(y + pad) * grid.width + x + pad] = gap * gap;
        }
    }
    distance_transform(grid.values, grid.width, grid.height);
    for (float &value : grid.values) value /= scale;
    return grid;
}

Grid closed_distance(const Grid &outside, float bridge, float scale) {
    Grid grid = outside;
    for (float &value : grid.values) {
        float beyond = (value - bridge) * scale;
        value = beyond > 0 && beyond < 1.5f ? beyond * beyond : DISTANCE_INFINITY;
    }
    distance_transform(grid.values, grid.width, grid.height);
    for (size_t i = 0; i < grid.values.size(); i++) {
        grid.values[i] = outside.values[i] > bridge ? DISTANCE_INFINITY : bridge - grid.values[i] / scale;
    }
    return grid;
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

Colour grimed(Colour colour, int x, int y, float smudge, bool wear) {
    float grain_strength = wear ? 0.13f : 0.10f;
    float dirt_strength = wear ? 0.0f : 0.10f;
    uint32_t speck_mask = wear ? 0x7ff : 0x1fff;
    float grain = unit_noise(x, y, 1) - 0.5f;
    float blotch = smooth_noise(x / smudge, y / smudge, 2) * 0.6f + smooth_noise(x / (smudge * 0.35f), y / (smudge * 0.35f), 3) * 0.4f;
    float dirt = std::max(0.0f, blotch - 0.62f) * 2.2f;
    float speck = (hash2(x, y, 4) & speck_mask) == 0 ? 0.55f : 0.0f;
    float shade = grain * grain_strength - dirt * dirt_strength - speck;
    float target = shade >= 0 ? 255.0f : 0.0f;
    return blend(colour, Colour{ target, target, target }, std::min(1.0f, fabsf(shade)));
}

float scratch_at(const CaseScene &scene, float u, float v) {
    float fx = u * scene.scratch_width - 0.5f, fy = v * scene.scratch_height - 0.5f;
    int x0 = (int)floorf(fx), y0 = (int)floorf(fy);
    float tx = fx - x0, ty = fy - y0;
    auto texel = [&](int x, int y) {
        x = ((x % scene.scratch_width) + scene.scratch_width) % scene.scratch_width;
        y = ((y % scene.scratch_height) + scene.scratch_height) % scene.scratch_height;
        return scene.scratches[(size_t)y * scene.scratch_width + x] / 255.0f;
    };
    float top = texel(x0, y0) + (texel(x0 + 1, y0) - texel(x0, y0)) * tx;
    float bottom = texel(x0, y0 + 1) + (texel(x0 + 1, y0 + 1) - texel(x0, y0 + 1)) * tx;
    return top + (bottom - top) * ty;
}

}

void case_raster(const CaseScene &scene, std::vector<uint32_t> &pixels) {
    Surface surface;
    surface.width = scene.width;
    surface.height = scene.height;
    size_t count = (size_t)scene.width * scene.height;
    surface.relief.assign(count, 0.0f);
    surface.albedo.assign(count, 0);
    surface.alpha.assign(count, 0.0f);
    surface.grime.assign(count, 0.0f);
    pixels.assign(count, 0);
    if (count == 0) return;
    Placement place{ scene.origin, scene.scale };
    for (const CaseLayer &layer : scene.layers) apply_layer(layer, place, surface);

    float length = sqrtf(LIGHT_X * LIGHT_X + LIGHT_Y * LIGHT_Y + LIGHT_Z * LIGHT_Z);
    float lx = LIGHT_X / length, ly = LIGHT_Y / length, lz = LIGHT_Z / length;
    float hx = lx, hy = ly, hz = lz + 1.0f;
    float half_length = sqrtf(hx * hx + hy * hy + hz * hz);
    hx /= half_length;
    hy /= half_length;
    hz /= half_length;
    float flat_light = AMBIENT + DIFFUSE * lz;
    float flat_highlight = powf(hz, SHININESS);
    float smudge = 90.0f * scene.grime_scale;
    float wear_width = std::max(1.0f, scene.wear_area.GetWidth());
    float scratch_alpha = ((scene.scratch_tint >> IM_COL32_A_SHIFT) & 0xff) / 255.0f;
    Colour scratch_colour = unpack(scene.scratch_tint);
    bool scratched = scene.wear && scene.scratches && scene.scratch_width > 0;

    Grid outside = outside_distance(surface, scene.scale, (int)ceilf((scene.ring_bridge + 2.0f) * scene.scale));
    Grid ring_distance;
    float ring_target = scene.ring_margin;
    if (scene.ring && scene.ring_bridge > scene.ring_margin) ring_distance = closed_distance(outside, scene.ring_bridge, scene.scale);
    Colour ring_colour = unpack(scene.ring_colour);
    float ring_alpha = ((scene.ring_colour >> IM_COL32_A_SHIFT) & 0xff) / 255.0f;

    parallel_rows(surface.height, [&](int from, int to) {
        for (int y = from; y < to; y++) {
            for (int x = 0; x < surface.width; x++) {
                size_t index = (size_t)y * surface.width + x;
                float px = x + 0.5f, py = y + 0.5f;
                Colour below{ 0, 0, 0 };
                float shadow_distance = outside.sample(px, py - scene.shadow_offset * scene.scale);
                float shadow = scene.shadow_alpha * (1.0f - smoothstep(shadow_distance / std::max(0.01f, scene.shadow_blur)));
                float below_alpha = shadow;
                if (scene.ring) {
                    float value = ring_distance.values.empty() ? outside.sample(px, py) : ring_distance.sample(px, py);
                    float line = clamp01((scene.ring_width * 0.5f - fabsf(value - ring_target)) * scene.scale + 0.5f);
                    if (outside.sample(px, py) <= 0.0f) line = 0;
                    float a = line * ring_alpha;
                    below = Colour{ ring_colour.r * a, ring_colour.g * a, ring_colour.b * a };
                    below_alpha = a + shadow * (1.0f - a);
                }
                float alpha = surface.alpha[index];
                Colour colour{ 0, 0, 0 };
                if (alpha > 0) {
                    int left = std::max(0, x - 1), right = std::min(surface.width - 1, x + 1);
                    int up = std::max(0, y - 1), down = std::min(surface.height - 1, y + 1);
                    float dx = (surface.relief[(size_t)y * surface.width + right] - surface.relief[(size_t)y * surface.width + left]) / (float)(right - left);
                    float dy = (surface.relief[(size_t)down * surface.width + x] - surface.relief[(size_t)up * surface.width + x]) / (float)(down - up);
                    float nx = -dx, ny = -dy, nz = 1.0f;
                    float normal_length = sqrtf(nx * nx + ny * ny + nz * nz);
                    nx /= normal_length;
                    ny /= normal_length;
                    nz /= normal_length;
                    float diffuse = std::max(0.0f, nx * lx + ny * ly + nz * lz);
                    float light = (AMBIENT + DIFFUSE * diffuse) / flat_light;
                    float highlight = std::max(0.0f, powf(std::max(0.0f, nx * hx + ny * hy + nz * hz), SHININESS) - flat_highlight) * SPECULAR * 255.0f;
                    Colour base = unpack(surface.albedo[index]);
                    float grime = surface.grime[index];
                    if (grime > 0) {
                        Colour dirty = grimed(base, x, y, smudge, scene.wear);
                        if (scratched) {
                            float wear_u = (place.logical_x(x) - scene.wear_area.Min.x) / wear_width;
                            float wear_v = (place.logical_y(y) - scene.wear_area.Min.y) / wear_width;
                            float scratch = scratch_at(scene, wear_u * 0.55f, wear_v * 0.55f * scene.scratch_width / scene.scratch_height);
                            dirty = blend(dirty, scratch_colour, scratch * scratch_alpha);
                        }
                        base = blend(base, dirty, grime);
                    }
                    colour = Colour{ base.r * light + highlight, base.g * light + highlight, base.b * light + highlight };
                }
                float dither = unit_noise(x, y, 9) - 0.5f;
                Colour out{ colour.r * alpha + below.r * (1.0f - alpha) + dither, colour.g * alpha + below.g * (1.0f - alpha) + dither,
                            colour.b * alpha + below.b * (1.0f - alpha) + dither };
                float out_alpha = alpha + below_alpha * (1.0f - alpha);
                pixels[index] = pack(out) | (ImU32)(clamp01(out_alpha) * 255.0f + 0.5f) << IM_COL32_A_SHIFT;
            }
        }
    });
}
