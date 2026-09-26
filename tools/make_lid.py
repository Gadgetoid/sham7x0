import math
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import svg_keyboard as svg

GLASS_UNITS = 282.0
MARGIN_X = 3
CASE_MARGIN_LEFT = 31.0
CASE_MARGIN_RIGHT = 24.0
CASE_MARGIN_TOP = 68.0
CASE_MARGIN_BOTTOM = 36.0


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
    text = open(path).read()
    layer = re.search(r"<g\b[^>]*>", text)
    base = svg.matrix(svg.attribute(layer.group(0), "transform"))
    found = []
    for match in re.finditer(r"<(path|rect|circle|ellipse)\b(.*?)/>", text, re.S):
        tag, body = match.groups()
        value = lambda name: svg.attribute(body, name)
        transform = svg.multiply(base, svg.matrix(value("transform")))
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
        found.append({"tag": tag, "fill": fill, "paths": paths, "points": [p for path in paths for p in path]})
    for shape in found:
        xs = [p[0] for p in shape["points"]]
        ys = [p[1] for p in shape["points"]]
        shape["box"] = (min(xs), min(ys), max(xs), max(ys))
        shape["centre"] = ((min(xs) + max(xs)) / 2, (min(ys) + max(ys)) / 2)
        shape["area"] = (max(xs) - min(xs)) * (max(ys) - min(ys))
    return found


def classify(found):
    glass = max((s for s in found if s["fill"] == "#008080" and s["tag"] == "rect"), key=lambda s: s["area"])
    frame = next(s for s in found if s["fill"] == "#f9f9f9" and s["tag"] == "rect")
    gx0, gy0, gx1, gy1 = glass["box"]
    left = [s for s in found if s["centre"][0] < gx0]
    right = [s for s in found if s["centre"][0] > gx1]
    named = {"glass": glass, "frame": frame}
    sides = sorted((s for s in left if s["fill"] == "#333333"), key=lambda s: s["centre"][1])
    flutes = sorted((s for s in left if s["fill"] == "#f9f9f9"), key=lambda s: s["centre"][1])
    for index, (key, flute) in enumerate(zip(sides, flutes)):
        named["side_{}".format(index)] = key
        named["flute_{}".format(index)] = flute
    named["light"] = next(s for s in left if s["fill"] == "#008080")
    named["light_icon"] = next(s for s in left if s["fill"] == "#b3b3b3")
    named["power"] = next(s for s in right if s["fill"] == "#008080")
    greys = sorted((s for s in right if s["fill"] == "#b3b3b3" and s["tag"] == "path"), key=lambda s: s["area"])
    named["menu_well"] = next(s for s in right if s["fill"] == "#b3b3b3" and s["tag"] == "ellipse")
    named["power_icon"], named["power_flute"] = greys[0], greys[-1]
    named["menu"] = next(s for s in right if s["fill"] == "#333333" and s["tag"] == "ellipse")
    wells = sorted((s for s in right if s["fill"] == "#333333" and s["tag"] == "path"), key=lambda s: s["centre"][1])
    named["arrow_well"], named["esc_well"] = wells[0], wells[-1]
    blues = sorted((s for s in right if s["fill"] == "#0000ff"), key=lambda s: s["centre"][1])
    named["up"], named["down"] = blues
    chevrons = sorted((s for s in right if s["fill"] == "#f9f9f9" and s["tag"] == "path"), key=lambda s: s["centre"][1])
    named["up_icon"], named["down_icon"] = chevrons
    circles = sorted((s for s in right if s["tag"] == "circle"), key=lambda s: s["area"])
    named["esc"], named["enter"] = circles
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
    everything = [unit(p) for name, shape in named.items() if name not in ("glass",) for p in shape["points"]]
    half_width = GLASS_UNITS * grid_w / (80 + 2 * margin_y) / 2
    min_x = min(p[0] for p in everything)
    max_x = max(p[0] for p in everything)
    min_y = min(p[1] for p in everything)
    max_y = max(p[1] for p in everything)
    lines = [
        "#pragma once",
        "",
        "struct LidShape {",
        "    const float *points;",
        "    const int *counts;",
        "    int regions;",
        "};",
        "",
        "static const int LID_LCD_MARGIN_X = {};".format(MARGIN_X),
        "static const int LID_LCD_MARGIN_Y = {};".format(margin_y),
        "static const float LID_LEFT_EXTENT = {:.2f}f;".format(-half_width - min_x + CASE_MARGIN_LEFT),
        "static const float LID_RIGHT_EXTENT = {:.2f}f;".format(max_x - half_width + CASE_MARGIN_RIGHT),
        "static const float LID_TOP_EXTENT = {:.2f}f;".format(-min_y + CASE_MARGIN_TOP),
        "static const float LID_BOTTOM_EXTENT = {:.2f}f;".format(max_y - GLASS_UNITS + CASE_MARGIN_BOTTOM),
    ]
    x0, y0, x1, y1 = named["frame"]["box"]
    (fx0, fy0), (fx1, fy1) = unit((x0, y0)), unit((x1, y1))
    lines.append("static const float LID_FRAME[] = {{ {:.2f}f, {:.2f}f, {:.2f}f, {:.2f}f }};".format(fx0, fy0, fx1, fy1))
    lines.append("static const float LID_FRAME_RADIUS = {:.2f}f;".format(3.7546248 * scale))
    lines.append("")
    order = ["side_{}".format(i) for i in range(5)] + ["flute_{}".format(i) for i in range(5)] + [
        "light", "light_icon", "power", "power_icon", "power_flute", "menu", "menu_well", "arrow_well", "up", "down", "up_icon",
        "down_icon", "esc_well", "esc", "enter"]
    for name in order:
        shape = named[name]
        polygons = svg.regions(shape["paths"]) if name.endswith("_icon") else [shape["paths"][0]]
        polygons = [[unit(p) for p in polygon] for polygon in polygons]
        upper = name.upper()
        lines.append("static const float LID_SHAPE_{}_POINTS[] = {{ {} }};".format(
            upper, ", ".join("{:.2f}f, {:.2f}f".format(x, y) for polygon in polygons for x, y in polygon)))
        lines.append("static const int LID_SHAPE_{}_COUNTS[] = {{ {} }};".format(upper, ", ".join(str(len(p)) for p in polygons)))
        lines.append("static const LidShape LID_SHAPE_{} = {{ LID_SHAPE_{}_POINTS, LID_SHAPE_{}_COUNTS, {} }};".format(upper, upper, upper, len(polygons)))
    open(sys.argv[2], "w").write("\n".join(lines) + "\n")
    print("{}: glass {:.1f}x{:.1f} mm, LCD margin {}x{}".format(sys.argv[2], gx1 - gx0, gy1 - gy0, MARGIN_X, margin_y))


if __name__ == "__main__":
    main()
