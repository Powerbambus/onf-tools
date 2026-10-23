#!/usr/bin/env python3
"""Writes tests/fixtures/malformed/*.onf and expected.tsv: one broken ONF 0.4.0 file per case of spec 12.3.

Every file is a valid document with exactly one thing wrong. expected.tsv says where the reader must stop
(scope "file" rejects the whole file, "page" makes only that page unreadable, spec 12.3.1) and with which error
name (onf/include/onf/errors.h). The files are written independently of the C++ code and do not depend on it.

    make_malformed_onf.py            write the files
"""
import json
import random
import string
import struct
import zipfile
from pathlib import Path

OUT = Path(__file__).resolve().parent / "malformed"
cases = []  # (name, scope, error name, spec section, writer)


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


def stroke_body(stroke_id=1, flags=0, layer=1, width=100, samples=((10, 20, 255, 0),), count=None, tool=0, raw_samples=None):
    count = len(samples) if count is None else count
    out = struct.pack("<I4sHHHHI", stroke_id, b"\x00\x00\x00\xff", tool, width, flags, layer, count)
    if raw_samples is not None:
        return out + raw_samples
    previous = (0, 0, 0, 0)
    for x, y, pressure, timestamp in samples:
        out += leb128(zigzag(x - previous[0])) + leb128(zigzag(y - previous[1])) + leb128(zigzag(pressure - previous[2]))
        if not flags & (1 << 3):
            out += leb128(timestamp - previous[3])
        previous = (x, y, pressure, timestamp)
    return out


def shape_body(stroke_id=1, tool=0, width=100, layer=1, shape_type=1, parameters=None):
    if parameters is None:
        parameters = struct.pack("<iiiiH", 100, 100, 50, 40, 0)
    return struct.pack("<I4sHHHHH", stroke_id, b"\x00\x00\x00\xff", tool, width, 0, layer, shape_type) + parameters


def strokes_file(chunks, version=2, magic=b"ONFS", end=True):
    return magic + struct.pack("<H", version) + b"\x00\x00" + b"".join(chunks) + (chunk(2) if end else b"")


MANIFEST = {
    "onf_version": "0.4.0", "title": "Malformed", "created_at": "2026-06-14T10:32:00.000Z",
    "modified_at": "2026-06-14T12:15:00.000Z", "pages": [{"id": "p00001", "order": 1}, {"id": "p00002", "order": 2}],
}
TEXT = {
    "id": "t1", "layer": 1, "x": 0, "y": 0, "width": 1000, "height": 500, "content": "Hello", "font_family": "Helvetica",
    "generic_family": "sans-serif", "font_size": 423, "color": "#000000FF", "bold": False, "italic": False,
}
ASSET = {"id": "a1", "layer": 1, "hash": "a" * 64, "extension": "pdf", "pdf_page": 1, "x": 0, "y": 0, "width": 100, "height": 100}
IMAGE = {"id": "a1", "layer": 1, "hash": "a" * 64, "extension": "png", "x": 0, "y": 0, "width": 100, "height": 100}
META = {
    "width": 21000, "height": 29700,
    "background": {"type": "dotted", "color": "#FFFFFFFF", "foreground_color": "#000000FF", "spacing": 500},
    "text_objects": [TEXT], "assets": [ASSET],
}
VALID_STROKES = strokes_file([chunk(1, stroke_body())])


