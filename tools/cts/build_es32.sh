#!/bin/sh
# Build VK-GL-CTS for OpenGL ES 3.2 on macOS (surfaceless platform).
#
# Background (plan/plan-02-cts-es32.md F4-F6): the CTS OSX platform only offers
# desktop CGL profiles, so ES runs need the surfaceless platform plus a
# library the loader can dlopen. This script:
#   1. gates on tools/cts/preflight.py (no point building CTS if F5 fails),
#   2. fetches VK-GL-CTS if absent (needs network + ~2 GB),
#   3. configures with -DDEQP_TARGET=surfaceless,
#   4. builds, then points DYLD_LIBRARY_PATH at a libEGL.so symlink to TGL's
#      dylib (CTS dlopens "libEGL.so"; macOS builds "libtgles.dylib").
#
# Usage:
#   sh tools/cts/build_es32.sh [--cts-dir <dir>] [--lib <path/to/libtgles.dylib>]
# After: run ESD32 groups per docs/reference/cts/ES32_GROUPS.md, e.g.
#   ./cts/build/cts-runner --type=es32 --group=dEQP-GLES32.functional.basic

set -eu

# The full suite needs room: ~2 GB shallow clone + fetch_sources.py artifacts
# (ANGLE, glslang, ...) + build outputs. Fail fast with a number, not an ENOSPC
# halfway through a 2-hour build. Checked 2026-09-21: /Volumes/D had 4.3 GiB
# free — NOT enough, so the suite builds on a farm/volume with headroom.
need_gb=15
free_gb=$(df -g "$PWD" | tail -n 1 | awk '{print $4}')
if [ "$free_gb" -lt "$need_gb" ]; then
  echo "need >= ${need_gb} GiB free at $PWD (have ${free_gb} GiB)." >&2
  echo "Run the in-repo gates instead: ctest + tools/cts/preflight.py." >&2
  exit 3
fi

CTS_DIR=""
LIB=""
while [ $# -gt 0 ]; do
  case "$1" in
    --cts-dir) CTS_DIR="$2"; shift 2 ;;
    --lib) LIB="$2"; shift 2 ;;
    *) echo "unknown arg: $1" >&2; exit 2 ;;
  esac
done
[ -n "$CTS_DIR" ] || CTS_DIR="$PWD/cts"
[ -n "$LIB" ] || LIB="$PWD/build/libtgles.dylib"

echo "== [1/4] preflight against $LIB"
python3 tools/cts/preflight.py --lib "$LIB"

if [ ! -d "$CTS_DIR" ]; then
  echo "== [2/4] cloning VK-GL-CTS into $CTS_DIR"
  git clone https://github.com/KhronosGroup/VK-GL-CTS.git "$CTS_DIR"
else
  echo "== [2/4] using existing CTS at $CTS_DIR"
fi

echo "== [3/4] configuring (surfaceless)"
cmake -S "$CTS_DIR" -B "$CTS_DIR/build" \
  -DDEQP_TARGET=surfaceless \
  -DCMAKE_BUILD_TYPE=Release

echo "== [3/4] building (this takes a while; get coffee)"
cmake --build "$CTS_DIR/build" --parallel

echo "== [4/4] wiring libEGL.so -> $LIB"
ln -sf "$LIB" "$CTS_DIR/build/libEGL.so"
echo "run with: DYLD_LIBRARY_PATH=$CTS_DIR/build $CTS_DIR/build/cts-runner --type=es32"
