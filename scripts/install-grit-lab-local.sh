#!/bin/sh
# Local audition build only. This installs a separate identity and never replaces BQST.
set -eu
ROOT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
RELEASE_DIR="$ROOT_DIR/build-release/grit-lab/BQST_artefacts/Release"
VST3_DIR="${BQST_VST3_DIR:-$HOME/Library/Audio/Plug-Ins/VST3}"
AU_DIR="$HOME/Library/Audio/Plug-Ins/Components"
PRODUCT="BQST Grit Lab"

# Check both sources before touching either installed bundle.
for bundle in "$RELEASE_DIR/VST3/$PRODUCT.vst3" "$RELEASE_DIR/AU/$PRODUCT.component"; do
    binary="$bundle/Contents/MacOS/$PRODUCT"
    [ -f "$binary" ] || { echo "Missing universal Lab build: $binary" >&2; exit 1; }
    arches=" $(lipo -archs "$binary") "
    case "$arches" in *' arm64 '*) ;; *) echo "Missing arm64 slice" >&2; exit 1 ;; esac
    case "$arches" in *' x86_64 '*) ;; *) echo "Missing Intel slice" >&2; exit 1 ;; esac
    identifier=$(/usr/libexec/PlistBuddy -c 'Print CFBundleIdentifier' "$bundle/Contents/Info.plist")
    [ "$identifier" = 'tech.ebraudio.bqst.gritlab' ] || { echo "Unexpected plugin identity" >&2; exit 1; }
    codesign --verify --deep --strict "$bundle"
done

# Finish the normal release gate in the user's desktop session before installing anything.
PLUGINVAL="${PLUGINVAL:-/Applications/pluginval.app/Contents/MacOS/pluginval}"
[ -x "$PLUGINVAL" ] || { echo "pluginval is required at $PLUGINVAL" >&2; exit 1; }
"$PLUGINVAL" --strictness-level 10 --validate "$RELEASE_DIR/VST3/$PRODUCT.vst3"

install_bundle() {
    source_bundle="$1"
    destination_dir="$2"
    name=$(basename "$source_bundle")
    mkdir -p "$destination_dir"
    staging=$(mktemp -d "$destination_dir/.bqst-grit-lab.XXXXXX")
    # Stage and verify before replacing an older Lab build, if one exists.
    ditto "$source_bundle" "$staging/$name"
    xattr -cr "$staging/$name"
    codesign --force -s - "$staging/$name"
    codesign --verify --deep --strict "$staging/$name"
    rm -rf "$destination_dir/$name"
    mv "$staging/$name" "$destination_dir/$name"
    rmdir "$staging"
    codesign --verify --deep --strict "$destination_dir/$name"
    echo "Installed: $destination_dir/$name"
}
install_bundle "$RELEASE_DIR/VST3/$PRODUCT.vst3" "$VST3_DIR"
install_bundle "$RELEASE_DIR/AU/$PRODUCT.component" "$AU_DIR"
echo "Rescan plugins in your DAW, then load BQST Grit Lab. Insert a FRESH Lab instance for Hybrid, or set the host Grit Model parameter to Hybrid. Select Grit, Mix 100%, Vintage off, Autogain on. Hybrid now blends 75% original Grit at its original Drive mapping with 25% independently remapped Captured Grit."
