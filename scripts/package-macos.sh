#!/bin/sh
set -eu

ROOT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
BUILD_DIR="${BUILD_DIR:-$ROOT_DIR/build-release}"
RELEASE_DIR="$BUILD_DIR/BQST_artefacts/Release"
DIST_DIR="$ROOT_DIR/dist"
STAGE_DIR="$DIST_DIR/pkgroot"
PKG_COMPONENT="$DIST_DIR/BQST-components.pkg"
PKG_FLAVOR="${PKG_FLAVOR:-universal}"
PKG_FINAL="${PKG_FINAL:-$DIST_DIR/BQST-1.0.2-macOS-$PKG_FLAVOR.pkg}"
PRODUCT_DISTRIBUTION="$ROOT_DIR/packaging/macos/Distribution.xml"
PRODUCT_RESOURCES="$ROOT_DIR/packaging/macos/resources"

VERSION="1.0.2"
IDENTIFIER="tech.ebraudio.bqst"
PLUGIN_SIGN_IDENTITY="${PLUGIN_SIGN_IDENTITY:--}"
INSTALLER_SIGN_IDENTITY="${INSTALLER_SIGN_IDENTITY:-}"
NOTARY_PROFILE="${NOTARY_PROFILE:-}"
NOTARIZE="${NOTARIZE:-0}"
EXPECTED_MACOS_ARCHS="${EXPECTED_MACOS_ARCHS:-arm64 x86_64}"
export COPYFILE_DISABLE=1

VST3_SRC="$RELEASE_DIR/VST3/BQST.vst3"
AU_SRC="$RELEASE_DIR/AU/BQST.component"
VST3_BINARY="$VST3_SRC/Contents/MacOS/BQST"
AU_BINARY="$AU_SRC/Contents/MacOS/BQST"

die() {
    echo "error: $*" >&2
    exit 1
}

info() {
    echo "==> $*"
}

require_codesign_identity() {
    identity="$1"

    if [ "$identity" = "-" ]; then
        return
    fi

    security find-identity -v -p codesigning | grep -F "$identity" >/dev/null 2>&1 \
        || die "codesigning identity not found: $identity"
}

require_installer_identity() {
    identity="$1"

    [ -n "$identity" ] || die "INSTALLER_SIGN_IDENTITY is required for notarized distribution"

    security find-certificate -c "$identity" >/dev/null 2>&1 \
        || die "installer signing certificate not found: $identity"
}

sign_bundle() {
    identity="$1"
    bundle="$2"

    # Apple discourages --deep for *applying* signatures; these bundles have a single
    # Mach-O and no nested code, so signing the bundle directly is equivalent and correct.
    # --deep is still used (and recommended) for verification below.
    if [ "$identity" = "-" ]; then
        codesign --force -s - "$bundle"
    else
        codesign --force --options runtime --timestamp -s "$identity" "$bundle"
        codesign --verify --deep --strict --verbose=2 "$bundle"
    fi
}

verify_architectures() {
    binary="$1"

    for arch in $EXPECTED_MACOS_ARCHS; do
        lipo "$binary" -verify_arch "$arch" >/dev/null 2>&1 \
            || die "missing required architecture '$arch' in $binary"
    done
}

if [ ! -d "$VST3_SRC" ]; then
    die "missing VST3 artifact: $VST3_SRC
Run: cmake --build build-release --config Release"
fi

if [ ! -d "$AU_SRC" ]; then
    die "missing AU artifact: $AU_SRC
Run: cmake --build build-release --config Release"
fi

[ -f "$VST3_BINARY" ] || die "missing VST3 binary: $VST3_BINARY"
[ -f "$AU_BINARY" ] || die "missing AU binary: $AU_BINARY"

[ -f "$PRODUCT_DISTRIBUTION" ] || die "missing product distribution: $PRODUCT_DISTRIBUTION"
[ -d "$PRODUCT_RESOURCES" ] || die "missing product resources: $PRODUCT_RESOURCES"

info "Verifying plugin architectures: $EXPECTED_MACOS_ARCHS"
verify_architectures "$VST3_BINARY"
verify_architectures "$AU_BINARY"

