#!/usr/bin/env bash
# Regenerates tests/fixtures/real_archives with archive tools other than our own code.
# Needs: zip (Info-ZIP), 7z, python3. The expected file contents are in tests/unit/test_real_archives.cpp.
set -euo pipefail

out="$(cd "$(dirname "$0")" && pwd)/real_archives"
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT

mkdir -p "$work/tree/pages/p00001" "$work/tree/assets"
cd "$work/tree"
printf '{"hello":"world"}\n' > manifest.json
python3 -c "open('pages/p00001/meta.json','w').write('0123456789' * 500)"
python3 -c "open('pages/p00001/strokes.bin','wb').write(bytes(range(256)))"
printf 'PNGDATA' > thumbnail.png

mkdir -p "$out"
zip -q -r "$out/infozip_dirs.zip" manifest.json pages assets thumbnail.png
zip -q -r -D "$out/infozip_nodirs.zip" manifest.json pages assets thumbnail.png
7z a -tzip -bso0 -bsp0 "$out/7z.zip" manifest.json pages assets thumbnail.png

python3 - "$out" <<'PY'
import sys, zipfile
out = sys.argv[1]
with zipfile.ZipFile(out + '/python.zip', 'w', zipfile.ZIP_DEFLATED) as z:
    z.write('manifest.json')
    z.mkdir('pages')
    z.write('pages/p00001/meta.json')
    z.write('pages/p00001/strokes.bin')
    z.mkdir('assets')
    z.write('thumbnail.png')
with zipfile.ZipFile(out + '/python_zip64.zip', 'w', zipfile.ZIP_DEFLATED) as z:
    for name in ['manifest.json', 'pages/p00001/meta.json', 'pages/p00001/strokes.bin', 'thumbnail.png']:
        with z.open(name, 'w', force_zip64=True) as f:
            f.write(open(name, 'rb').read())
PY
