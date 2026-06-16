#!/bin/sh
# Local pre-commit / pre-release checks: configure, build the VST3 + unit tests, run the unit
# tests, and validate the plugin with pluginval. This is the local equivalent of CI.
#
# Uses a native-arch build for speed (the release build is universal; see
# scripts/build-macos-release.sh). Override defaults with env vars:
#   BUILD_DIR=...    build directory (default: build)
#   STRICTNESS=...   pluginval strictness level (default: 10)
#   PLUGINVAL=...    path to the pluginval binary (default: the installed app)
set -eu

ROOT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
BUILD_DIR="${BUILD_DIR:-$ROOT_DIR/build}"
STRICTNESS="${STRICTNESS:-10}"
PLUGINVAL="${PLUGINVAL:-/Applications/pluginval.app/Contents/MacOS/pluginval}"
ARCH=$(uname -m)
JOBS=$(sysctl -n hw.ncpu 2>/dev/null || echo 4)

info() { echo "==> $*"; }

info "Configuring ($ARCH) in $BUILD_DIR"
cmake -S "$ROOT_DIR" -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES="$ARCH" >/dev/null

info "Building VST3 + unit tests"
cmake --build "$BUILD_DIR" --target BQST_VST3 BqstDspTests -j "$JOBS"

info "Running unit tests"
ctest --test-dir "$BUILD_DIR" --output-on-failure

VST3=$(find "$BUILD_DIR" -name 'BQST.vst3' -type d | head -1)

if [ -x "$PLUGINVAL" ] && [ -n "$VST3" ]; then
    info "Validating with pluginval (strictness $STRICTNESS)"
    "$PLUGINVAL" --strictness-level "$STRICTNESS" --validate "$VST3"
else
    info "Skipping pluginval (not found at '$PLUGINVAL', or VST3 missing). Set PLUGINVAL to override."
fi

info "All checks passed."
