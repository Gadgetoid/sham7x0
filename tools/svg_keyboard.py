import math
import re

TOKEN = re.compile(r"[A-Za-z]|[-+]?(?:\d+\.?\d*|\.\d+)(?:[eE][-+]?\d+)?")
NUMBER = re.compile(r"[-+]?(?:\d+\.?\d*|\.\d+)(?:[eE][-+]?\d+)?")
ARGUMENTS = {"m": 2, "l": 2, "h": 1, "v": 1, "c": 6, "s": 4, "q": 4, "t": 2, "a": 7}
CURVE_STEPS = 10
LOCATOR_FILL = "#f9f9f9"


def cubic(p0, p1, p2, p3, steps=CURVE_STEPS):
    points = []
    for i in range(1, steps + 1):
        t = i / steps
        m = 1 - t
        points.append((m * m * m * p0[0] + 3 * m * m * t * p1[0] + 3 * m * t * t * p2[0] + t * t * t * p3[0],
                       m * m * m * p0[1] + 3 * m * m * t * p1[1] + 3 * m * t * t * p2[1] + t * t * t * p3[1]))
    return points


def arc(start, rx, ry, rotation, large, sweep, end):
    if rx == 0 or ry == 0 or start == end:
        return [end]
    phi = math.radians(rotation)
    cos_phi, sin_phi = math.cos(phi), math.sin(phi)
    dx, dy = (start[0] - end[0]) / 2, (start[1] - end[1]) / 2
    x1 = cos_phi * dx + sin_phi * dy
    y1 = -sin_phi * dx + cos_phi * dy
    rx, ry = abs(rx), abs(ry)
    scale = (x1 * x1) / (rx * rx) + (y1 * y1) / (ry * ry)
    if scale > 1:
        rx *= math.sqrt(scale)
        ry *= math.sqrt(scale)
    numerator = rx * rx * ry * ry - rx * rx * y1 * y1 - ry * ry * x1 * x1
    denominator = rx * rx * y1 * y1 + ry * ry * x1 * x1
    factor = math.sqrt(max(0.0, numerator / denominator)) if denominator else 0.0
    if large == sweep:
        factor = -factor
    cx1 = factor * rx * y1 / ry
    cy1 = -factor * ry * x1 / rx
    cx = cos_phi * cx1 - sin_phi * cy1 + (start[0] + end[0]) / 2
    cy = sin_phi * cx1 + cos_phi * cy1 + (start[1] + end[1]) / 2

    def angle(ux, uy, vx, vy):
        return math.atan2(ux * vy - uy * vx, ux * vx + uy * vy)

    theta = angle(1, 0, (x1 - cx1) / rx, (y1 - cy1) / ry)
    delta = angle((x1 - cx1) / rx, (y1 - cy1) / ry, (-x1 - cx1) / rx, (-y1 - cy1) / ry)
    if not sweep and delta > 0:
        delta -= 2 * math.pi
    elif sweep and delta < 0:
        delta += 2 * math.pi
    steps = max(2, int(abs(delta) / (math.pi / 16)))
    points = []
    for i in range(1, steps + 1):
        t = theta + delta * i / steps
        x, y = rx * math.cos(t), ry * math.sin(t)
        points.append((cos_phi * x - sin_phi * y + cx, sin_phi * x + cos_phi * y + cy))
    points[-1] = end
    return points


def subpaths(d):
    items = TOKEN.findall(d)
    paths = []
    current = []
    x = y = 0.0
    start = (0.0, 0.0)
    command = None
    i = 0
    while i < len(items):
        if items[i].isalpha():
            command = items[i]
            i += 1
            if command in "zZ":
                if current:
                    paths.append(current)
                current = []
                x, y = start
                continue
        op = command.lower()
        relative = command.islower()
        values = [float(v) for v in items[i:i + ARGUMENTS[op]]]
        i += ARGUMENTS[op]
        base = (x, y) if relative else (0.0, 0.0)
        if op == "m":
            if current:
                paths.append(current)
            x, y = base[0] + values[0], base[1] + values[1]
            start = (x, y)
            current = [(x, y)]
            command = "l" if relative else "L"
        elif op == "l":
            x, y = base[0] + values[0], base[1] + values[1]
            current.append((x, y))
        elif op == "h":
            x = (x if relative else 0.0) + values[0]
            current.append((x, y))
        elif op == "v":
            y = (y if relative else 0.0) + values[0]
            current.append((x, y))
        elif op == "c":
            p1 = (base[0] + values[0], base[1] + values[1])
            p2 = (base[0] + values[2], base[1] + values[3])
            p3 = (base[0] + values[4], base[1] + values[5])
            current.extend(cubic((x, y), p1, p2, p3))
            x, y = p3
        elif op == "a":
            end = (base[0] + values[5], base[1] + values[6])
            current.extend(arc((x, y), values[0], values[1], values[2], int(values[3]), int(values[4]), end))
            x, y = end
        else:
            raise ValueError("unsupported path command " + command)
    if current:
        paths.append(current)
    return [clean(path) for path in paths]


