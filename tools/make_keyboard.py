import json
import sys

DPAD_KEY = 82.0
DPAD_UP_DY = -99.30
DPAD_SIDE_DX = 105.17
DPAD_SIDE_DY = -5.83
DPAD_WELL_DY = -25.85
DPAD_WELL_R = 82.0
DPAD_FLOOR_R = 62.0

KEY_BACKSPACE = 8
KEY_ENTER = 13
KEY_ESC = 27
KEY_TAB = 9
KEY_UP = 0x100
KEY_DOWN = 0x101
KEY_LEFT = 0x102
KEY_RIGHT = 0x103
KEY_NEW = 0x120
KEY_SMBL = 0x121
KEY_SEARCH = 0x122
KEY_CASE = 0x123
KEY_CUT = 0x124
KEY_COPY = 0x125
KEY_PASTE = 0x126
KEY_EDIT = 0x127
KEY_SYNC = 0x128
KEY_CAPS = 0x129
KEY_PICK = 0x12a

FIXED_CODES = {
    "ESC": KEY_ESC, "DEL": KEY_BACKSPACE, "ENTER": KEY_ENTER, "SPACE": ord(" "), "MINUS": ord("-"),
    "COMMA": ord(","), "PERIOD": ord("."), "MENU": KEY_TAB, "NEW": KEY_NEW, "SMBL": KEY_PICK,
    "UP": KEY_UP, "DOWN": KEY_DOWN, "LEFT": KEY_LEFT, "RIGHT": KEY_RIGHT,
}

WORD_FUNCTIONS = {
    "SEARCH": KEY_SEARCH, "CAPS": KEY_CAPS, "CUT": KEY_CUT, "COPY": KEY_COPY, "PASTE": KEY_PASTE,
    "EDIT": KEY_EDIT, "SMBL": KEY_SMBL, "PC SYNC": KEY_SYNC, "A⇌A": KEY_CASE, "✓": KEY_ENTER,
}

LETTER_FREQUENCY = {
    "e": 12.0, "t": 9.1, "a": 8.2, "o": 7.5, "i": 7.0, "n": 6.7, "s": 6.3, "h": 6.1, "r": 6.0, "d": 4.3,
    "l": 4.0, "c": 2.8, "u": 2.8, "m": 2.4, "w": 2.4, "f": 2.2, "g": 2.0, "y": 2.0, "p": 1.9, "b": 1.5,
    "v": 1.0, "k": 0.8, "j": 0.2, "x": 0.2, "q": 0.1, "z": 0.1,
}

KEY_WEAR = {"fn_2nd": 0.9, "space": 1.0, "enter": 0.8, "enter_wide": 0.8, "del": 0.85, "up": 0.7, "down": 0.75, "left": 0.6,
            "right": 0.6, "esc": 0.5, "menu": 0.85}

SHAPES = {"pill": "KB_SHAPE_PILL", "d_key": "KB_SHAPE_CURSOR", "square_end": "KB_SHAPE_SQUARE_END"}
SIDES = {"right": 0, "down": 1, "left": 2, "up": 3}
COLOURS = {"light": "KB_LIGHT", "dark": "KB_DARK", "2nd-outlined": "KB_DARK", "blue": "KB_BLUE"}
SECONDARY_COLOURS = {"grey": "KB_GREY", "purple": "KB_PURPLE", "purple-badge": "KB_BADGE"}
ICONS = {"": "KB_ICON_NONE", "backspace_arrow": "KB_ICON_BACKSPACE", "return": "KB_ICON_RETURN",
         "shift": "KB_ICON_SHIFT", "box_down": "KB_ICON_BOX_DOWN", "triangle_up": "KB_ICON_TRIANGLE",
         "triangle_down": "KB_ICON_TRIANGLE", "triangle_left": "KB_ICON_TRIANGLE", "triangle_right": "KB_ICON_TRIANGLE",
         "case_toggle": "KB_ICON_CASE_TOGGLE", "check": "KB_ICON_CHECK"}


def c_string(text):
    if text is None:
        return "nullptr"
    return '"' + text.replace("\\", "\\\\").replace('"', '\\"') + '"'


def colour(values):
    return "IM_COL32({}, {}, {}, 255)".format(*values[:3])


