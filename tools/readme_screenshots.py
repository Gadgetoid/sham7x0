import argparse
import os
import shutil
import struct
import subprocess
import sys
import tempfile
import zlib

import test

ROOT = test.ROOT
OUTPUT = os.path.join(ROOT, "docs", "screenshots")
EMULATOR = os.path.join(ROOT, "sham7x0")

KEY_3 = "3.0"

SHOTS = [
    {"name": "not_initialized", "seconds": 2, "keys": []},
    {"name": "initialize", "seconds": 2, "keys": [(0, test.POWER, 1.5)]},
    {"name": "setup", "seconds": 10, "keys": [(0, test.POWER, 1.5), (2, test.ENTER, 0.3)]},
    {"name": "main_menu", "load": "main", "seconds": 2, "keys": []},
    {"name": "main_menu_lit", "load": "main", "seconds": 2, "keys": [], "lit": True},
    {"name": "calendar", "load": "main", "seconds": 2, "keys": [(0.5, test.KEY_1, 0.2)], "lit": True},
    {"name": "clock", "load": "main", "seconds": 3, "keys": [(0.5, test.MAIN, 0.2), (1.5, test.KEY_1, 0.2)]},
    {"name": "memo", "load": "main", "seconds": 5, "lit": True,
     "keys": [(0.5, test.MEMO, 0.2), (1.5, test.NEW, 0.2), (2.5, test.SHIFT, 0.5), (2.6, test.KEY_H, 0.2), (3.5, test.KEY_I, 0.2)]},
    {"name": "programs", "load": "programs", "seconds": 2, "keys": [(0.5, test.PROG, 0.2)]},
    {"name": "basic_program", "load": "programs", "seconds": 4, "keys": [(0.5, test.KEY_2, 0.2)]},
    {"name": "machine_code_program", "load": "programs", "seconds": 4, "keys": [(0.5, KEY_3, 0.2)], "lit": True},
    {"name": "memo_received", "load": "main", "install": ["memo/Chili Joke [Chili food joke]"], "seconds": 10,
     "keys": [(8, test.MEMO, 0.2)]},
    {"name": "test_mode", "seconds": 3, "keys": [(0, test.ESC, 1.2), (0, test.KEY_D, 1.2)]},
]


def png_chunk(tag, body):
    return struct.pack(">I", len(body)) + tag + body + struct.pack(">I", zlib.crc32(tag + body) & 0xFFFFFFFF)


def write_png(path, width, height, rows, colour_type):
    header = struct.pack(">IIBBBBB", width, height, 8, colour_type, 0, 0, 0)
    raw = b"".join(b"\x00" + row for row in rows)
    with open(path, "wb") as output:
        output.write(b"\x89PNG\r\n\x1a\n" + png_chunk(b"IHDR", header) + png_chunk(b"IDAT", zlib.compress(raw, 9)) + png_chunk(b"IEND", b""))


def ppm_to_png(ppm_path, png_path):
    data = open(ppm_path, "rb").read()
    magic, width, height, maximum, pixels = data.split(maxsplit=4)
    width, height = int(width), int(height)
    rows = [pixels[y * width * 3:(y + 1) * width * 3] for y in range(height)]
    write_png(png_path, width, height, rows, 2)


def bmp_to_png(bmp_path, png_path):
    data = open(bmp_path, "rb").read()
    offset = struct.unpack_from("<I", data, 10)[0]
    width, height = struct.unpack_from("<ii", data, 18)
    masks = struct.unpack_from("<IIII", data, 54)
    shifts = [(mask & -mask).bit_length() - 1 for mask in masks]
    top_down = height < 0
    height = abs(height)
    rows = []
    for y in range(height):
        source_row = y if top_down else height - 1 - y
        row = bytearray()
        for pixel in struct.iter_unpack("<I", data[offset + source_row * width * 4:offset + (source_row + 1) * width * 4]):
            red, green, blue, alpha = [(pixel[0] >> shift) & 0xFF for shift in shifts]
            if alpha:
                red, green, blue = [min(255, round(channel * 255 / alpha)) for channel in (red, green, blue)]
            row += bytes((red, green, blue, alpha))
        rows.append(bytes(row))
    write_png(png_path, width, height, rows, 6)


def make_states(rom):
    os.makedirs(test.OUTPUT, exist_ok=True)
    for case in test.CASES:
        if "save" not in case:
            continue
        digest, error = test.run_case(case, rom)
        if digest in (None, "skip"):
            sys.exit(f"cannot make the {case['save']} state: {error}")


def lcd_shot(shot, rom, cell):
    keys = list(shot["keys"])
    if shot.get("lit"):
        keys.append((shot["seconds"] - 1, test.BACKLIGHT, 0.2))
    ppm = os.path.join(test.OUTPUT, shot["name"] + ".ppm")
    command = [test.HEADLESS, rom, f"--seconds={shot['seconds']}", f"--lcd={ppm}", f"--lcd-cell={cell}"]
    if keys:
        command.append("--keys=" + ",".join(f"{at + test.KEY_OFFSET if at else 0}:{key}/{hold}" for at, key, hold in keys))
    for name in shot.get("install", []):
        command.append("--install=" + os.path.join(ROOT, "apps", name + ".wzd"))
    if "load" in shot:
        command.append("--load=" + test.state_path(shot["load"]))
    subprocess.run(command, capture_output=True, check=True)
    ppm_to_png(ppm, os.path.join(OUTPUT, shot["name"] + ".png"))


def device_shot(rom, name, layout, width):
    data = tempfile.mkdtemp()
    try:
        shutil.copy(test.state_path("main"), os.path.join(data, "state-os1.62.bin"))
        bmp = os.path.join(data, "device.bmp")
        environment = dict(os.environ, POCKET_PERSIST="1")
        subprocess.run([EMULATOR, f"--data={data}", f"--rom={rom}", "--borderless", f"--layout={layout}", f"--size={width}x{width}",
                        "--menu=backlight", f"--screenshot={bmp}", "--frames=150"], capture_output=True, check=True, env=environment)
        bmp_to_png(bmp, os.path.join(OUTPUT, name + ".png"))
    finally:
        shutil.rmtree(data)


def main():
    parser = argparse.ArgumentParser(description="Render the README screenshots into docs/screenshots.")
    parser.add_argument("--rom", default=os.path.join(ROOT, "rom", "r162.da1"))
    parser.add_argument("--cell", type=int, default=3, help="LCD pixel size in the individual shots")
    options = parser.parse_args()
    os.makedirs(OUTPUT, exist_ok=True)
    make_states(options.rom)
    for shot in SHOTS:
        lcd_shot(shot, options.rom, options.cell)
        print(shot["name"])
    device_shot(options.rom, "device", 3, 640)
    print("device")


if __name__ == "__main__":
    main()
