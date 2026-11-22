#!/bin/sh
# Re-renders the manual's figures from the real editor, offscreen (no window is shown).
#   figures.sh <path/to/BqstEditorSnapshot>
# Build that tool first: cmake --build <build-dir> --target BqstEditorSnapshot
set -eu
here=$(cd "$(dirname "$0")" && pwd)
snap="$1"
raw=$(mktemp -d)
"$snap" 865 "$raw/full.png" 3
"$snap" 865 "$raw/full6.png" 6
"$snap" 865 "$raw/about.png" 3 about
"$snap" menu "$raw/menu.png" 3
"$snap" tips "$raw/tips.png" 3
"$snap" vu "$raw/vu.png" 3
uv run --with pillow python "$here/figures.py" "$raw" "$here/img"
rm -rf "$raw"