def clean(points):
    out = []
    for point in points:
        if not out or abs(point[0] - out[-1][0]) > 1e-5 or abs(point[1] - out[-1][1]) > 1e-5:
            out.append(point)
    if len(out) > 2 and abs(out[0][0] - out[-1][0]) < 1e-5 and abs(out[0][1] - out[-1][1]) < 1e-5:
        out.pop()
    return out


def matrix(transform):
    result = (1.0, 0.0, 0.0, 1.0, 0.0, 0.0)
    for name, args in re.findall(r"(\w+)\(([^)]*)\)", transform or ""):
        v = [float(n) for n in NUMBER.findall(args)]
        if name == "translate":
            m = (1.0, 0.0, 0.0, 1.0, v[0], v[1] if len(v) > 1 else 0.0)
        elif name == "matrix":
            m = tuple(v)
        elif name == "scale":
            m = (v[0], 0.0, 0.0, v[1] if len(v) > 1 else v[0], 0.0, 0.0)
        else:
            raise ValueError("unsupported transform " + name)
        result = multiply(result, m)
    return result


def multiply(a, b):
    return (a[0] * b[0] + a[2] * b[1], a[1] * b[0] + a[3] * b[1], a[0] * b[2] + a[2] * b[3], a[1] * b[2] + a[3] * b[3],
            a[0] * b[4] + a[2] * b[5] + a[4], a[1] * b[4] + a[3] * b[5] + a[5])


def apply(m, point):
    return m[0] * point[0] + m[2] * point[1] + m[4], m[1] * point[0] + m[3] * point[1] + m[5]


def area(points):
    return sum(points[i][0] * points[(i + 1) % len(points)][1] - points[(i + 1) % len(points)][0] * points[i][1]
               for i in range(len(points))) / 2


def contains(points, point):
    inside = False
    j = len(points) - 1
    for i in range(len(points)):
        xi, yi = points[i]
        xj, yj = points[j]
        if (yi > point[1]) != (yj > point[1]) and point[0] < (xj - xi) * (point[1] - yi) / (yj - yi) + xi:
            inside = not inside
        j = i
    return inside


def regions(paths):
    depth = [sum(1 for j, other in enumerate(paths) if j != i and contains(other, path[0])) for i, path in enumerate(paths)]
    result = []
    for i, path in enumerate(paths):
        if depth[i] % 2:
            continue
        holes = [paths[j] for j in range(len(paths)) if depth[j] == depth[i] + 1 and contains(path, paths[j][0])]
        result.append(bridge(path, holes))
    return result


def crosses(a, b, c, d):
    def side(p, q, r):
        return (q[0] - p[0]) * (r[1] - p[1]) - (q[1] - p[1]) * (r[0] - p[0])
    if a in (c, d) or b in (c, d):
        return False
    return side(a, b, c) * side(a, b, d) < 0 and side(c, d, a) * side(c, d, b) < 0


def visible(a, b, polygons):
    for polygon in polygons:
        for i in range(len(polygon)):
            if crosses(a, b, polygon[i], polygon[(i + 1) % len(polygon)]):
                return False
    return True


def bridge(outer, holes):
    outer = outer if area(outer) > 0 else outer[::-1]
    holes = [hole if area(hole) < 0 else hole[::-1] for hole in holes]
    remaining = sorted(holes, key=lambda h: -max(p[0] for p in h))
    while remaining:
        hole = remaining.pop(0)
        hi = max(range(len(hole)), key=lambda i: hole[i][0])
        start = hole[hi]
        order = sorted(range(len(outer)), key=lambda i: (outer[i][0] - start[0]) ** 2 + (outer[i][1] - start[1]) ** 2)
        oi = next(i for i in order if visible(start, outer[i], [outer, hole] + remaining))
        loop = hole[hi:] + hole[:hi + 1]
        outer = outer[:oi + 1] + loop + outer[oi:]
    return outer


