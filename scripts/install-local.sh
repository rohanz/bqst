#!/bin/sh
set -eu

ROOT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
RELEASE_DIR="$ROOT_DIR/build-release/BQST_artefacts/Release"
CUSTOM_VST3_DIR="${BQST_VST3_DIR:-$HOME/Library/Audio/Plug-Ins/VST3}"
USER_COMPONENT_DIR="$HOME/Library/Audio/Plug-Ins/Components"

# Verify both artefacts exist BEFORE deleting anything. These deletes used to run first, so if
# build-release/ had been cleaned (which the release procedure in AGENTS.md explicitly tells you
# to do) or the build had failed, the installed plugins were destroyed and `set -eu` then aborted
# on the missing source -- leaving the DAW with no plugin at all.
for artefact in "$RELEASE_DIR/VST3/BQST.vst3" "$RELEASE_DIR/AU/BQST.component"; do
    if [ ! -d "$artefact" ]; then
        echo "ERROR: missing build artefact: $artefact" >&2
        echo "Run scripts/build-macos-release.sh first. Nothing was installed or removed." >&2
        exit 1
    fi
done

mkdir -p "$CUSTOM_VST3_DIR"
mkdir -p "$USER_COMPONENT_DIR"

rm -rf "$CUSTOM_VST3_DIR/BQST.vst3"
cp -R "$RELEASE_DIR/VST3/BQST.vst3" "$CUSTOM_VST3_DIR/BQST.vst3"
xattr -cr "$CUSTOM_VST3_DIR/BQST.vst3"
codesign --force --deep -s - "$CUSTOM_VST3_DIR/BQST.vst3"

rm -rf "$USER_COMPONENT_DIR/BQST.component"
cp -R "$RELEASE_DIR/AU/BQST.component" "$USER_COMPONENT_DIR/BQST.component"
xattr -cr "$USER_COMPONENT_DIR/BQST.component"
codesign --force --deep -s - "$USER_COMPONENT_DIR/BQST.component"

echo "Installed BQST VST3 to: $CUSTOM_VST3_DIR/BQST.vst3"
echo "Installed BQST AU to:   $USER_COMPONENT_DIR/BQST.component"
