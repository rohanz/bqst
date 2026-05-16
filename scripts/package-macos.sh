#!/bin/sh
set -eu

ROOT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
BUILD_DIR="$ROOT_DIR/build-release"
RELEASE_DIR="$BUILD_DIR/BQST_artefacts/Release"
DIST_DIR="$ROOT_DIR/dist"
STAGE_DIR="$DIST_DIR/pkgroot"
PKG_COMPONENT="$DIST_DIR/BQST-components.pkg"
PKG_FINAL="$DIST_DIR/BQST-1.0.0-macOS.pkg"

VERSION="1.0.0"
IDENTIFIER="xyz.rohanjk.bqst"
PLUGIN_SIGN_IDENTITY="${PLUGIN_SIGN_IDENTITY:--}"
INSTALLER_SIGN_IDENTITY="${INSTALLER_SIGN_IDENTITY:-}"

VST3_SRC="$RELEASE_DIR/VST3/BQST.vst3"
AU_SRC="$RELEASE_DIR/AU/BQST.component"

if [ ! -d "$VST3_SRC" ]; then
    echo "Missing VST3 artifact: $VST3_SRC" >&2
    echo "Run: cmake --build build-release --config Release" >&2
    exit 1
fi

if [ ! -d "$AU_SRC" ]; then
    echo "Missing AU artifact: $AU_SRC" >&2
    echo "Run: cmake --build build-release --config Release" >&2
    exit 1
fi

rm -rf "$STAGE_DIR" "$PKG_COMPONENT" "$PKG_FINAL"
mkdir -p "$STAGE_DIR/Library/Audio/Plug-Ins/VST3"
mkdir -p "$STAGE_DIR/Library/Audio/Plug-Ins/Components"
mkdir -p "$DIST_DIR"

cp -R "$VST3_SRC" "$STAGE_DIR/Library/Audio/Plug-Ins/VST3/BQST.vst3"
cp -R "$AU_SRC" "$STAGE_DIR/Library/Audio/Plug-Ins/Components/BQST.component"
xattr -cr "$STAGE_DIR/Library/Audio/Plug-Ins/VST3/BQST.vst3"
xattr -cr "$STAGE_DIR/Library/Audio/Plug-Ins/Components/BQST.component"

codesign --force --deep --options runtime --timestamp -s "$PLUGIN_SIGN_IDENTITY" "$STAGE_DIR/Library/Audio/Plug-Ins/VST3/BQST.vst3"
codesign --force --deep --options runtime --timestamp -s "$PLUGIN_SIGN_IDENTITY" "$STAGE_DIR/Library/Audio/Plug-Ins/Components/BQST.component"

pkgbuild \
    --root "$STAGE_DIR" \
    --identifier "$IDENTIFIER.plugins" \
    --version "$VERSION" \
    --install-location "/" \
    "$PKG_COMPONENT"

if [ -n "$INSTALLER_SIGN_IDENTITY" ]; then
    productsign --sign "$INSTALLER_SIGN_IDENTITY" "$PKG_COMPONENT" "$PKG_FINAL"
    rm -f "$PKG_COMPONENT"
else
    mv "$PKG_COMPONENT" "$PKG_FINAL"
fi

pkgutil --check-signature "$PKG_FINAL" || true

echo "Built installer:"
echo "$PKG_FINAL"