def write(path, manifest=None, metas=None, strokes=None, extra=(), raw_manifest=None, drop=(), raw_metas=None):
    """A document with two pages. Page p00002 is always intact, so that "only this page is unreadable" can be seen."""
    metas = metas or {}
    strokes = strokes or {}
    raw_metas = raw_metas or {}
    entries = {
        "manifest.json": raw_manifest if raw_manifest is not None else json.dumps(MANIFEST if manifest is None else manifest).encode(),
    }
    for page in ("p00001", "p00002"):
        entries[f"pages/{page}/meta.json"] = raw_metas.get(page) or json.dumps(metas.get(page, META)).encode()
        entries[f"pages/{page}/strokes.bin"] = strokes.get(page, VALID_STROKES)
    for name in drop:
        del entries[name]
    with zipfile.ZipFile(path, "w") as archive:
        for name, data in entries.items():
            info = zipfile.ZipInfo(name, date_time=(2026, 6, 14, 10, 32, 0))
            info.compress_type = zipfile.ZIP_STORED if name.endswith(".bin") else zipfile.ZIP_DEFLATED
            info.external_attr = 0o100644 << 16
            archive.writestr(info, data)
        folder = zipfile.ZipInfo("assets/", date_time=(2026, 6, 14, 10, 32, 0))
        folder.external_attr = (0o40755 << 16) | 0x10
        archive.writestr(folder, b"")
        for name, data, attributes in extra:
            info = zipfile.ZipInfo(name, date_time=(2026, 6, 14, 10, 32, 0))
            info.external_attr = attributes
            info.create_system = 3
            info.compress_type = zipfile.ZIP_DEFLATED
            archive.writestr(info, data)


def case(name, scope, error, section, **kwargs):
    cases.append((name, scope, error, section, lambda path, kwargs=kwargs: write(path, **kwargs)))


def raw_case(name, scope, error, section, data):
    cases.append((name, scope, error, section, lambda path, data=data: path.write_bytes(data)))


def page_strokes(data, page="p00001"):
    return {"strokes": {page: data}}


def page_meta(meta, page="p00001"):
    return {"metas": {page: meta}}


def with_meta(**changes):
    meta = json.loads(json.dumps(META))
    meta.update(changes)
    return meta


# ---------------------------------------------------------------- container, spec 4 (file-level)
raw_case("zip_not_an_archive", "file", "zip_open_failed", "4.1", b"definitely not a zip archive" * 20)
raw_case("zip_empty_file", "file", "zip_open_failed", "4.1", b"")
for name, entry_name, error in [("zip_path_traversal", "../evil.txt", "unsafe_path"), ("zip_path_traversal_nested", "pages/../../evil", "unsafe_path"),
                                ("zip_absolute_path", "/etc/evil", "unsafe_path"), ("zip_backslash_path", "pages\\p00001", "unsafe_path"),
                                ("zip_drive_letter", "C:/evil", "unsafe_path")]:
    case(name, "file", error, "4.6", extra=[(entry_name, b"x", 0o100644 << 16)])
case("zip_symlink_entry", "file", "zip_symlink_entry", "4.1", extra=[("link", b"/etc/passwd", 0o120777 << 16)])
case("zip_compression_bomb", "file", "compression_ratio_exceeded", "4.7", extra=[("bomb.bin", bytes(4 * 1024 * 1024), 0o100644 << 16)])

# ---------------------------------------------------------------- manifest, spec 6.1 (file-level)
case("manifest_missing", "file", "manifest_missing", "6.1", drop=["manifest.json"])
case("manifest_not_json", "file", "json_syntax", "2.3.5", raw_manifest=b'{"onf_version": ')
case("manifest_bom", "file", "json_bom", "2.3.5", raw_manifest=b"\xef\xbb\xbf" + json.dumps(MANIFEST).encode())
case("manifest_duplicate_key", "file", "json_duplicate_key", "2.3.5", raw_manifest=b'{"onf_version":"0.3.0","onf_version":"0.3.0"}')
case("manifest_unpaired_surrogate", "file", "json_unpaired_surrogate", "2.3.5",
     raw_manifest=json.dumps(MANIFEST).replace('"Malformed"', '"\\\\ud800"').replace("\\\\", "\\").encode())
case("manifest_too_deep", "file", "json_nesting_too_deep", "12.6", raw_manifest=b'{"x":' + b"[" * 40 + b"]" * 40 + b"}")
case("manifest_float_number", "file", "json_non_integer_number", "2.3.5",
     raw_manifest=json.dumps(MANIFEST).replace('"order": 1', '"order": 1.0').replace('"order":1', '"order":1.0').encode())
