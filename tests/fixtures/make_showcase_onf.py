#!/usr/bin/env python3
"""Writes tests/fixtures/valid/showcase.onf, an ONF 0.5.0 file that uses most features, independently of the C++ code.

One page with: all four pen tools, a fountain pen stroke without taper, the four shape types with rotation, text with spans on several
layers, a PNG placement, a cropped PNG placement, a crop that lies outside the image (drawn as a placeholder), and a PDF placement. It is meant for looking at the output of onf-reader svg, and as a
file for the command line tests (tests/cli/test_reader_cli.py).
"""
import hashlib
import json
import math
import struct
import zipfile
import zlib
from pathlib import Path

OUT = Path(__file__).resolve().parent / "valid" / "showcase.onf"

NO_PRESSURE, NO_TIMESTAMP, HAS_CREATED_AT = 1 << 2, 1 << 3, 1 << 4
NO_TAPER_START, NO_TAPER_END = 1 << 5, 1 << 6


def leb128(value):
    out = bytearray()
    while value > 0x7F:
        out.append((value & 0x7F) | 0x80)
        value >>= 7
    out.append(value)
    return bytes(out)


def zigzag(n):
    return (n << 1) if n >= 0 else ((-n << 1) - 1)


def chunk(chunk_type, body=b""):
    return struct.pack("<HI", chunk_type, len(body)) + body


def stroke(stroke_id, color, tool, width, layer, points, flags=0):
    """points: (x, y, pressure); no timestamps are written."""
    flags |= NO_TIMESTAMP
    out = struct.pack("<I4sHHHHI", stroke_id, bytes(color), tool, width, flags, layer, len(points))
    previous = (0, 0, 0)
    for x, y, pressure in points:
        for value, before in zip((x, y, pressure), previous):
            out += leb128(zigzag(value - before))
        previous = (x, y, pressure)
    return chunk(1, out)


def shape(shape_id, color, tool, width, layer, shape_type, parameters):
    head = struct.pack("<I4sHHHH", shape_id, bytes(color), tool, width, 0, layer)
    return chunk(3, head + struct.pack("<H", shape_type) + parameters)


