#!/usr/bin/env python3
"""Tests of the onf-reader command line, run by ctest: python3 -I test_reader_cli.py READER FIXTURES_DIR

The reader is started as a program, so these tests cover argument handling, exit codes and the shape of the output.
The rendering rules themselves are checked on the SVG: element counts and the order of the layers.
"""
import re
import subprocess
import sys
import tempfile
import xml.dom.minidom
import zipfile
from pathlib import Path

READER = sys.argv[1]
FIXTURES = Path(sys.argv[2]) / "valid"
failures = []


def run(*args):
    done = subprocess.run([READER, *map(str, args)], capture_output=True, text=True)
    return done.returncode, done.stdout, done.stderr


def check(condition, message):
    if not condition:
        failures.append(message)
        print("FAIL:", message)


def rewritten(source, replacements, target):
    """Copy an ONF file, applying {entry name: function(bytes) -> bytes}."""
    with zipfile.ZipFile(source) as zin, zipfile.ZipFile(target, "w") as zout:
        for info in zin.infolist():
            data = zin.read(info.filename)
            if info.filename in replacements:
                data = replacements[info.filename](data)
            zout.writestr(info, data)


showcase = FIXTURES / "showcase.onf"
minimal = FIXTURES / "minimal.onf"

# --- info ---
code, out, err = run("info", showcase)
check(code == 0, f"info exits 0, got {code}")
check("version:     0.5.0" in out and "generator:   make_showcase_onf.py" in out, "info shows version and generator")
check("6 strokes (285 samples), 5 shapes" in out, "info counts strokes, samples and shapes")
check("5 placements (1 pdf, 4 png)" in out and "layers:      1, 2, 3, 4, 5" in out, "info counts assets and layers")

code, out, err = run("info", minimal)
check(code == 0 and "page 1 (p00002)" in out and "page 2 (p00001)" in out, "pages are listed in display order")

# --- svg ---
code, out, err = run("svg", showcase)
check(code == 0, f"svg exits 0, got {code}")
check("missing_asset" in err and "(spec 9.3.2)" in err, "a missing asset is reported on stderr and is not an error")
check("corrupt_asset" in err and "crop region does not lie inside the image" in err, "a crop outside the image is a corrupt asset, reported and not an error")
check("missing asset" in out, "a missing asset is drawn as a placeholder with an error indication")
try:
    document = xml.dom.minidom.parseString(out)
    check(True, "")
except Exception as e:  # noqa: BLE001
    document = None
    check(False, f"the SVG is well-formed XML: {e}")

if document is not None:
    svg = document.documentElement
    check(svg.getAttribute("width") == "210mm" and svg.getAttribute("height") == "297mm", "the page size is in millimetres")
    check(svg.getAttribute("viewBox") == "0 0 21000 29700", "the view box is in coord units")
    layer_ids = [g.getAttribute("id") for g in document.getElementsByTagName("g") if g.getAttribute("id").startswith("layer")]
    check(layer_ids == ["layer1", "layer2", "layer3", "layer4", "layer5"], f"layers are drawn in ascending order, got {layer_ids}")
    check(len(document.getElementsByTagName("mask")) == 0, "the SVG has no masks")
    grid = [g for g in document.getElementsByTagName("g") if g.getAttribute("stroke-width") == "4"]
    check(len(grid) == 1 and "non-scaling-stroke" not in grid[0].toxml(), "line_width gives lines of that width in coord units")
    check(len(document.getElementsByTagName("ellipse")) == 1 and len(document.getElementsByTagName("polygon")) == 1, "shapes are drawn")
    check(len(document.getElementsByTagName("image")) == 2, "the PNG asset is embedded twice: whole and cropped")
    nested = [e for e in document.getElementsByTagName("svg") if e is not svg]
    check(len(nested) == 1 and nested[0].getAttribute("viewBox") == "8 8 16 16", "the cropped placement is a nested SVG whose view box is the region")
    check(nested and nested[0].getAttribute("width") == "3000" and nested[0].getAttribute("height") == "3000", "the region fills the placement")
    check(out.count("corrupt asset") == 1 and out.count("missing asset") == 1, "a missing and a corrupt asset each get a placeholder")
    check("mix-blend-mode:multiply" in out, "the highlighter uses multiply")
    check(re.search(r"rotate\(30 14000 5000\)", out) is not None, "the ellipse rotation 3000 is 30 degrees clockwise")
    texts = "".join(t.firstChild.data for t in document.getElementsByTagName("tspan") if t.firstChild)
    check("Showcase:" in texts and "second line, larger" in texts, "text and spans are drawn")

with tempfile.TemporaryDirectory() as tmp:
    target = Path(tmp) / "page.svg"
    code, out, err = run("svg", showcase, "--page", "p00001", "-o", target)
    check(code == 0 and out == "" and target.read_text().startswith("<?xml"), "-o writes the file and prints nothing")

    code, out, err = run("svg", minimal, "--number", "1")
    check(code == 0 and 'viewBox="0 0 14800 21000"' in out, "--number 1 is the first displayed page")
    code, out, err = run("svg", minimal)
    check(code == 0 and 'viewBox="0 0 14800 21000"' in out, "the default is the first displayed page")

    # --- errors and exit codes ---
    check(run()[0] == 1, "no arguments is a usage error")
    check(run("info")[0] == 1, "a missing file name is a usage error")
    check(run("frobnicate", showcase)[0] == 1, "an unknown command is a usage error")
    check(run("svg", showcase, "--page", "p00001", "--number", "1")[0] == 1, "--page and --number together are refused")
    check(run("svg", showcase, "--number", "0")[0] == 1, "--number 0 is refused")
    check(run("svg", showcase, "--number", "5")[0] == 1, "a page number beyond the document is refused")
    check(run("info", Path(tmp) / "missing.onf")[0] == 2, "a missing file is a file error")

    garbage = Path(tmp) / "garbage.onf"
    garbage.write_bytes(b"this is not a zip file" * 10)
    code, out, err = run("info", garbage)
    check(code == 2 and "error:" in err, "a file that is not a ZIP archive is a file error")

    code, out, err = run("svg", showcase, "--page", "p00009")
    check(code == 3, "a page that is not in the document is a page error")

    # A file of another ONF version needs the decision of the host (spec 10.2).
    older = Path(tmp) / "older.onf"
    rewritten(showcase, {"manifest.json": lambda d: d.replace(b'"0.5.0"', b'"0.2.0"')}, older)
    code, out, err = run("info", older)
    check(code == 4 and "unsupported_version" in err, "an unsupported version is rejected and reported")
    code, out, err = run("info", older, "--allow-unsupported-version")
    check(code == 0 and "version:     0.2.0" in out, "--allow-unsupported-version opens it")

    # A strokes.bin of the 0.2.0 layout makes the page unreadable, the other output stays (spec 12.3.1).
    v1 = Path(tmp) / "v1.onf"
    rewritten(showcase, {"pages/p00001/strokes.bin": lambda d: d[:4] + b"\x01\x00" + d[6:]}, v1)
    code, out, err = run("info", v1)
    check(code == 3 and "UNREADABLE: 404 strokes_version_unsupported (spec 7.2)" in out, "a strokes.bin version 1 is an unreadable page")
    check("strokes_version_mismatch" in err, "the version mismatch event is reported")
    code, out, err = run("svg", v1)
    check(code == 3 and out == "", "no SVG is written for an unreadable page")

print("onf-reader cli:", "FAILED" if failures else "ok", f"({len(failures)} failures)")
sys.exit(1 if failures else 0)
