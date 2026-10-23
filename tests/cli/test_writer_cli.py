#!/usr/bin/env python3
"""Tests of the onf-writer command line, run by ctest: python3 -I test_writer_cli.py WRITER READER FIXTURES_DIR

onf-writer rewrite reads a file with the library and writes it again. The result is checked with Python's zipfile
and with the independently written fixtures, and the pages are compared through onf-reader.
"""
import subprocess
import sys
import tempfile
import zipfile
from pathlib import Path

WRITER, READER = sys.argv[1], sys.argv[2]
FIXTURES = Path(sys.argv[3]) / "valid"
failures = []


def run(program, *args):
    done = subprocess.run([program, *map(str, args)], capture_output=True, text=True)
    return done.returncode, done.stdout, done.stderr


def check(condition, message):
    if not condition:
        failures.append(message)
        print("FAIL:", message)


def rewritten(source, replacements, target):
    with zipfile.ZipFile(source) as zin, zipfile.ZipFile(target, "w") as zout:
        for info in zin.infolist():
            data = zin.read(info.filename)
            if info.filename in replacements:
                data = replacements[info.filename](data)
            zout.writestr(info, data)


minimal = FIXTURES / "minimal.onf"
showcase = FIXTURES / "showcase.onf"

with tempfile.TemporaryDirectory() as tmp:
    tmp = Path(tmp)
    out = tmp / "out.onf"

    code, stdout, stderr = run(WRITER, "rewrite", minimal, out)
    check(code == 0, f"rewrite exits 0, got {code}: {stderr}")
    check("thumbnail.png is left out" in stderr, "an invalid thumbnail is left out and the reader is told")

    # The archive is a valid ZIP with the layout of spec 4.2 and the compression of spec 4.5.
    with zipfile.ZipFile(out) as archive:
        check(archive.testzip() is None, "the ZIP checksums are correct")
        names = archive.namelist()
        check("assets/" in names and "manifest.json" in names, "assets/ is an explicit entry and the manifest is there")
        check("thumbnail.png" not in names, "the invalid thumbnail is not copied")
        methods = {info.filename: info.compress_type for info in archive.infolist()}
        check(methods["manifest.json"] == zipfile.ZIP_DEFLATED, "manifest.json is deflated")
        check(methods["pages/p00001/meta.json"] == zipfile.ZIP_DEFLATED, "meta.json is deflated")
        check(methods["pages/p00001/strokes.bin"] == zipfile.ZIP_STORED, "strokes.bin is stored")
        check(sorted(n for n in names if n.startswith("pages/")) == [f"pages/p0000{i}/{f}" for i in (1, 2) for f in ("meta.json", "strokes.bin")],
              "the page directories are p00001 and p00002")

        # The page that is displayed second was written by an independent encoder in the fixture. Its strokes.bin is
        # canonical, so the library has to write exactly the same bytes.
        with zipfile.ZipFile(minimal) as original:
            check(archive.read("pages/p00002/strokes.bin") == original.read("pages/p00001/strokes.bin"),
                  "strokes.bin is byte for byte what the independent encoder wrote")
            check(archive.read("pages/p00001/strokes.bin") == original.read("pages/p00002/strokes.bin"), "an empty strokes.bin is the same too")
            asset_names = [n for n in original.namelist() if n.startswith("assets/") and n != "assets/"]
            check(all(archive.read(n) == original.read(n) for n in asset_names) and asset_names, "the asset file is copied unchanged")

    # Both pages look the same as before, display number by display number (the ids were renumbered).
    for number in (1, 2):
        before = run(READER, "svg", minimal, "--number", number)
        after = run(READER, "svg", out, "--number", number)
        check(before[0] == 0 and after[0] == 0 and before[1] == after[1], f"page {number} draws the same SVG after the rewrite")

    code, info, stderr = run(READER, "info", out)
    check(code == 0 and "generator:   onf-tools onf-writer" in info and "title:       Minimal" in info, "onf-reader reads the result and shows the new generator")
    check(stderr == "", f"no events for the result, got {stderr!r}")

    # Writing is deterministic and a second rewrite changes nothing.
    again = tmp / "again.onf"
    check(run(WRITER, "rewrite", out, again)[0] == 0, "rewriting the result works")
    check(out.read_bytes() == again.read_bytes(), "a second rewrite gives the same bytes")

    # A cropped image survives the rewrite (spec 9.3.2, 9.4.5). The showcase file minus its broken placements.
    import json
    complete = tmp / "complete.onf"

    def drop_broken(data):
        meta = json.loads(data)
        meta["assets"] = [a for a in meta["assets"] if a["id"] in ("a00001", "a00002", "a00004")]
        return json.dumps(meta).encode()

    rewritten(showcase, {"pages/p00001/meta.json": drop_broken}, complete)
    copy = tmp / "complete_copy.onf"
    code, stdout, stderr = run(WRITER, "rewrite", complete, copy)
    check(code == 0, f"a document with a cropped image can be rewritten, got {code}: {stderr}")
    with zipfile.ZipFile(copy) as archive:
        meta = json.loads(archive.read("pages/p00001/meta.json"))
        crops = [a.get("crop") for a in meta["assets"]]
        check({"x": 8, "y": 8, "width": 16, "height": 16} in crops, "the crop is written again")
        check(crops.count(None) == 2, "the placements without a crop stay without one")
    before = run(READER, "svg", complete)
    after = run(READER, "svg", copy)
    check(before[0] == 0 and after[0] == 0 and before[1] == after[1] and "viewBox=\"8 8 16 16\"" in after[1],
          "the cropped page draws the same SVG after the rewrite")

    # --- what cannot be copied faithfully ---
    refused = tmp / "refused.onf"
    code, stdout, stderr = run(WRITER, "rewrite", showcase, refused)
    check(code == 3 and "cannot be copied" in stderr and not refused.exists(), "a missing asset stops the rewrite, nothing is written")

    broken = tmp / "broken.onf"
    rewritten(minimal, {"pages/p00001/strokes.bin": lambda d: d[:4] + b"\x01\x00" + d[6:]}, broken)
    code, stdout, stderr = run(WRITER, "rewrite", broken, refused)
    check(code == 3 and "page p00001 cannot be read" in stderr and not refused.exists(), "an unreadable page stops the rewrite")

    # --- arguments and exit codes ---
    check(run(WRITER)[0] == 1, "no arguments is a usage error")
    check(run(WRITER, "rewrite", minimal)[0] == 1, "a missing output is a usage error")
    check(run(WRITER, "copy", minimal, out)[0] == 1, "an unknown command is a usage error")
    check(run(WRITER, "rewrite", out, out)[0] == 1, "the output must not be the input")
    check(run(WRITER, "rewrite", tmp / "missing.onf", refused)[0] == 2, "a missing input is a read error")
    check(run(WRITER, "rewrite", minimal, tmp / "no_such_directory" / "x.onf")[0] == 5, "an output in a missing directory is a write error")

    older = tmp / "older.onf"
    rewritten(minimal, {"manifest.json": lambda d: d.replace(b'"0.4.0"', b'"0.2.0"')}, older)
    code, stdout, stderr = run(WRITER, "rewrite", older, refused)
    check(code == 4 and not refused.exists(), "an unsupported version is rejected")
    code, stdout, stderr = run(WRITER, "rewrite", older, refused, "--allow-unsupported-version")
    check(code == 0 and 'version:     0.4.0' in run(READER, "info", refused)[1], "--allow-unsupported-version rewrites it as 0.4.0")

print("onf-writer cli:", "FAILED" if failures else "ok", f"({len(failures)} failures)")
sys.exit(1 if failures else 0)
