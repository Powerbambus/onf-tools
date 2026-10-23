# onf-tools

Reader and writer tools for the [OpenNotes Format (ONF)](https://github.com/Powerbambus/open-notes-format/), an open, vendor-neutral file format for handwritten digital notes.

This repository exists to test the specification against independent implementations. It is not a finished product.

## Status

Early development. Targets ONF **0.4.0**, which is a pre-release version without stability guarantees.

| Component | State |
|---|---|
| `onf/` read side | started: build setup, error and diagnostics types, limits, byte cursor, varint and zigzag decoding, value types, strokes.bin parsing (header, chunks, strokes with layer, optional `created_at` and `NO_TIMESTAMP`, shapes, samples), strict JSON parser, color/version/date parsers, entry name and identifier checks, ZIP container with all checks of spec 4, manifest (including `generator`) and meta.json parsing (page, background, text objects with spans, asset placements with `layer`, `pdf_page` and `crop`), version policy, `inspect`/`open`, page reading, and asset loading with the checks of spec 9.2 (SHA-256 against the file name, magic bytes, image header size limit, crop region inside the image; PDF only the magic, a PDF parser is not part of this repository). The read side is complete for ONF 0.4.0 |
| `reader/` | `onf-reader info` and `onf-reader svg`: a command line tool that prints the structure of a file and draws a page as SVG (backgrounds, layers, the four pens with the taper flags, `top_margin`, `line_width`, shapes, text with spans, images with their crop, placeholders). No text wrapping, no PDF rendering |
| `onf/` write side | `write_onf`: validates a document against the rules of spec 11.5, then writes manifest, pages, `strokes.bin` (with `layer`, optional `created_at`, `NO_TIMESTAMP`, the taper flags, shapes), assets named by their SHA-256 and the thumbnail. Compression as in spec 4.5. Crops are validated against the image. The same document gives the same bytes. Checked with the vectors of Appendix C, round trips, and a fuzz target |
| `onf/` erase | `erase_disk` (`onf/edit/erase.h`): erases a disk from the ink of one layer by removing samples, as in spec 7.6.1: strokes are split, border samples are interpolated, cut ends of fountain pen strokes get `NO_TAPER_START` or `NO_TAPER_END`, touched shapes become strokes. An operation for applications, readers do not need it |
| `writer/` | `onf-writer rewrite IN OUT`: reads a file completely and writes it again as ONF 0.4.0 with renumbered pages. It refuses to write if a page or asset cannot be copied faithfully |

The initial goal is a **Minimal Reader** as defined in section 11.3 of the specification. A Full Reader and a Conforming Writer follow later.

## Layout

```
onf-tools/
├── CMakeLists.txt
├── CMakePresets.json
├── onf/            Format layer
│   ├── include/onf/    Public headers (primitives/: byte cursor, varint, UTF-8; model/: value types; read/: strokes.bin, JSON, manifest and meta.json parsers, document API; container/: ZIP archive and entry name checks)
│   └── src/
├── reader/         Command line tool (`onf-reader`), depends only on onf/
├── fuzz/           libFuzzer targets (need Clang, off by default)
├── writer/         Command line tool (`onf-writer`), depends only on onf/
├── tests/
│   ├── unit/           Unit tests for onf/ (doctest)
│   ├── cli/            Tests of the onf-reader command line (Python, run by ctest)
│   ├── support/        Test-only helpers (varint encoder, vector loader, ZIP and strokes.bin builders)
│   ├── vectors/        Varint and zigzag vectors (Appendix A) and STROKE/SHAPE chunk vectors (Appendix C), extracted from the spec
│   └── fixtures/       Archives written by other tools (real_archives/), valid ONF files written by Python scripts (valid/), and one malformed ONF file per case of spec 12.3 (malformed/, with expected.tsv)
├── third_party/
│   └── doctest/        Vendored test framework (MIT)
├── LICENSE
└── README.md
```

The format layer will be split into model, primitives, container and parser as it grows.

### Design rules

- `onf/` knows nothing about rendering, GUIs, or the applications built on it. It has no global state and does not print.
- `reader/` and `writer/` depend on `onf/` and never on each other.
- `onf/` must be testable without `reader/` or `writer/`.
- `onf/` reports problems in two ways: **errors** for malformed files (the spec requires aborting) and **diagnostics** for events the host application must handle (version mismatch, font substitution, missing asset). It never decides on behalf of the application.

The separation is intentional. If the format layer proves useful, it can be extracted into a standalone library after ONF 1.0.0 without rewriting the applications.

## Scope

In scope for the first milestone:

- Opening the ZIP container with path and size validation (spec 4.6, 4.7)
- Parsing `manifest.json`, `meta.json` and `strokes.bin`
- Varint and zigzag decoding, checked against the Appendix A vectors; strokes and shapes against the Appendix C vectors
- Forward compatibility rules (unknown keys, chunks, flags, enum values)
- Exporting a page as an SVG to check the parsed content visually (`onf-reader svg`)

Not done yet: text wrapping and font substitution events, rendering PDF pages, creating documents from scratch on the command line.

## Getting started

Requirements: CMake 3.25 or newer, Ninja, a C++20 compiler (GCC or Clang), and the development files of libzip and zlib. The `asan-ubsan` preset also needs the AddressSanitizer and UndefinedBehaviorSanitizer runtimes of the compiler (on Fedora with GCC: `libasan` and `libubsan`).

```bash
cmake --preset debug
cmake --build --preset debug
ctest --preset debug
```

Use the preset `asan-ubsan` instead of `debug` to build and run the tests with both sanitizers. The command line tool is built as `onf-reader` (in `build/<preset>/reader/`):

```bash
onf-reader info tests/fixtures/valid/showcase.onf
onf-reader svg tests/fixtures/valid/showcase.onf -o page.svg
onf-writer rewrite tests/fixtures/valid/minimal.onf copy.onf
```

Exit codes: 0 ok, 1 usage, 2 the file cannot be read, 3 a page cannot be read, 4 version rejected. Events (missing assets, version mismatches) go to stderr.

### Fuzzing

```bash
cmake -S . -B build/fuzz -G Ninja -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_BUILD_TYPE=Debug -DONF_BUILD_FUZZERS=ON -DONF_BUILD_TESTS=OFF
cmake --build build/fuzz
mkdir -p /tmp/corpus && cp tests/fixtures/valid/*.onf tests/fixtures/malformed/*.onf /tmp/corpus/
build/fuzz/fuzz/fuzz_document /tmp/corpus -max_total_time=300
```

`fuzz_strokes` and `fuzz_meta` take `strokes.bin` and JSON files as seeds.

## Testing

Unit tests for `onf/` live in `tests/unit`. The test suite is built around two more things:

1. The canonical varint and zigzag vectors from Appendix A of the specification. They are stored in `tests/vectors/*.tsv`, generated by `tests/vectors/extract_from_spec.py`. The script also recomputes every table row independently. After a specification change, check that the files are still current:

   ```bash
   python3 -I tests/vectors/extract_from_spec.py --check
   ```

   The script expects the specification at `../../formats/open_notes_format/SPECIFICATION.md`; use `--spec PATH` otherwise. Run it without `--check` to regenerate the files.
2. A set of malformed files, one per failure case of section 12.3 of the specification (bad magic bytes, truncated chunk, non-terminating varint, duplicate page order, path traversal in the archive, a layer of 0, overlapping spans, and so on). They are written by `tests/fixtures/make_malformed_onf.py`, independently of the C++ code. `tests/fixtures/malformed/expected.tsv` says for each file whether the reader must reject the whole file or only one page, and with which error.
3. The chunk vectors V1 to V6 of Appendix C (`tests/vectors/ink_chunks.tsv`, extracted by the same script) and SHA-256 digests computed with Python's `hashlib`.

The valid fixtures come from `tests/fixtures/make_valid_onf.py` and `make_showcase_onf.py`. All fixtures are language-independent and may move to the specification repository later, so that every implementation can test against the same files.

## Relationship to the specification

The specification is the source of truth. If an implementation and the specification disagree, the specification is read again first. Findings from this repository that point to unclear or contradictory wording are reported as issues against the specification repository.

## Contributing

Issues and pull requests are welcome. Please read the specification section that your change touches and reference it in the description.

## License

AGPL-3.0. See `LICENSE`.