case("manifest_integer_too_large", "file", "json_integer_out_of_range", "2.3.5",
     raw_manifest=json.dumps(MANIFEST).replace('"order": 1', '"order": 9007199254740992').replace('"order":1', '"order":9007199254740992').encode())
case("manifest_field_missing", "file", "manifest_invalid_field", "6.1.2", manifest={k: v for k, v in MANIFEST.items() if k != "title"})
case("manifest_bad_date", "file", "manifest_invalid_field", "6.1.2", manifest={**MANIFEST, "created_at": "2026-02-30T10:32:00.000Z"})
case("manifest_version_not_a_string", "file", "manifest_invalid_field", "6.1.2", manifest={**MANIFEST, "onf_version": 3})
case("manifest_generator_not_a_string", "file", "manifest_invalid_field", "6.1.2", manifest={**MANIFEST, "generator": 5})
case("manifest_no_pages", "file", "manifest_pages_invalid", "6.1.2", manifest={**MANIFEST, "pages": []})
case("manifest_duplicate_page_id", "file", "manifest_pages_invalid", "6.1.2",
     manifest={**MANIFEST, "pages": [{"id": "p00001", "order": 1}, {"id": "p00001", "order": 2}]})
case("manifest_duplicate_order", "file", "manifest_pages_invalid", "6.1.2",
     manifest={**MANIFEST, "pages": [{"id": "p00001", "order": 1}, {"id": "p00002", "order": 1}]})
case("manifest_order_gap", "file", "manifest_pages_invalid", "6.1.2",
     manifest={**MANIFEST, "pages": [{"id": "p00001", "order": 1}, {"id": "p00002", "order": 3}]})
case("manifest_id_gap", "file", "manifest_pages_invalid", "6.1.2",
     manifest={**MANIFEST, "pages": [{"id": "p00001", "order": 1}, {"id": "p00003", "order": 2}]})
case("manifest_page_id_traversal", "file", "invalid_identifier", "4.6.1",
     manifest={**MANIFEST, "pages": [{"id": "p00001", "order": 1}, {"id": "../p0002", "order": 2}]})
case("manifest_page_directory_missing", "file", "page_directory_missing", "6.1.2",
     manifest={**MANIFEST, "pages": [{"id": "p00001", "order": 1}, {"id": "p00002", "order": 2}, {"id": "p00003", "order": 3}]})

# ---------------------------------------------------------------- strokes.bin, spec 7 (page-level, page p00001 only)
case("strokes_missing", "page", "page_file_missing", "4.2", drop=["pages/p00001/strokes.bin"])
case("strokes_too_short", "page", "strokes_too_short", "7.2", **page_strokes(b"ONFS\x02"))
case("strokes_bad_magic", "page", "strokes_bad_magic", "7.2", **page_strokes(strokes_file([], magic=b"ONFX")))
case("strokes_version_zero", "page", "strokes_version_zero", "7.2", **page_strokes(strokes_file([], version=0)))
case("strokes_version_of_0_2_0", "page", "strokes_version_unsupported", "7.2", **page_strokes(strokes_file([chunk(1, stroke_body())], version=1)))
case("strokes_version_higher", "page", "strokes_version_unsupported", "7.2", **page_strokes(strokes_file([], version=3)))
case("strokes_truncated_chunk_header", "page", "chunk_header_truncated", "7.3.2", **page_strokes(strokes_file([chunk(1, stroke_body())], end=False) + b"\x01\x00\x07"))
case("strokes_chunk_length_too_large", "page", "chunk_length_exceeds_data", "7.3.2", **page_strokes(strokes_file([struct.pack("<HI", 1, 5000) + b"xx"], end=False)))
case("stroke_header_too_short", "page", "stroke_header_too_short", "7.3.2", **page_strokes(strokes_file([chunk(1, b"x" * 19)])))
case("stroke_header_cut_in_created_at", "page", "stroke_header_too_short", "7.3.2", **page_strokes(strokes_file([chunk(1, stroke_body(flags=1 << 4)[:24])])))
case("stroke_sample_count_zero", "page", "stroke_sample_count_zero", "7.3.2", **page_strokes(strokes_file([chunk(1, stroke_body(count=0, samples=()))])))
case("stroke_base_width_zero", "page", "stroke_base_width_zero", "7.3.2", **page_strokes(strokes_file([chunk(1, stroke_body(width=0))])))
case("stroke_layer_zero", "page", "ink_layer_zero", "5.6", **page_strokes(strokes_file([chunk(1, stroke_body(layer=0))])))
case("stroke_samples_missing", "page", "stroke_samples_truncated", "7.3.2", **page_strokes(strokes_file([chunk(1, stroke_body(count=5))])))
case("stroke_count_exceeds_body", "page", "stroke_samples_truncated", "7.3.2", **page_strokes(strokes_file([chunk(1, stroke_body(count=4000000000))])))
case("stroke_no_timestamp_but_too_few_bytes", "page", "stroke_samples_truncated", "7.3.2",
     **page_strokes(strokes_file([chunk(1, stroke_body(flags=1 << 3, count=3, raw_samples=b"\x00" * 8))])))