def elements(path):
    text = open(path).read()
    stack = [((1.0, 0.0, 0.0, 1.0, 0.0, 0.0), "")]
    for match in re.finditer(r"<(/?)(g|path|rect|circle|ellipse)\b(.*?)(/?)>", text, re.S):
        closing, tag, body, self_closing = match.groups()
        if tag == "g":
            if closing:
                stack.pop()
            elif not self_closing:
                stack.append((multiply(stack[-1][0], matrix(attribute(body, "transform"))), attribute(body, "inkscape:label") or ""))
            continue
        if closing:
            continue
        yield tag, body, multiply(stack[-1][0], matrix(attribute(body, "transform"))), stack[-1][1]


def ellipse(d):
    values = [float(v) for v in NUMBER.findall(d)]
    return values[0] - values[2], values[1], values[2], values[3]


def attribute(body, name):
    match = re.search(r"\s" + name + r'="([^"]+)"', body)
    return match.group(1) if match else None


def load(path, group="keyboard"):
    keys, circles, icons, locators = [], [], [], []
    label = None
    for tag, body, transform, layer in elements(path):
        if layer != group or tag not in ("path", "rect"):
            continue
        style = attribute(body, "style") or ""
        fill_match = re.search(r"fill:(#[0-9a-fA-F]+)", style)
        fill = fill_match.group(1).lower() if fill_match else ""
        name = attribute(body, "inkscape:label") or ""
        if tag == "rect":
            x, y, w, h = (float(attribute(body, n)) for n in ("x", "y", "width", "height"))
            x0, y0 = apply(transform, (x, y))
            label = (x0, y0, w, h)
            continue
        d = attribute(body, "d")
        if re.search(r"(^|\s)a\s", d.strip()) and fill in ("#999999", "#cccccc"):
            cx, cy, rx, ry = ellipse(d)
            cx, cy = apply(transform, (cx, cy))
            circles.append((cx, cy, rx, ry))
            continue
        paths = [[apply(transform, p) for p in sub] for sub in subpaths(d)]
        if name.startswith("icon-"):
            icons.append((name, paths))
        elif fill == LOCATOR_FILL:
            locators.append(paths[0])
        else:
            keys.append(paths[0])
    return keys, circles, label, icons, locators


def centre(points):
    xs = [p[0] for p in points]
    ys = [p[1] for p in points]
    return (min(xs) + max(xs)) / 2, (min(ys) + max(ys)) / 2


def triangulate(polygon):
    points = polygon if area(polygon) > 0 else polygon[::-1]
    return [tuple(points[i] for i in triangle) for triangle in triangle_indices(points)]


def triangle_indices(points):
    indices = list(range(len(points)))
    triangles = []

    def cross(a, b, c):
        return (b[0] - a[0]) * (c[1] - a[1]) - (b[1] - a[1]) * (c[0] - a[0])

    def inside(p, a, b, c):
        return cross(a, b, p) >= -1e-9 and cross(b, c, p) >= -1e-9 and cross(c, a, p) >= -1e-9

    while len(indices) > 3:
        count = len(indices)
        ear = None
        for k in range(count):
            a, b, c = points[indices[k - 1]], points[indices[k]], points[indices[(k + 1) % count]]
            if cross(a, b, c) <= 1e-12:
                continue
            if any(inside(points[j], a, b, c) for j in indices if points[j] not in (a, b, c)):
                continue
            ear = k
            break
        if ear is None:
            ear = min(range(count), key=lambda k: abs(cross(points[indices[k - 1]], points[indices[k]], points[indices[(k + 1) % count]])))
        else:
            triangles.append((indices[ear - 1], indices[ear], indices[(ear + 1) % count]))
        del indices[ear]
    if len(indices) == 3 and cross(points[indices[0]], points[indices[1]], points[indices[2]]) > 1e-12:
        triangles.append(tuple(indices))
    return triangles


def icon_geometry(paths):
    triangles = [t for region in regions(paths) for t in triangulate(region)]
    return triangles, paths
