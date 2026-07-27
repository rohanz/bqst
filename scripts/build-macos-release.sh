#!/bin/sh
set -eu

ROOT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
ARCH="${1:-universal}"
MACOS_DEPLOYMENT_TARGET="${MACOS_DEPLOYMENT_TARGET:-10.13}"

case "$ARCH" in
    universal)
        BUILD_DIR="$ROOT_DIR/build-release"
        OSX_ARCHITECTURES="arm64;x86_64"
        ;;
    arm64)
        BUILD_DIR="$ROOT_DIR/build-release-arm64"
        OSX_ARCHITECTURES="arm64"
        ;;
    x86_64)
        BUILD_DIR="$ROOT_DIR/build-release-x86_64"
        OSX_ARCHITECTURES="x86_64"
        ;;
    *)
        echo "usage: $0 [universal|arm64|x86_64]" >&2
        exit 2
        ;;
esac

# Configure from scratch. An incremental build does not regenerate the bundle Info.plist, so
# after a version bump the artefacts kept the OLD CFBundleShortVersionString while
# package-macos.sh stamped the new number onto the installer -- a package whose payload disagreed
# with its own version. A release build is infrequent enough that the rebuild cost is worth it.
rm -rf "$BUILD_DIR"

cmake -S "$ROOT_DIR" -B "$BUILD_DIR" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_OSX_DEPLOYMENT_TARGET="$MACOS_DEPLOYMENT_TARGET" \
    -DCMAKE_OSX_ARCHITECTURES="$OSX_ARCHITECTURES"

cmake --build "$BUILD_DIR" --config Release

echo "Built BQST macOS $ARCH release:"
echo "$BUILD_DIR/BQST_artefacts/Release/VST3/BQST.vst3"
echo "$BUILD_DIR/BQST_artefacts/Release/AU/BQST.component"