def respace_cursor(keys, layout):
    cursor = {key["id"]: key for key in keys if key["kind"] == "cursor"}
    down = cursor["down"]
    scale = down["w"] / DPAD_KEY
    cursor["left"]["x"] = down["x"] - DPAD_SIDE_DX * scale
    cursor["right"]["x"] = down["x"] + DPAD_SIDE_DX * scale
    cursor["left"]["y"] = cursor["right"]["y"] = down["y"] + DPAD_SIDE_DY * scale
    cursor["up"]["x"] = down["x"]
    cursor["up"]["y"] = down["y"] + DPAD_UP_DY * scale
    well = layout["recesses"][0]
    well["x"] = down["x"]
    well["y"] = down["y"] + DPAD_WELL_DY * scale
    well["r"] = DPAD_WELL_R * scale
    well["slope"] = (DPAD_WELL_R - DPAD_FLOOR_R) * scale


def key_codes(key):
    code = key["code"]
    legend = key["legend"].get("text") or ""
    secondary = key["secondary"]
    shift_code = 0
    second_code = 0
    is_letter = len(code) == 1 and code.isalpha()
    if is_letter:
        normal = ord(code.lower())
        shift_code = ord(code)
    elif code.startswith("DIGIT_"):
        normal = ord(code[-1])
    else:
        normal = FIXED_CODES.get(code, 0)
    for item in secondary:
        text = item["text"]
        if item["colour"] == "grey":
            shift_code = ord(text) if len(text) == 1 else 0
        elif text in WORD_FUNCTIONS:
            second_code = WORD_FUNCTIONS[text]
        elif len(text) == 1:
            second_code = ord(text)
    if code == "MINUS" and not shift_code:
        shift_code = ord("_")
    action = "KB_ACTION_SHIFT" if code == "SHIFT" else "KB_ACTION_SECOND" if code == "FN_2ND" else "KB_ACTION_KEY"
    return normal, shift_code, second_code, action, is_letter, legend


def key_wear(key):
    if key["id"] in LETTER_FREQUENCY:
        return 0.2 + 0.8 * LETTER_FREQUENCY[key["id"]] / 12.0
    if key["id"] in KEY_WEAR:
        return KEY_WEAR[key["id"]]
    if key["kind"] == "digit":
        return 0.25
    return 0.35