case("stroke_duplicate_id", "page", "stroke_id_duplicate", "7.3.2", **page_strokes(strokes_file([chunk(1, stroke_body(7)), chunk(1, stroke_body(7))])))
case("stroke_and_shape_share_id", "page", "stroke_id_duplicate", "7.3.2", **page_strokes(strokes_file([chunk(1, stroke_body(7)), chunk(3, shape_body(7))])))
case("stroke_varint_not_canonical", "page", "varint_not_canonical", "2.3.2", **page_strokes(strokes_file([chunk(1, stroke_body(raw_samples=b"\x80\x00\x00\x00\x00"))])))
case("stroke_varint_too_long", "page", "varint_too_long", "2.3.2", **page_strokes(strokes_file([chunk(1, stroke_body(raw_samples=b"\x80\x80\x80\x80\x80\x01\x00\x00"))])))
case("stroke_varint_out_of_range", "page", "varint_out_of_range", "2.3.2", **page_strokes(strokes_file([chunk(1, stroke_body(raw_samples=b"\x80\x80\x80\x80\x20\x00\x00\x00"))])))
case("stroke_varint_truncated", "page", "stroke_samples_truncated", "7.3.2", **page_strokes(strokes_file([chunk(1, stroke_body(raw_samples=b"\x80"))])))
case("stroke_pressure_out_of_range", "page", "sample_value_out_of_range", "7.5.2", **page_strokes(strokes_file([chunk(1, stroke_body(samples=((0, 0, 256, 0),)))])))
case("stroke_timestamp_out_of_range", "page", "varint_out_of_range", "2.3.2", **page_strokes(strokes_file([chunk(1, stroke_body(raw_samples=b"\x00\x00" + leb128(zigzag(0)) + b"\x80\x80\x80\x80\x10"))])))

# ---------------------------------------------------------------- shapes, spec 7.9 (page-level)
case("shape_header_too_short", "page", "shape_header_too_short", "7.3.2", **page_strokes(strokes_file([chunk(3, b"x" * 15)])))
case("shape_without_shape_type", "page", "shape_header_too_short", "7.3.2", **page_strokes(strokes_file([chunk(3, shape_body()[:17])])))
case("shape_parameters_truncated", "page", "shape_parameters_truncated", "7.3.2", **page_strokes(strokes_file([chunk(3, shape_body()[:-1])])))
case("shape_layer_zero", "page", "ink_layer_zero", "5.6", **page_strokes(strokes_file([chunk(3, shape_body(layer=0))])))
case("shape_base_width_zero", "page", "stroke_base_width_zero", "7.3.2", **page_strokes(strokes_file([chunk(3, shape_body(width=0))])))
case("shape_ellipse_rx_zero", "page", "shape_parameter_invalid", "7.9.4", **page_strokes(strokes_file([chunk(3, shape_body(parameters=struct.pack("<iiiiH", 0, 0, 0, 5, 0)))])))
case("shape_rectangle_height_negative", "page", "shape_parameter_invalid", "7.9.4",
     **page_strokes(strokes_file([chunk(3, shape_body(shape_type=2, parameters=struct.pack("<iiiiH", 0, 0, 5, -5, 0)))])))
