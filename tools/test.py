import argparse
import hashlib
import os
import subprocess
import sys
import zlib
import struct

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
HEADLESS = os.path.join(ROOT, "headless")
EXPECTED = os.path.join(ROOT, "tests", "expected.txt")
OUTPUT = os.path.join(ROOT, "build", "test")

POWER = "99.0"
ENTER = "6.6"
ESC = "0.0"
MAIN = "9.0"
MEMO = "9.3"
NEW = "2.6"
SHIFT = "0.5"
KEY_1 = "1.0"
KEY_H = "1.3"
KEY_I = "3.2"
BACKLIGHT = "9.6"

CASES = [
    {"name": "uninitialized", "seconds": 2, "keys": []},
    {"name": "init_prompt", "seconds": 2, "keys": [(0, POWER, 1.5)]},
    {"name": "welcome", "seconds": 10, "keys": [(0, POWER, 1.5), (2, ENTER, 0.3)]},
    {"name": "main_menu", "seconds": 15, "keys": [(0, POWER, 1.5), (2, ENTER, 0.3), (11, ESC, 0.3)], "save": "main"},
    {"name": "calendar", "load": "main", "seconds": 2, "keys": [(0.5, KEY_1, 0.2)]},
    {"name": "second_menu", "load": "main", "seconds": 2, "keys": [(0.5, MAIN, 0.2)]},
    {"name": "clock", "load": "main", "seconds": 3, "keys": [(0.5, MAIN, 0.2), (1.5, KEY_1, 0.2)]},
    {"name": "clock_ticks", "load": "main", "seconds": 64, "keys": [(0.5, MAIN, 0.2), (1.5, KEY_1, 0.2)]},
    {"name": "memo_typing", "load": "main", "seconds": 5,
     "keys": [(0.5, MEMO, 0.2), (1.5, NEW, 0.2), (2.5, SHIFT, 0.5), (2.6, KEY_H, 0.2), (3.5, KEY_I, 0.2)]},
    {"name": "power_off", "load": "main", "seconds": 2, "keys": [(0.5, POWER, 0.3)], "lcd": "lcd on 0"},
    {"name": "backlight", "load": "main", "seconds": 2, "keys": [(0.5, BACKLIGHT, 0.2)], "lcd": "backlight 1"},
]


def state_path(name):
    return os.path.join(OUTPUT, name + ".state")


def write_png(pbm_path, png_path, scale=3):
    data = open(pbm_path, "rb").read()
    _, _, rest = data.partition(b"\n")
    dimensions, _, pixels = rest.partition(b"\n")
    width, height = map(int, dimensions.split())
    row_bytes = (width + 7) // 8
    raw = b""
    for y in range(height):
        line = bytearray()
        for x in range(width):
            ink = pixels[y * row_bytes + x // 8] >> (7 - x % 8) & 1
            line += bytes([40 if ink else 200]) * scale
        raw += (b"\x00" + bytes(line)) * scale

    def chunk(tag, body):
        return struct.pack(">I", len(body)) + tag + body + struct.pack(">I", zlib.crc32(tag + body) & 0xFFFFFFFF)

    header = struct.pack(">IIBBBBB", width * scale, height * scale, 8, 0, 0, 0, 0)
    png = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", header) + chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b"")
    open(png_path, "wb").write(png)


def run_case(case, rom):
    pbm = os.path.join(OUTPUT, case["name"] + ".pbm")
    command = [HEADLESS, rom, f"--seconds={case['seconds']}", f"--pbm={pbm}"]
    if case["keys"]:
        command.append("--keys=" + ",".join(f"{at}:{key}/{hold}" for at, key, hold in case["keys"]))
    if "load" in case:
        command.append("--load=" + state_path(case["load"]))
    if "save" in case:
        command.append("--save=" + state_path(case["save"]))
    result = subprocess.run(command, capture_output=True, text=True)
    if result.returncode != 0:
        return None, result.stderr.strip()
    write_png(pbm, os.path.join(OUTPUT, case["name"] + ".png"))
    lcd_line = next((line for line in result.stderr.splitlines() if line.startswith("lcd ")), "")
    if "lcd" in case and case["lcd"] not in lcd_line:
        return None, f"expected '{case['lcd']}' in '{lcd_line}'"
    return hashlib.md5(open(pbm, "rb").read()).hexdigest(), ""


def load_expected():
    expected = {}
    if os.path.exists(EXPECTED):
        for line in open(EXPECTED):
            if line.strip():
                name, digest = line.split()
                expected[name] = digest
    return expected


def main():
    parser = argparse.ArgumentParser(description="Run the firmware regression cases against the headless harness.")
    parser.add_argument("--rom", default=os.path.join(ROOT, "rom", "r162.da1"))
    parser.add_argument("--update", action="store_true", help="record the current screens as expected")
    options = parser.parse_args()
    if not os.path.exists(options.rom):
        print(f"skipping: no ROM at {options.rom}")
        return 0
    os.makedirs(OUTPUT, exist_ok=True)
    expected = load_expected()
    recorded = {}
    failures = 0
    for case in CASES:
        digest, error = run_case(case, options.rom)
        name = case["name"]
        if digest is None:
            print(f"FAIL {name}: {error}")
            failures += 1
            continue
        recorded[name] = digest
        if options.update:
            print(f"record {name} {digest}")
        elif name not in expected:
            print(f"FAIL {name}: no expected screen, run with --update")
            failures += 1
        elif expected[name] != digest:
            print(f"FAIL {name}: screen changed, see build/test/{name}.png")
            failures += 1
        else:
            print(f"ok   {name}")
    if options.update:
        os.makedirs(os.path.dirname(EXPECTED), exist_ok=True)
        with open(EXPECTED, "w") as file:
            for name, digest in recorded.items():
                file.write(f"{name} {digest}\n")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