def main():
    layout = json.load(open(sys.argv[1]))
    keys = layout["keys"]
    respace_cursor(keys, layout)
    keyboard = layout["keyboard"]
    colours = layout["colours"]
    well = layout["recesses"][0]
    lines = [
        "#pragma once",
        "",
        "#include <cstdint>",
        "",
        "enum { KB_SHAPE_PILL, KB_SHAPE_CURSOR, KB_SHAPE_SQUARE_END };",
        "enum { KB_LIGHT, KB_DARK, KB_BLUE };",
        "enum { KB_GREY, KB_PURPLE, KB_BADGE };",
        "enum { KB_ICON_NONE, KB_ICON_BACKSPACE, KB_ICON_RETURN, KB_ICON_SHIFT, KB_ICON_BOX_DOWN, KB_ICON_TRIANGLE,",
        "       KB_ICON_CASE_TOGGLE, KB_ICON_CHECK };",
        "enum { KB_ACTION_KEY, KB_ACTION_SHIFT, KB_ACTION_SECOND };",
        "",
        "struct KeyboardSecondary {",
        "    const char *text;",
        "    int colour;",
        "    float size;",
        "    float dx;",
        "    int icon;",
        "};",
        "",
        "struct KeyboardKey {",
        "    const char *id;",
        "    float x, y, w, h;",
        "    int shape;",
        "    int round_side;",
        "    int colour;",
        "    const char *legend;",
        "    int icon;",
        "    float legend_size;",
        "    float legend_dy;",
        "    float stretch;",
        "    uint32_t code;",
        "    uint32_t shift_code;",
        "    uint32_t second_code;",
        "    int action;",
        "    bool letter;",
        "    bool ring;",
        "    bool homing;",
        "    float wear;",
        "    int secondary_count;",
        "    KeyboardSecondary secondary[2];",
        "};",
        "",
        "static const float KB_WIDTH = {:.2f}f;".format(keyboard["width"]),
        "static const float KB_HEIGHT = {:.2f}f;".format(keyboard["height"]),
        "static const float KB_FRONT_DEPTH = {:.2f}f;".format(keyboard["front_depth"]),
        "static const float KB_FRONT_SEAM = {:.2f}f;".format(keyboard["front_seam"]),
        "static const float KB_TOP_RADIUS = {:.2f}f;".format(keyboard["top_corner_radius"]),
        "static const float KB_BOTTOM_RADIUS = {:.2f}f;".format(keyboard["bottom_corner_radius"]),
        "static const float KB_LIP = {:.2f}f;".format(keyboard["lip_width"]),
        "static const float KB_FINGER_X = {:.2f}f;".format(keyboard["finger_recess"]["x"]),
        "static const float KB_FINGER_Y = {:.2f}f;".format(keyboard["height"] + keyboard["finger_recess"]["y_offset"]),
        "static const float KB_FINGER_W = {:.2f}f;".format(keyboard["finger_recess"]["w"]),
        "static const float KB_FINGER_H = {:.2f}f;".format(keyboard["finger_recess"]["h"]),
        "static const float KB_WELL_X = {:.2f}f;".format(well["x"]),
        "static const float KB_WELL_Y = {:.2f}f;".format(well["y"]),
        "static const float KB_WELL_R = {:.2f}f;".format(well["r"]),
        "static const float KB_WELL_SLOPE = {:.2f}f;".format(well["slope"]),
        "static const float KB_LABEL_CLEARANCE = {:.2f}f;".format(layout["label_clearance"]),
        "static const float KB_LEGEND_ALPHA = {:.2f}f;".format(layout["fonts"]["alpha"]),
        "static const float KB_LEGEND_STRETCH = {:.3f}f;".format(layout["fonts"]["legend"]["stretch"]),
        "static const ImU32 KB_KEYBED = {};".format(colour(colours["keybed"])),
        "static const ImU32 KB_FRONT_TOP = {};".format(colour(colours["front"]["top"])),
        "static const ImU32 KB_FRONT_BOTTOM = {};".format(colour(colours["front"]["bottom"])),
        "static const ImU32 KB_SEAM_DARK = {};".format(colour(colours["front"]["seam_dark"])),
        "static const ImU32 KB_SEAM_LIGHT = {};".format(colour(colours["front"]["seam_light"])),
        "static const ImU32 KB_KEY_TOP[3] = {{ {}, {}, {} }};".format(colour(colours["light"]["top"]), colour(colours["dark"]["top"]), colour(colours["blue"]["top"])),
        "static const ImU32 KB_KEY_BOTTOM[3] = {{ {}, {}, {} }};".format(colour(colours["light"]["bottom"]), colour(colours["dark"]["bottom"]), colour(colours["blue"]["bottom"])),
        "static const ImU32 KB_KEY_LEGEND[3] = {{ {}, {}, {} }};".format(colour(colours["light"]["legend"]), colour(colours["dark"]["legend"]), colour(colours["blue"]["legend"])),
        "static const ImU32 KB_SECONDARY[3] = {{ {}, {}, {} }};".format(colour(colours["grey"]), colour(colours["purple"]), colour(colours["purple-badge"]["fill"])),
        "static const ImU32 KB_BADGE_TEXT = {};".format(colour(colours["purple-badge"]["text"])),
        "static const uint32_t KB_KEY_CAPS = 0x{:x};".format(KEY_CAPS),
        "",
        "static const KeyboardKey keyboard_keys[] = {",
    ]
    for key in keys:
        normal, shift_code, second_code, action, is_letter, legend = key_codes(key)
        legend_spec = key["legend"]
        secondaries = []
        for item in key["secondary"][:2]:
            secondaries.append("{{ {}, {}, {:.2f}f, {:.2f}f, {} }}".format(
                c_string(item["text"]), SECONDARY_COLOURS[item["colour"]], item["size"], item.get("dx", 0.0),
                ICONS[item.get("icon", "")]))
        while len(secondaries) < 2:
            secondaries.append("{ nullptr, 0, 0.0f, 0.0f, 0 }")
        lines.append("    {{ {}, {:.2f}f, {:.2f}f, {:.2f}f, {:.2f}f, {}, {}, {}, {}, {}, {:.2f}f, {:.2f}f, {:.2f}f, 0x{:x}, 0x{:x}, 0x{:x}, {}, {}, {}, {}, {:.2f}f, {}, {{ {}, {} }} }},".format(
            c_string(key["id"]), key["x"], key["y"], key["w"], key["h"], SHAPES[key["shape"]],
            SIDES.get(key.get("round_side", key.get("square_side", "right")), 0), COLOURS[key["colour"]], c_string(legend_spec.get("text")),
            ICONS[legend_spec.get("icon", "")], legend_spec["size"], legend_spec["dy"], legend_spec.get("stretch", 1.0),
            normal, shift_code, second_code, action, "true" if is_letter else "false",
            "true" if "outline_ring" in key else "false", "true" if "homing_bar" in key else "false",
            key_wear(key), len(key["secondary"][:2]), secondaries[0], secondaries[1]))
    lines.append("};")
    open(sys.argv[2], "w").write("\n".join(lines) + "\n")
    print("{}: {} keys".format(sys.argv[2], len(keys)))


if __name__ == "__main__":
    main()
