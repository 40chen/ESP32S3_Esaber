#!/usr/bin/env python3
"""Turn assets/logo/eye.png into src/drivers/EyeTexture.h.

The eye is drawn as one RGB565 square that is blitted into the panel sprite
row by row.  Two properties of the source artwork make that cheap:

  * the eyeball is a circle on a transparent background, and
  * the screen behind it is black.

So instead of carrying an alpha channel and masking at runtime, the artwork is
composited over black here, at build time.  The transparent corners come out
black, which is exactly what the background already is, and the anti-aliased
rim survives as a smooth blend towards black.  No transparency handling, no
clip mask, no per-pixel work on the device.

Pure standard library on purpose: decoding a PNG is ~40 lines with zlib, and
this way the tool runs anywhere without Pillow.

Usage:
    python tools/make_eye_texture.py            # defaults, writes the header
    python tools/make_eye_texture.py --size 220 # different on-screen size
"""

import argparse
import struct
import zlib
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
DEFAULT_INPUT = REPO_ROOT / "assets" / "logo" / "eye.png"
DEFAULT_OUTPUT = REPO_ROOT / "src" / "drivers" / "EyeTexture.h"
DEFAULT_SIZE = 200


def read_png(path):
    """Decode a non-interlaced, 8-bit RGB/RGBA PNG into (w, h, rgba bytes)."""
    data = path.read_bytes()
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError(f"{path} is not a PNG")

    pos = 8
    idat = b""
    width = height = depth = color_type = interlace = None
    while pos < len(data):
        length, kind = struct.unpack(">I4s", data[pos : pos + 8])
        body = data[pos + 8 : pos + 8 + length]
        if kind == b"IHDR":
            width, height, depth, color_type, _, _, interlace = struct.unpack(">IIBBBBB", body)
        elif kind == b"IDAT":
            idat += body
        elif kind == b"IEND":
            break
        pos += 12 + length

    if depth != 8 or interlace != 0 or color_type not in (2, 6):
        raise ValueError("expected a non-interlaced 8-bit RGB or RGBA PNG")
    channels = 3 if color_type == 2 else 4

    raw = zlib.decompress(idat)
    stride = width * channels
    rows = []
    previous = bytearray(stride)
    offset = 0
    for _ in range(height):
        filter_type = raw[offset]
        offset += 1
        line = bytearray(raw[offset : offset + stride])
        offset += stride
        # Undo the per-row filter (PNG spec section 9).
        if filter_type == 1:
            for x in range(channels, stride):
                line[x] = (line[x] + line[x - channels]) & 0xFF
        elif filter_type == 2:
            for x in range(stride):
                line[x] = (line[x] + previous[x]) & 0xFF
        elif filter_type == 3:
            for x in range(stride):
                left = line[x - channels] if x >= channels else 0
                line[x] = (line[x] + ((left + previous[x]) >> 1)) & 0xFF
        elif filter_type == 4:
            for x in range(stride):
                left = line[x - channels] if x >= channels else 0
                up = previous[x]
                up_left = previous[x - channels] if x >= channels else 0
                estimate = left + up - up_left
                dl, du, dul = abs(estimate - left), abs(estimate - up), abs(estimate - up_left)
                if dl <= du and dl <= dul:
                    nearest = left
                elif du <= dul:
                    nearest = up
                else:
                    nearest = up_left
                line[x] = (line[x] + nearest) & 0xFF
        elif filter_type != 0:
            raise ValueError(f"unknown PNG filter {filter_type}")
        rows.append(line)
        previous = line

    # Normalise to RGBA so the resampler has one code path.
    rgba = bytearray(width * height * 4)
    for y, row in enumerate(rows):
        for x in range(width):
            source = x * channels
            target = (y * width + x) * 4
            rgba[target] = row[source]
            rgba[target + 1] = row[source + 1]
            rgba[target + 2] = row[source + 2]
            rgba[target + 3] = row[source + 3] if channels == 4 else 255
    return width, height, rgba