case("shape_rotation_36000", "page", "shape_parameter_invalid", "7.9.6", **page_strokes(strokes_file([chunk(3, shape_body(parameters=struct.pack("<iiiiH", 0, 0, 5, 5, 36000)))])))

# ---------------------------------------------------------------- meta.json, spec 6.2, 8, 9 (page-level)
case("meta_missing", "page", "page_file_missing", "4.2", drop=["pages/p00001/meta.json"])
case("meta_not_json", "page", "json_syntax", "2.3.5", raw_metas={"p00001": b'{"width": 21000,'})
case("meta_width_missing", "page", "meta_invalid_field", "6.2.2", **page_meta({k: v for k, v in META.items() if k != "width"}))
case("meta_width_zero", "page", "meta_invalid_field", "6.2.2", **page_meta(with_meta(width=0)))
case("meta_background_field_missing", "page", "background_invalid_field", "6.3.1",
     **page_meta(with_meta(background={"type": "dotted", "color": "#FFFFFFFF", "foreground_color": "#000000FF"})))
def with_margin(value):
    return page_meta(with_meta(background={"type": "ruled", "color": "#FFFFFFFF", "foreground_color": "#000000FF", "spacing": 500, "top_margin": value}))


case("meta_top_margin_negative", "page", "background_invalid_field", "6.3.1", **with_margin(-1))
case("meta_top_margin_too_large", "page", "background_invalid_field", "6.3.1", **with_margin(2147483648))
case("meta_top_margin_string", "page", "background_invalid_field", "6.3.1", **with_margin("10"))
case("meta_top_margin_fraction", "page", "json_non_integer_number", "2.3.5", **with_margin(1.5))
case("meta_text_objects_not_an_array", "page", "meta_invalid_field", "6.2.2", **page_meta(with_meta(text_objects={})))
case("text_layer_zero", "page", "layer_invalid", "5.6", **page_meta(with_meta(text_objects=[{**TEXT, "layer": 0}])))
case("text_layer_too_large", "page", "layer_invalid", "5.6", **page_meta(with_meta(text_objects=[{**TEXT, "layer": 65536}])))
case("text_layer_is_a_string", "page", "meta_invalid_field", "5.6", **page_meta(with_meta(text_objects=[{**TEXT, "layer": "middle"}])))
case("text_layer_missing", "page", "meta_invalid_field", "8.3", **page_meta(with_meta(text_objects=[{k: v for k, v in TEXT.items() if k != "layer"}])))
case("text_font_size_zero", "page", "meta_invalid_field", "8.3", **page_meta(with_meta(text_objects=[{**TEXT, "font_size": 0}])))
case("text_duplicate_id", "page", "duplicate_object_id", "8.3", **page_meta(with_meta(text_objects=[TEXT, TEXT])))
# The string is not repetitive, so that the compression ratio rule of spec 4.7 does not decide the case.
LONG_STRING = "".join(random.Random(7).choice(string.ascii_lowercase) for _ in range(70000))
case("asset_string_too_long", "page", "json_string_too_long", "12.6", **page_meta(with_meta(assets=[{**ASSET, "extension": LONG_STRING}])))
case("spans_overlap", "page", "spans_invalid", "8.3.1", **page_meta(with_meta(text_objects=[{**TEXT, "spans": [{"start": 0, "end": 3}, {"start": 2, "end": 4}]}])))
case("spans_beyond_content", "page", "spans_invalid", "8.3.1", **page_meta(with_meta(text_objects=[{**TEXT, "spans": [{"start": 0, "end": 6}]}])))
case("spans_not_sorted", "page", "spans_invalid", "8.3.1", **page_meta(with_meta(text_objects=[{**TEXT, "spans": [{"start": 3, "end": 4}, {"start": 0, "end": 1}]}])))
case("spans_empty_range", "page", "spans_invalid", "8.3.1", **page_meta(with_meta(text_objects=[{**TEXT, "spans": [{"start": 2, "end": 2}]}])))
case("spans_font_family_without_generic", "page", "spans_invalid", "8.3.1", **page_meta(with_meta(text_objects=[{**TEXT, "spans": [{"start": 0, "end": 1, "font_family": "X"}]}])))
case("spans_too_many", "page", "spans_invalid", "8.3.1",
     **page_meta(with_meta(text_objects=[{**TEXT, "content": "x" * 65536, "spans": [{"start": i, "end": i + 1} for i in range(65536)]}])))
