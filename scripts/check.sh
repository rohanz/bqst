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

info "Building all plugin formats + unit tests"
TARGETS="BQST_VST3 BQST_Standalone BqstDspTests BqstChainTests BqstCreamTests"
if [ "$(uname)" = "Darwin" ]; then
    # An AU-only compile break used to survive every pre-release check.
    TARGETS="$TARGETS BQST_AU"
fi
# shellcheck disable=SC2086
cmake --build "$BUILD_DIR" --target $TARGETS -j "$JOBS"

info "Running unit tests"
ctest --test-dir "$BUILD_DIR" --output-on-failure

VST3=$(find "$BUILD_DIR" -name 'BQST.vst3' -type d | head -1)

# pluginval is a release gate, so a missing binary must fail rather than quietly pass. Set
# SKIP_PLUGINVAL=1 to opt out deliberately; the final message then says so.
if [ -x "$PLUGINVAL" ] && [ -n "$VST3" ]; then
    info "Validating with pluginval (strictness $STRICTNESS)"
    "$PLUGINVAL" --strictness-level "$STRICTNESS" --validate "$VST3"
    info "All checks passed."
elif [ "${SKIP_PLUGINVAL:-0}" = "1" ]; then
    info "All checks passed EXCEPT pluginval, which was skipped via SKIP_PLUGINVAL=1."
else
    echo "ERROR: pluginval not found at '$PLUGINVAL'${VST3:+}" >&2
    [ -n "$VST3" ] || echo "ERROR: no built BQST.vst3 found under '$BUILD_DIR'" >&2
    echo "Set PLUGINVAL=/path/to/pluginval, or SKIP_PLUGINVAL=1 to bypass deliberately." >&2
    exit 1
fi