def resample_over_black(width, height, rgba, size):
    """Area-average down to size x size, compositing onto black.

    Averaging premultiplied samples is what makes the edge clean: a pixel that
    is 40% opaque contributes 40% of its colour and 60% of the black behind it,
    which is exactly the blend the panel would have shown anyway.
    """
    # Premultiply once; every later step is a plain weighted sum.
    premultiplied = [0.0] * (width * height * 3)
    for y in range(height):
        for x in range(width):
            pixel = (y * width + x) * 4
            alpha = rgba[pixel + 3] / 255.0
            for channel in range(3):
                premultiplied[(y * width + x) * 3 + channel] = rgba[pixel + channel] * alpha

    out = bytearray(size * size * 4)
    for dy in range(size):
        y0 = dy * height / size
        y1 = (dy + 1) * height / size
        for dx in range(size):
            x0 = dx * width / size
            x1 = (dx + 1) * width / size
            total = [0.0, 0.0, 0.0]
            area = 0.0
            for sy in range(int(y0), min(int(y1) + 1, height)):
                weight_y = min(sy + 1, y1) - max(sy, y0)
                if weight_y <= 0:
                    continue
                for sx in range(int(x0), min(int(x1) + 1, width)):
                    weight_x = min(sx + 1, x1) - max(sx, x0)
                    if weight_x <= 0:
                        continue
                    weight = weight_x * weight_y
                    base = (sy * width + sx) * 3
                    total[0] += premultiplied[base] * weight
                    total[1] += premultiplied[base + 1] * weight
                    total[2] += premultiplied[base + 2] * weight
                    area += weight
            target = (dy * size + dx) * 4
            for channel in range(3):
                value = total[channel] / area if area else 0.0
                out[target + channel] = max(0, min(255, int(value + 0.5)))
            out[target + 3] = 255
    return out


def to_rgb565(rgba):
    values = []
    for pixel in range(0, len(rgba), 4):
        red, green, blue = rgba[pixel], rgba[pixel + 1], rgba[pixel + 2]
        values.append(((red & 0xF8) << 8) | ((green & 0xFC) << 3) | (blue >> 3))
    return values


def write_header(path, size, values):
    lines = [
        "// Generated by tools/make_eye_texture.py from assets/logo/eye.png.",
        "// Do not edit by hand: re-run the script instead.",
        "//",
        "// RGB565 in the natural order -- the same value DisplayDriver's colour565()",
        "// builds -- and already composited over black, so the transparent corners",
        "// of the artwork arrive as black, exactly the colour behind the eye.",
        "//",
        "// The values are NOT byte swapped here even though TFT_eSPI stores sprite",
        "// pixels swapped: DisplayDriver turns on the sprite's own swap flag and",
        "// lets pushImage convert the rows as it copies them.  Keeping the array in",
        "// the natural order keeps it something a human can check against the PNG.",
        "",
        "#pragma once",
        "",
        "#include <Arduino.h>",
        "",
        f"constexpr int16_t kEyeTextureSize = {size};",
        "",
        f"const uint16_t kEyeTexture[kEyeTextureSize * kEyeTextureSize] = {{",
    ]
    for start in range(0, len(values), 12):
        chunk = ", ".join(f"0x{value:04X}" for value in values[start : start + 12])
        lines.append(f"    {chunk},")
    lines.append("};")
    lines.append("")
    path.write_text("\n".join(lines), encoding="ascii")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--input", type=Path, default=DEFAULT_INPUT)
    parser.add_argument("--output", type=Path, default=DEFAULT_OUTPUT)
    parser.add_argument("--size", type=int, default=DEFAULT_SIZE, help="on-screen square in pixels")
    args = parser.parse_args()

    width, height, rgba = read_png(args.input)
    print(f"{args.input}: {width}x{height} RGBA")
    scaled = resample_over_black(width, height, rgba, args.size)
    write_header(args.output, args.size, to_rgb565(scaled))
    print(f"{args.output}: {args.size}x{args.size} RGB565, {args.size * args.size * 2} bytes of flash")


if __name__ == "__main__":
    main()