case("asset_layer_zero", "page", "layer_invalid", "5.6", **page_meta(with_meta(assets=[{**ASSET, "layer": 0}])))
case("asset_hash_uppercase", "page", "invalid_identifier", "4.6.1", **page_meta(with_meta(assets=[{**ASSET, "hash": "A" * 64}])))
case("asset_hash_traversal", "page", "invalid_identifier", "4.6.1", **page_meta(with_meta(assets=[{**ASSET, "hash": "../" + "a" * 61}])))
case("asset_pdf_page_missing", "page", "meta_invalid_field", "9.3.2", **page_meta(with_meta(assets=[{k: v for k, v in ASSET.items() if k != "pdf_page"}])))
case("asset_pdf_page_zero", "page", "meta_invalid_field", "9.3.2", **page_meta(with_meta(assets=[{**ASSET, "pdf_page": 0}])))
for name, crop in [("crop_not_an_object", 5), ("crop_is_null", None), ("crop_empty", {}),
                   ("crop_height_missing", {"x": 0, "y": 0, "width": 1}),
                   ("crop_x_negative", {"x": -1, "y": 0, "width": 1, "height": 1}),
                   ("crop_y_negative", {"x": 0, "y": -1, "width": 1, "height": 1}),
                   ("crop_width_zero", {"x": 0, "y": 0, "width": 0, "height": 1}),
                   ("crop_height_zero", {"x": 0, "y": 0, "width": 1, "height": 0}),
                   ("crop_x_too_large", {"x": 2147483648, "y": 0, "width": 1, "height": 1}),
                   ("crop_width_too_large", {"x": 0, "y": 0, "width": 2147483648, "height": 1}),
                   ("crop_member_is_a_string", {"x": "0", "y": 0, "width": 1, "height": 1})]:
    case(name, "page", "crop_invalid", "9.3.2", **page_meta(with_meta(assets=[{**IMAGE, "crop": crop}])))
case("crop_member_is_a_float", "page", "json_non_integer_number", "2.3.5",
     raw_metas={"p00001": json.dumps(with_meta(assets=[{**IMAGE, "crop": {"x": 0, "y": 0, "width": 1, "height": 1}}])).replace('"x": 0,', '"x": 0.5,', 1).encode()})
case("asset_duplicate_id", "page", "duplicate_object_id", "8.3", **page_meta(with_meta(assets=[ASSET, ASSET])))


def main():
    OUT.mkdir(exist_ok=True)
    for old in OUT.glob("*.onf"):
        old.unlink()
    rows = []
    for name, scope, error, section, writer in cases:
        writer(OUT / f"{name}.onf")
        rows.append(f"{name}\t{scope}\t{error}\t{section}")
    (OUT / "expected.tsv").write_text("# file\tscope\terror\tspec section\n" + "\n".join(rows) + "\n", encoding="utf-8")
    print(f"wrote {len(rows)} files to {OUT}")


if __name__ == "__main__":
    main()
