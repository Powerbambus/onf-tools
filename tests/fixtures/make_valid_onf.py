#!/usr/bin/env python3
"""Writes tests/fixtures/valid/minimal.onf, an ONF 0.3.0 file, independently of the C++ code.

Page p00001 is displayed second and holds two strokes, a shape, a text object with spans and a PDF placement.
Page p00002 is displayed first and has no strokes. The expected values are in tests/unit/test_document.cpp.
"""
import hashlib
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


HAS_CREATED_AT = 1 << 4
NO_TIMESTAMP = 1 << 3
NO_PRESSURE = 1 << 2


def stroke_body(stroke_id, color, tool, base_width, flags, layer, samples, created_at=None):
    """Header of spec 7.4.1 (20 bytes, created_at after it if given) and the delta-coded samples of spec 7.5."""
    if created_at is not None:
        flags |= HAS_CREATED_AT
    out = struct.pack("<I4sHHHHI", stroke_id, bytes(color), tool, base_width, flags, layer, len(samples))
    if created_at is not None:
        out += struct.pack("<Q", created_at)
    block = bytearray()
    previous = (0, 0, 0, 0)
    for x, y, pressure, timestamp in samples:
        block += leb128(zigzag(x - previous[0]))
        block += leb128(zigzag(y - previous[1]))
        block += leb128(zigzag(pressure - previous[2]))
        if not flags & NO_TIMESTAMP:
            block += leb128(timestamp - previous[3])
        previous = (x, y, pressure, timestamp)
    return out + bytes(block)


def shape_body(stroke_id, color, tool, base_width, layer, shape_type, parameters, created_at=None):
    """Header of spec 7.9.2 (16 bytes), optional created_at, shape_type and the parameters of spec 7.9.4."""
    flags = HAS_CREATED_AT if created_at is not None else 0
    out = struct.pack("<I4sHHHH", stroke_id, bytes(color), tool, base_width, flags, layer)
    if created_at is not None:
        out += struct.pack("<Q", created_at)
    return out + struct.pack("<H", shape_type) + parameters


def strokes_file(chunks):
    return b"ONFS" + struct.pack("<H", 2) + b"\x00\x00" + b"".join(chunks) + chunk(2)


manifest = {
    "onf_version": "0.3.0",
    "generator": "make_valid_onf.py",
    "title": "Minimal",
    "created_at": "2026-06-14T10:32:00.000Z",
    "modified_at": "2026-06-14T12:15:00.000Z",
    "pages": [{"id": "p00001", "order": 2}, {"id": "p00002", "order": 1}],
}

text_object = {
    "id": "t00001", "layer": 3, "x": 5000, "y": 3000, "width": 8000, "height": 4000,
    "content": "This is a text object.\nThis is a second line.", "font_family": "Helvetica",
    "generic_family": "sans-serif", "font_size": 423, "color": "#000000FF", "bold": False, "italic": False,
    "spans": [{"start": 0, "end": 4, "bold": True}, {"start": 10, "end": 21, "color": "#FF0000FF", "underline": True}],
    "created_at": 1781433600000,
}

pdf_bytes = b"%PDF-1.7\n% a stand-in for a PDF document\n"
pdf_hash = hashlib.sha256(pdf_bytes).hexdigest()
pdf_asset = {
    "id": "a00001", "layer": 1, "hash": pdf_hash,
    "extension": "pdf", "pdf_page": 3, "x": 0, "y": 0, "width": 21000, "height": 29700,
}

meta_one = {
    "width": 21000, "height": 29700,
    "background": {"type": "dotted", "color": "#FFFFFFFF", "foreground_color": "#000000FF", "spacing": 500},
    "text_objects": [text_object], "assets": [pdf_asset],
}
meta_two = {
    "width": 14800, "height": 21000,
    "background": {"type": "ruled", "color": "#FFFFF0FF", "foreground_color": "#0000FFFF", "spacing": 700},
    "text_objects": [], "assets": [],
}

example_samples = [(10000, 5000, 200, 0), (10004, 5003, 202, 8), (10009, 5007, 200, 16)]
strokes_one = strokes_file([
    # a fountain pen stroke with timestamps and created_at: the example of spec 7.5.2
    chunk(1, stroke_body(1, (10, 20, 30, 255), 1, 100, 0, 2, example_samples, created_at=1781433600000)),
    # vector V1 of spec Appendix C: no pressure, no timestamps, no created_at
    chunk(1, stroke_body(2, (0, 0, 0, 255), 0, 100, NO_PRESSURE | NO_TIMESTAMP, 2, [(10000, 5000, 255, 0), (10004, 5003, 255, 0)])),
    # vector V3 of spec Appendix C: an ellipse
    chunk(3, shape_body(3, (255, 0, 0, 255), 0, 100, 2, 1, struct.pack("<iiiiH", 10500, 14850, 3000, 1500, 4500))),
])
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
    add(archive, f"assets/{pdf_hash}.pdf", pdf_bytes, False)
    add(archive, "thumbnail.png", b"PNGDATA", False)
print("wrote", OUT)
