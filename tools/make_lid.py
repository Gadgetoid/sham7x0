import math
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import svg_keyboard as svg

GLASS_UNITS = 282.0
MARGIN_X = 3


def rounded_rect(x, y, w, h, r):
    points = []
    for cx, cy, start in ((x + w - r, y + r, -90), (x + w - r, y + h - r, 0), (x + r, y + h - r, 90), (x + r, y + r, 180)):
        for i in range(9):
            angle = math.radians(start + i * 90 / 8)
            points.append((cx + r * math.cos(angle), cy + r * math.sin(angle)))
    return points


def ellipse_points(cx, cy, rx, ry):
    return [(cx + rx * math.cos(2 * math.pi * i / 64), cy + ry * math.sin(2 * math.pi * i / 64)) for i in range(64)]


def shapes(path):
    found = []
    for tag, body, transform, group in svg.elements(path):
        value = lambda name: svg.attribute(body, name)
        fill = re.search(r"fill:(#[0-9a-fA-F]{6})", value("style") or "")
        fill = fill.group(1).lower() if fill else ""
        if tag == "path":
            paths = svg.subpaths(value("d"))
        elif tag == "rect":
            x, y, w, h = (float(value(n)) for n in ("x", "y", "width", "height"))
            paths = [rounded_rect(x, y, w, h, float(value("ry") or value("rx") or 0))]
        else:
            rx = float(value("r") or value("rx"))
            ry = float(value("r") or value("ry"))
            paths = [ellipse_points(float(value("cx")), float(value("cy")), rx, ry)]
        paths = [[svg.apply(transform, p) for p in path] for path in paths]
        found.append({"tag": tag, "fill": fill, "paths": paths, "points": [p for path in paths for p in path],
                      "label": value("inkscape:label") or "", "group": group})
    for shape in found:
        xs = [p[0] for p in shape["points"]]
        ys = [p[1] for p in shape["points"]]
        shape["box"] = (min(xs), min(ys), max(xs), max(ys))
        shape["centre"] = ((min(xs) + max(xs)) / 2, (min(ys) + max(ys)) / 2)
        shape["area"] = (max(xs) - min(xs)) * (max(ys) - min(ys))
    return found


LABELS = {
    "glass": "screen-area", "frame": "screen-recess", "light": "button-backlight", "light_icon": "icon-backlight",
    "power": "button-power", "power_icon": "icon-power", "power_flute": "recess-power", "menu": "button-menu",
    "menu_well": "recess-menu", "arrow_well": "up-down-buttons-recess", "up": "button-up", "down": "button-down",
    "up_icon": "icon-arrow-up", "down_icon": "icon-arrow-down", "esc_well": "enter-esc-recess", "esc": "button-esc",
    "enter": "button-enter", "body": "lid-shape", "hinge_left": "lid-hinge-left", "hinge_right": "lid-hinge-right",
    "keyboard_hinge": "keyboard-hinge", "keyboard_body": "keyboard-shape",
}
SIDES = ("main", "tel", "cal", "memo", "prog")


def classify(found):
    by_label = {}
    for shape in found:
        if shape["group"] in ("lid", "Layer 1") and shape["label"]:
            by_label[shape["label"]] = shape
    named = {name: by_label[label] for name, label in LABELS.items()}
    for index, side in enumerate(SIDES):
        named["side_{}".format(index)] = by_label["button-" + side]
        named["flute_{}".format(index)] = by_label["recess-" + side]
    return named