def png(width, height):
    raw = bytearray()
    for y in range(height):
        raw.append(0)
        for x in range(width):
            raw += bytes((x * 255 // (width - 1), y * 255 // (height - 1), 160))

    def block(kind, data):
        body = kind + data
        return struct.pack(">I", len(data)) + body + struct.pack(">I", zlib.crc32(body))

    return (b"\x89PNG\r\n\x1a\n" + block(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0))
            + block(b"IDAT", zlib.compress(bytes(raw))) + block(b"IEND", b""))


def wave(x0, y0, length, amplitude, count, pressure):
    points = []
    for i in range(count):
        t = i / (count - 1)
        points.append((round(x0 + t * length), round(y0 + amplitude * math.sin(t * 2 * math.pi)), pressure(t)))
    return points


image = png(32, 32)
image_hash = hashlib.sha256(image).hexdigest()
pdf = b"%PDF-1.7\n% a stand-in, onf-reader draws a placeholder\n"
pdf_hash = hashlib.sha256(pdf).hexdigest()

ink = []
ramp = lambda t: round(40 + 215 * t)
bump = lambda t: round(255 * (1 - abs(2 * t - 1)))
ink.append(stroke(1, (20, 20, 120, 255), 0, 140, 3, wave(2000, 4000, 8000, 700, 80, ramp)))        # ballpoint, pressure ramp
ink.append(stroke(2, (120, 20, 20, 255), 1, 260, 3, wave(2000, 6000, 8000, 700, 120, bump)))       # fountain pen, taper
ink.append(stroke(3, (20, 100, 20, 255), 2, 200, 3, wave(2000, 8000, 8000, 700, 80, ramp)))        # pencil, opacity from pressure
ink.append(stroke(4, (255, 230, 0, 0x66), 3, 900, 3, [(2000, 4800, 255), (10000, 5200, 255)]))  # highlighter
ink.append(stroke(5, (0, 0, 0, 255), 0, 100, 3, [(6000, 5000, 255)], NO_PRESSURE))                 # one sample: a dot
ink.append(stroke(6, (0, 0, 0, 255), 1, 500, 3, [(5200, 3300, 255), (5200, 9300, 255)], NO_PRESSURE | NO_TAPER_START | NO_TAPER_END))  # cut ends
ink.append(shape(7, (200, 0, 0, 255), 0, 120, 3, 1, struct.pack("<iiiiH", 14000, 5000, 2500, 1200, 3000)))   # ellipse
ink.append(shape(8, (0, 0, 200, 255), 0, 120, 3, 2, struct.pack("<iiiiH", 14000, 8000, 3000, 1500, 1500)))   # rectangle
ink.append(shape(9, (0, 0, 0, 255), 1, 150, 3, 3, struct.pack("<iiii", 12000, 10000, 16000, 10600)))        # line
ink.append(shape(10, (0, 120, 120, 255), 2, 150, 3, 4, struct.pack("<iiiiii", 2000, 11500, 4500, 11500, 3250, 13500)))  # triangle
ink.append(shape(11, (255, 120, 0, 0x66), 3, 700, 4, 2, struct.pack("<iiiiH", 6000, 15000, 7000, 1200, 0)))  # highlighter rectangle on layer 4

strokes_bin = b"ONFS" + struct.pack("<H", 2) + b"\x00\x00" + b"".join(ink) + chunk(2)

text_title = {
    "id": "t00001", "layer": 5, "x": 2000, "y": 1000, "width": 12000, "height": 2200,
    "content": "Showcase: bold, red and struck", "font_family": "Helvetica", "generic_family": "sans-serif",
    "font_size": 706, "color": "#000000FF", "bold": False, "italic": False,
    "spans": [{"start": 10, "end": 14, "bold": True}, {"start": 16, "end": 19, "color": "#CC0000FF"},
              {"start": 24, "end": 30, "strikethrough": True, "underline": False}],
}
text_note = {
    "id": "t00002", "layer": 2, "x": 2000, "y": 14500, "width": 9000, "height": 2000,
    "content": "Text below the ink\nsecond line, larger", "font_family": "Georgia", "generic_family": "serif",
    "font_size": 423, "color": "#224488FF", "bold": False, "italic": True,
    "spans": [{"start": 19, "end": 38, "font_size": 560}],
}
meta = {
    "width": 21000, "height": 29700,
    "background": {"type": "grid", "color": "#FFFFF8FF", "foreground_color": "#2244AAFF", "spacing": 500, "top_margin": 1000, "line_width": 4},
    "text_objects": [text_title, text_note],
    "assets": [
        {"id": "a00001", "layer": 1, "hash": image_hash, "extension": "png", "x": 14000, "y": 13000, "width": 5000, "height": 3000},
        {"id": "a00002", "layer": 1, "hash": pdf_hash, "extension": "pdf", "pdf_page": 2, "x": 14000, "y": 17000, "width": 5000, "height": 7000},
        {"id": "a00003", "layer": 1, "hash": "0" * 64, "extension": "png", "x": 2000, "y": 18000, "width": 5000, "height": 3000},
        # the middle of the 32 x 32 image, scaled to fill the placement (spec 9.4.5)
        {"id": "a00004", "layer": 1, "hash": image_hash, "extension": "png", "crop": {"x": 8, "y": 8, "width": 16, "height": 16},
         "x": 8000, "y": 18000, "width": 3000, "height": 3000},
        # a region that is not inside the image: a corrupt asset, the page stays readable
        {"id": "a00005", "layer": 1, "hash": image_hash, "extension": "png", "crop": {"x": 20, "y": 20, "width": 20, "height": 20},
         "x": 8000, "y": 22000, "width": 3000, "height": 3000},
    ],
}
manifest = {
    "onf_version": "0.5.0", "generator": "make_showcase_onf.py", "title": "Showcase",
    "created_at": "2026-06-14T10:32:00.000Z", "modified_at": "2026-06-14T12:15:00.000Z",
    "pages": [{"id": "p00001", "order": 1}],
}


def add(archive, name, data, deflate):
    info = zipfile.ZipInfo(name, date_time=(2026, 6, 14, 10, 32, 0))
    info.compress_type = zipfile.ZIP_DEFLATED if deflate else zipfile.ZIP_STORED
    info.external_attr = 0o100644 << 16
    archive.writestr(info, data)


OUT.parent.mkdir(exist_ok=True)
with zipfile.ZipFile(OUT, "w") as archive:
    add(archive, "manifest.json", json.dumps(manifest, indent=4).encode(), True)
    add(archive, "pages/p00001/meta.json", json.dumps(meta, indent=4).encode(), True)
    add(archive, "pages/p00001/strokes.bin", strokes_bin, False)
    folder = zipfile.ZipInfo("assets/", date_time=(2026, 6, 14, 10, 32, 0))
    folder.external_attr = (0o40755 << 16) | 0x10
    archive.writestr(folder, b"")
    add(archive, f"assets/{image_hash}.png", image, False)
    add(archive, f"assets/{pdf_hash}.pdf", pdf, False)
print("wrote", OUT)