if [ "$NOTARIZE" = "1" ] || [ -n "$NOTARY_PROFILE" ]; then
    [ -n "$NOTARY_PROFILE" ] || die "NOTARY_PROFILE is required when NOTARIZE=1"
    [ "$PLUGIN_SIGN_IDENTITY" != "-" ] || die "PLUGIN_SIGN_IDENTITY must be a Developer ID Application identity for notarization"
    require_codesign_identity "$PLUGIN_SIGN_IDENTITY"
    require_installer_identity "$INSTALLER_SIGN_IDENTITY"
elif [ "$PLUGIN_SIGN_IDENTITY" != "-" ]; then
    require_codesign_identity "$PLUGIN_SIGN_IDENTITY"
fi

rm -rf "$STAGE_DIR" "$PKG_COMPONENT" "$PKG_FINAL"
mkdir -p "$STAGE_DIR/Library/Audio/Plug-Ins/VST3"
mkdir -p "$STAGE_DIR/Library/Audio/Plug-Ins/Components"
mkdir -p "$DIST_DIR"

info "Staging plugin bundles"
cp -R "$VST3_SRC" "$STAGE_DIR/Library/Audio/Plug-Ins/VST3/BQST.vst3"
cp -R "$AU_SRC" "$STAGE_DIR/Library/Audio/Plug-Ins/Components/BQST.component"
xattr -cr "$STAGE_DIR/Library/Audio/Plug-Ins/VST3/BQST.vst3"
xattr -cr "$STAGE_DIR/Library/Audio/Plug-Ins/Components/BQST.component"
find "$STAGE_DIR" -name '._*' -delete

info "Signing VST3 with identity: $PLUGIN_SIGN_IDENTITY"
sign_bundle "$PLUGIN_SIGN_IDENTITY" "$STAGE_DIR/Library/Audio/Plug-Ins/VST3/BQST.vst3"

info "Signing AU with identity: $PLUGIN_SIGN_IDENTITY"
sign_bundle "$PLUGIN_SIGN_IDENTITY" "$STAGE_DIR/Library/Audio/Plug-Ins/Components/BQST.component"
find "$STAGE_DIR" -name '._*' -delete

info "Building component package"
pkgbuild \
    --root "$STAGE_DIR" \
    --identifier "$IDENTIFIER.plugins" \
    --version "$VERSION" \
    --install-location "/" \
    "$PKG_COMPONENT"

if [ -n "$INSTALLER_SIGN_IDENTITY" ]; then
    info "Building and signing product installer with identity: $INSTALLER_SIGN_IDENTITY"
    productbuild \
        --distribution "$PRODUCT_DISTRIBUTION" \
        --package-path "$DIST_DIR" \
        --resources "$PRODUCT_RESOURCES" \
        --sign "$INSTALLER_SIGN_IDENTITY" \
        "$PKG_FINAL"
else
    info "Building unsigned product installer"
    productbuild \
        --distribution "$PRODUCT_DISTRIBUTION" \
        --package-path "$DIST_DIR" \
        --resources "$PRODUCT_RESOURCES" \
        "$PKG_FINAL"
fi

rm -f "$PKG_COMPONENT"

if [ -n "$INSTALLER_SIGN_IDENTITY" ]; then
    info "Verifying installer signature"
    pkgutil --check-signature "$PKG_FINAL" \
        || die "installer signature verification failed: $PKG_FINAL"
else
    # Unsigned build: pkgutil reports "no signature" (non-zero), which is expected here.
    pkgutil --check-signature "$PKG_FINAL" || true
fi

if [ "$NOTARIZE" = "1" ] || [ -n "$NOTARY_PROFILE" ]; then
    info "Submitting installer for notarization"
    xcrun notarytool submit "$PKG_FINAL" --keychain-profile "$NOTARY_PROFILE" --wait

    info "Stapling notarization ticket"
    xcrun stapler staple "$PKG_FINAL"
    xcrun stapler validate "$PKG_FINAL"
    spctl -a -vv -t install "$PKG_FINAL"
fi

echo "Built installer:"
echo "$PKG_FINAL"
