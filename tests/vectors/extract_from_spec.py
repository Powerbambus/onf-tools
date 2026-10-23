#!/usr/bin/env python3
"""Extract the test vectors from the ONF specification: varint and zigzag (Appendix A.3), STROKE and SHAPE chunks (Appendix C).

    extract_from_spec.py [--spec PATH]            write the .tsv files next to this script
    extract_from_spec.py [--spec PATH] --check    compare the .tsv files with the specification

The tables are also recomputed here with an independent encoder. A mismatch means the
specification table is inconsistent and is reported instead of written.
"""
import argparse
import re
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
DEFAULT_SPEC = HERE.parents[3] / "formats" / "open_notes_format" / "SPECIFICATION.md"

ERROR_BY_REASON = [
    ("not canonical", "varint_not_canonical"),
    ("exceeds the maximum", "varint_out_of_range"),
    ("continuation bit set on the fifth byte", "varint_too_long"),
    ("data ends inside", "varint_truncated"),
]


def table_rows(lines, header_prefix):
    start = next((i for i, l in enumerate(lines) if l.startswith(header_prefix)), None)
    if start is None:
        sys.exit(f"table not found: {header_prefix!r}")
    rows = []
    for line in lines[start + 2:]:
        if not line.startswith("|"):
            break
        rows.append([c.strip() for c in line.strip().strip("|").split("|")])
    if not rows:
        sys.exit(f"table is empty: {header_prefix!r}")
    return rows


def hex_bytes(cell):
    m = re.search(r"`((?:0x[0-9A-Fa-f]{2})(?: 0x[0-9A-Fa-f]{2})*)`", cell)
    if not m:
        sys.exit(f"no byte sequence in cell: {cell!r}")
    return " ".join(b[2:].lower() for b in m.group(1).split())


def leb128(value):
    out = []
    while value > 0x7F:
        out.append((value & 0x7F) | 0x80)
        value >>= 7
    out.append(value)
    return " ".join(f"{b:02x}" for b in out)


def zigzag(n):
    return (n << 1) if n >= 0 else ((-n << 1) - 1)


def chunk_vectors(lines):
    """V1 to V6 of Appendix C: a bold heading "**Vn: ...**" followed by a fenced block with the whole chunk."""
    rows = []
    i = 0
    while i < len(lines):
        m = re.match(r"\*\*(V\d+): ", lines[i])
        if m:
            start = next(j for j in range(i, len(lines)) if lines[j].startswith("```"))
            end = next(j for j in range(start + 1, len(lines)) if lines[j].startswith("```"))
            data = bytes.fromhex("".join(lines[start + 1:end]).replace(" ", ""))
            chunk_type = int.from_bytes(data[0:2], "little")
            length = int.from_bytes(data[2:6], "little")
            if length != len(data) - 6:
                sys.exit(f"{m.group(1)}: chunk_length {length} does not match the body of {len(data) - 6} bytes")
            rows.append(f"{m.group(1)}\t{chunk_type}\t{data.hex(' ')}")
            i = end
        i += 1
    if not rows:
        sys.exit("no chunk vectors found in Appendix C")
    return rows


def build(spec_text):
    lines = spec_text.splitlines()

    zigzag_rows = []
    for signed, zz, cell in table_rows(lines, "| Signed delta | Zigzag value | LEB128 bytes |"):
        signed, zz, data = int(signed), int(zz), hex_bytes(cell)
        if zigzag(signed) != zz or leb128(zz) != data:
            sys.exit(f"inconsistent zigzag row in the specification: {signed} {zz} {data}")
        zigzag_rows.append(f"{signed}\t{zz}\t{data}")

    plain_rows = []
    for value, cell in table_rows(lines, "| Unsigned value | LEB128 bytes |"):
        value, data = int(value), hex_bytes(cell)
        if leb128(value) != data:
            sys.exit(f"inconsistent plain varint row in the specification: {value} {data}")
        plain_rows.append(f"{value}\t{data}")

    malformed_rows = []
    for cell, field, reason in table_rows(lines, "| Bytes | Field | Reason |"):
        field = field.strip("`")
        error = next((e for key, e in ERROR_BY_REASON if key in reason.lower()), None)
        if error is None:
            sys.exit(f"unknown reason in the specification, extend ERROR_BY_REASON: {reason!r}")
        malformed_rows.append(f"{hex_bytes(cell)}\t{field}\t{error}")

    return {
        "zigzag.tsv": "# signed\tzigzag\tbytes\n" + "\n".join(zigzag_rows) + "\n",
        "varint_plain.tsv": "# value\tbytes\n" + "\n".join(plain_rows) + "\n",
        "varint_malformed.tsv": "# bytes\tfield\terror\n" + "\n".join(malformed_rows) + "\n",
        "ink_chunks.tsv": "# name\tchunk_type\tbytes (the whole chunk including the 6-byte frame)\n"
                          + "\n".join(chunk_vectors(lines)) + "\n",
    }


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--spec", type=Path, default=DEFAULT_SPEC)
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()

    if not args.spec.is_file():
        sys.exit(f"specification not found: {args.spec} (use --spec)")
    files = build(args.spec.read_text(encoding="utf-8"))

    if args.check:
        stale = [n for n, c in files.items()
                 if not (HERE / n).exists() or (HERE / n).read_text(encoding="utf-8") != c]
        if stale:
            sys.exit("out of date with the specification: " + ", ".join(stale))
        print("vectors match the specification")
        return

    for name, content in files.items():
        (HERE / name).write_text(content, encoding="utf-8")
        print(f"wrote {name}")


if __name__ == "__main__":
    main()
