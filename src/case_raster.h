#pragma once
#include <cfloat>
#include <cstdint>
#include <vector>

#include "imgui.h"
#include "imgui_internal.h"

enum CaseLayerKind { CASE_SOLID, CASE_CYLINDER, CASE_RECESS, CASE_GROOVE, CASE_RAISE };

enum CaseRecessShape { CASE_BOWL, CASE_TROUGH, CASE_SHARP };

struct CaseLayer {
    CaseLayerKind kind = CASE_SOLID;
    std::vector<ImVec2> outline;
    ImRect edges = ImRect(-FLT_MAX, -FLT_MAX, FLT_MAX, FLT_MAX);
    ImU32 top_colour = 0;
    ImU32 bottom_colour = 0;
    float gradient_top = 0;
    float gradient_bottom = 0;
    float base = 0;
    float height = 0;
    float radius = 0;
    float axis_top = 0;
    float axis_bottom = 0;
    CaseRecessShape shape = CASE_BOWL;
    float tint = 0;
    float fade_from = 0;
    float fade_to = 0;
    bool outside_only = false;
    bool grime = true;
};

struct CaseScene {
    ImVec2 origin;
    int width = 0;
    int height = 0;
    float scale = 1.0f;
    std::vector<CaseLayer> layers;
    bool ring = false;
    ImU32 ring_colour = 0;
    float ring_margin = 3.0f;
    float ring_width = 2.0f;
    float ring_bridge = 0.0f;
    float shadow_offset = 4.0f;
    float shadow_blur = 4.0f;
    float shadow_alpha = 0.35f;
    bool wear = false;
    float grime_scale = 1.0f;
    ImRect wear_area;
    const uint8_t *scratches = nullptr;
    int scratch_width = 0;
    int scratch_height = 0;
    ImU32 scratch_tint = 0;
};

void case_raster(const CaseScene &scene, std::vector<uint32_t> &pixels);
