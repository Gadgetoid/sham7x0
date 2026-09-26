import re

NUMBER = re.compile(r"-?(?:\d+\.?\d*|\.\d+)(?:e-?\d+)?")
TOKEN = re.compile(r"[A-Za-z]|-?(?:\d+\.?\d*|\.\d+)(?:e-?\d+)?")
CURVE_STEPS = 10


def tokens(d):
    return TOKEN.findall(d)


def cubic(p0, p1, p2, p3, steps=CURVE_STEPS):
    points = []
    for i in range(1, steps + 1):
        t = i / steps
        m = 1 - t
        points.append((m * m * m * p0[0] + 3 * m * m * t * p1[0] + 3 * m * t * t * p2[0] + t * t * t * p3[0],
                       m * m * m * p0[1] + 3 * m * m * t * p1[1] + 3 * m * t * t * p2[1] + t * t * t * p3[1]))
    return points


def flatten(d):
    items = tokens(d)
    points = []
    x = y = 0.0
    command = None
    i = 0
    while i < len(items):
        if items[i].isalpha():
            command = items[i]
            i += 1
            if command in "zZ":
                continue
        relative = command.islower()
        op = command.lower()
        if op == "m":
            nx, ny = float(items[i]), float(items[i + 1])
            i += 2
            x, y = (x + nx, y + ny) if relative else (nx, ny)
            points.append((x, y))
            command = "l" if relative else "L"
        elif op == "l":
            nx, ny = float(items[i]), float(items[i + 1])
            i += 2
            x, y = (x + nx, y + ny) if relative else (nx, ny)
            points.append((x, y))
        elif op == "h":
            n = float(items[i])
            i += 1
            x = x + n if relative else n
            points.append((x, y))
        elif op == "v":
            n = float(items[i])
            i += 1
            y = y + n if relative else n
            points.append((x, y))
        elif op == "c":
            values = [float(v) for v in items[i:i + 6]]
            i += 6
            base = (x, y) if relative else (0.0, 0.0)
            p1 = (base[0] + values[0], base[1] + values[1])
            p2 = (base[0] + values[2], base[1] + values[3])
            p3 = (base[0] + values[4], base[1] + values[5])
            points.extend(cubic((x, y), p1, p2, p3))
            x, y = p3
        else:
            raise ValueError("unsupported path command " + command)
    cleaned = []
    for point in points:
        if not cleaned or abs(point[0] - cleaned[-1][0]) > 1e-4 or abs(point[1] - cleaned[-1][1]) > 1e-4:
            cleaned.append(point)
    if len(cleaned) > 2 and abs(cleaned[0][0] - cleaned[-1][0]) < 1e-4 and abs(cleaned[0][1] - cleaned[-1][1]) < 1e-4:
        cleaned.pop()
    return cleaned


def ellipse(d):
    values = [float(v) for v in NUMBER.findall(d)]
    start_x, start_y, rx, ry = values[0], values[1], values[2], values[3]
    return start_x - rx, start_y, rx, ry


def attribute(body, name):
    match = re.search(r'\s' + name + r'="([^"]+)"', body)
    return match.group(1) if match else None


def load(path):
    text = open(path).read()
    translate = re.search(r'translate\(([-\d.]+),([-\d.]+)\)', text)
    dx, dy = (float(translate.group(1)), float(translate.group(2))) if translate else (0.0, 0.0)
    keys = []
    circles = []
    label = None
    for match in re.finditer(r"<(path|rect)\b(.*?)/>", text, re.S):
        tag, body = match.groups()
        if tag == "rect":
            label = tuple(float(attribute(body, name)) for name in ("x", "y", "width", "height"))
            label = (label[0] + dx, label[1] + dy, label[2], label[3])
            continue
        d = attribute(body, "d")
        if " a " in " " + d + " " or re.search(r"\sa\s", d):
            cx, cy, rx, ry = ellipse(d)
            circles.append((cx + dx, cy + dy, rx, ry))
            continue
        keys.append([(px + dx, py + dy) for px, py in flatten(d)])
    return keys, circles, label


def centre(points):
    xs = [p[0] for p in points]
    ys = [p[1] for p in points]
    return (min(xs) + max(xs)) / 2, (min(ys) + max(ys)) / 2
