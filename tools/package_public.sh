#!/usr/bin/env bash
# Package the distributable Windows build into dist/<name>.zip.
#   bash tools/package_public.sh <build-dir configured with -DRNRR_PUBLIC_BUILD=ON>
# The zip holds the executable, its own assets, the mod catalog, the licenses
# and LEEME.txt -- and refuses to run on a build that carries generated code,
# so nothing derived from a ROM can be packaged by accident.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD="${1:?usage: package_public.sh <public build dir>}"
VERSION="$(tr -d '[:space:]' < "$ROOT/VERSION")"
NAME="RockAndRollRacingPC-$VERSION-windows"
EXE="$BUILD/RockNRollRacingSNESRecomp.exe"

[ -f "$EXE" ] || { echo "no executable at $EXE" >&2; exit 1; }
grep -q 'RNRR_PUBLIC_BUILD:BOOL=ON' "$BUILD/CMakeCache.txt" ||
  { echo "$BUILD is not a public build (configure with -DRNRR_PUBLIC_BUILD=ON)" >&2; exit 1; }
if grep -q 'src/gen/' "$BUILD/build.ninja" 2>/dev/null; then
  echo "$BUILD compiles generated code; refusing to package it" >&2; exit 1
fi

STAGE="$ROOT/dist/$NAME"
rm -rf "$STAGE" "$ROOT/dist/$NAME.zip"
mkdir -p "$STAGE/mods/preloaded" "$STAGE/assets"
cp "$EXE" "$STAGE/"
cp -r "$ROOT/assets/shaders" "$ROOT/assets/fonts" "$STAGE/assets/"
cp -r "$ROOT/mods/preloaded/packages" "$STAGE/mods/preloaded/"
cp -r "$ROOT/licenses" "$STAGE/"
cp "$ROOT/LICENSE" "$STAGE/LICENSE.txt"
cp "$ROOT/LEEME.txt" "$STAGE/"
rm -f "$STAGE/mods/preloaded/packages/.gitkeep"

python - "$ROOT/dist" "$NAME" <<'PY'
import os, sys, zipfile
dist, name = sys.argv[1], sys.argv[2]
with zipfile.ZipFile(os.path.join(dist, name + ".zip"), "w", zipfile.ZIP_DEFLATED) as z:
    for base, _, files in os.walk(os.path.join(dist, name)):
        for f in files:
            full = os.path.join(base, f)
            z.write(full, os.path.relpath(full, dist))
PY
ls -la "$ROOT/dist/$NAME.zip"