def main():
    found = shapes(sys.argv[1])
    named = classify(found)
    gx0, gy0, gx1, gy1 = named["glass"]["box"]
    scale = GLASS_UNITS / (gy1 - gy0)
    centre_x = (gx0 + gx1) / 2

    def unit(point):
        return (point[0] - centre_x) * scale, (point[1] - gy0) * scale

    grid_w = 240 + 2 * MARGIN_X
    grid_h = round(grid_w * (gy1 - gy0) / (gx1 - gx0))
    margin_y = (grid_h - 80) // 2
    body = [unit(p) for p in named["body"]["points"]]
    half_width = GLASS_UNITS * grid_w / (80 + 2 * margin_y) / 2
    min_x = min(p[0] for p in body)
    max_x = max(p[0] for p in body)
    min_y = min(p[1] for p in body)
    max_y = max(p[1] for p in body)
    keyboard = [unit(p) for p in named["keyboard_body"]["points"]]
    keyboard_left = min(p[0] for p in keyboard)
    shoulder = min(p[1] for p in keyboard if p[0] < keyboard_left + 15 * scale)
    lines = [
        "#pragma once",
        "",
        "struct LidShape {",
        "    const float *points;",
        "    const int *counts;",
        "    int regions;",
        "    const float *triangles;",
        "    int triangle_count;",
        "    const int *indices;",
        "    int index_count;",
        "};",
        "",
        "static const int LID_LCD_MARGIN_X = {};".format(MARGIN_X),
        "static const int LID_LCD_MARGIN_Y = {};".format(margin_y),
        "static const float LID_LEFT_EXTENT = {:.2f}f;".format(-half_width - min_x),
        "static const float LID_RIGHT_EXTENT = {:.2f}f;".format(max_x - half_width),
        "static const float LID_TOP_EXTENT = {:.2f}f;".format(-min_y),
        "static const float LID_BOTTOM_EXTENT = {:.2f}f;".format(max_y - GLASS_UNITS),
        "static const float LID_BODY_RADIUS = {:.2f}f;".format(12.0 * scale),
        "static const float LID_KEYBOARD_BOTTOM = {:.2f}f;".format(max(p[1] for p in keyboard)),
        "static const float LID_COMPACT_SHIFT = {:.2f}f;".format(shoulder - max_y),
        "static const float LID_UNITS_PER_MM = {:.5f}f;".format(scale),
        "static const float LID_GLASS_MM_X = {:.4f}f;".format(centre_x),
        "static const float LID_GLASS_MM_Y = {:.4f}f;".format(gy0),
    ]
    x0, y0, x1, y1 = named["frame"]["box"]
    (fx0, fy0), (fx1, fy1) = unit((x0, y0)), unit((x1, y1))
    lines.append("static const float LID_FRAME[] = {{ {:.2f}f, {:.2f}f, {:.2f}f, {:.2f}f }};".format(fx0, fy0, fx1, fy1))
    lines.append("static const float LID_FRAME_RADIUS = {:.2f}f;".format(3.7546248 * scale))
    lines.append("")
    order = ["side_{}".format(i) for i in range(5)] + ["flute_{}".format(i) for i in range(5)] + [
        "light", "light_icon", "power", "power_icon", "power_flute", "menu", "menu_well", "arrow_well", "up", "down", "up_icon",
        "down_icon", "esc_well", "esc", "enter", "body", "hinge_left", "hinge_right", "keyboard_hinge", "keyboard_body"]
    for name in order:
        shape = named[name]
        polygons = shape["paths"] if name.endswith("_icon") else [shape["paths"][0]]
        polygons = [[unit(p) for p in polygon] for polygon in polygons]
        if not name.endswith("_icon") and svg.area(polygons[0]) < 0:
            polygons[0] = polygons[0][::-1]
        indices = [] if name.endswith("_icon") else [i for triangle in svg.triangle_indices(polygons[0]) for i in triangle]
        triangles = [p for triangle in svg.icon_geometry(polygons)[0] for p in triangle] if name.endswith("_icon") else []
        upper = name.upper()
        lines.append("static const float LID_SHAPE_{}_POINTS[] = {{ {} }};".format(
            upper, ", ".join("{:.2f}f, {:.2f}f".format(x, y) for polygon in polygons for x, y in polygon)))
        lines.append("static const int LID_SHAPE_{}_COUNTS[] = {{ {} }};".format(upper, ", ".join(str(len(p)) for p in polygons)))
        triangle_array = "nullptr"
        if triangles:
            triangle_array = "LID_SHAPE_{}_TRIANGLES".format(upper)
            lines.append("static const float {}[] = {{ {} }};".format(triangle_array, ", ".join("{:.2f}f, {:.2f}f".format(x, y) for x, y in triangles)))
        index_array = "nullptr"
        if indices:
            index_array = "LID_SHAPE_{}_INDICES".format(upper)
            lines.append("static const int {}[] = {{ {} }};".format(index_array, ", ".join(str(i) for i in indices)))
        lines.append("static const LidShape LID_SHAPE_{} = {{ LID_SHAPE_{}_POINTS, LID_SHAPE_{}_COUNTS, {}, {}, {}, {}, {} }};".format(
            upper, upper, upper, len(polygons), triangle_array, len(triangles) // 3, index_array, len(indices)))
    open(sys.argv[2], "w").write("\n".join(lines) + "\n")
    print("{}: glass {:.1f}x{:.1f} mm, LCD margin {}x{}".format(sys.argv[2], gx1 - gx0, gy1 - gy0, MARGIN_X, margin_y))


if __name__ == "__main__":
    main()
