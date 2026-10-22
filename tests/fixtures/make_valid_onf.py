#!/usr/bin/env python3
"""Writes tests/fixtures/valid/minimal.onf, an ONF 0.2.0 file, independently of the C++ code.

Page p00001 is displayed second and holds one stroke with the three samples of the example in spec 7.5.2.
Page p00002 is displayed first and has no strokes. The expected values are in tests/unit/test_document.cpp.
"""
import json
import struct
import zipfile
from pathlib import Path

OUT = Path(__file__).resolve().parent / "valid" / "minimal.onf"


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


def stroke_body(stroke_id, created_at, color, tool, base_width, flags, samples):
    header = struct.pack("<IQ4sHHHI", stroke_id, created_at, bytes(color), tool, base_width, flags, len(samples))
    block = bytearray()
    previous = (0, 0, 0, 0)
    for x, y, pressure, timestamp in samples:
        block += leb128(zigzag(x - previous[0]))
        block += leb128(zigzag(y - previous[1]))
        block += leb128(zigzag(pressure - previous[2]))
        block += leb128(timestamp - previous[3])
        previous = (x, y, pressure, timestamp)
    return header + bytes(block)


def strokes_file(chunks):
    return b"ONFS" + struct.pack("<H", 1) + b"\x00\x00" + b"".join(chunks) + chunk(2)


manifest = {
    "onf_version": "0.2.0",
    "title": "Minimal",
    "created_at": "2026-06-14T10:32:00.000Z",
    "modified_at": "2026-06-14T12:15:00.000Z",
    "pages": [{"id": "p00001", "order": 2}, {"id": "p00002", "order": 1}],
}

text_object = {
    "id": "t00001", "created_at": 1781433600000, "x": 5000, "y": 3000, "width": 8000, "height": 4000,
    "content": "This is a text object.\nThis is a second line.", "font_family": "Helvetica",
    "generic_family": "sans-serif", "font_size": 423, "color": "#000000FF", "bold": False, "italic": False,
    "layer": "middle",
}

meta_one = {
    "width": 21000, "height": 29700,
    "background": {"type": "dotted", "color": "#FFFFFFFF", "foreground_color": "#000000FF", "spacing": 500},
    "text_objects": [text_object], "assets": [],
}
meta_two = {
    "width": 14800, "height": 21000,
    "background": {"type": "ruled", "color": "#FFFFF0FF", "foreground_color": "#0000FFFF", "spacing": 700},
    "text_objects": [], "assets": [],
}

example_samples = [(10000, 5000, 200, 0), (10004, 5003, 202, 8), (10009, 5007, 200, 16)]
strokes_one = strokes_file([chunk(1, stroke_body(1, 1781433600000, (10, 20, 30, 255), 1, 100, 0, example_samples))])
strokes_two = strokes_file([])


def add(archive, name, data, deflate):
    info = zipfile.ZipInfo(name, date_time=(2026, 6, 14, 10, 32, 0))
    info.compress_type = zipfile.ZIP_DEFLATED if deflate else zipfile.ZIP_STORED
    info.external_attr = 0o100644 << 16
    archive.writestr(info, data)


OUT.parent.mkdir(exist_ok=True)
with zipfile.ZipFile(OUT, "w") as archive:
    add(archive, "manifest.json", json.dumps(manifest, indent=4).encode(), True)
    add(archive, "pages/p00001/meta.json", json.dumps(meta_one, indent=4).encode(), True)
    add(archive, "pages/p00001/strokes.bin", strokes_one, False)
    add(archive, "pages/p00002/meta.json", json.dumps(meta_two, indent=4).encode(), True)
    add(archive, "pages/p00002/strokes.bin", strokes_two, False)
    folder = zipfile.ZipInfo("assets/", date_time=(2026, 6, 14, 10, 32, 0))
    folder.external_attr = (0o40755 << 16) | 0x10
    archive.writestr(folder, b"")
    add(archive, "thumbnail.png", b"PNGDATA", False)
print("wrote", OUT)
