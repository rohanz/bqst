#!/bin/sh
# Renders manual.html to dist/BQST-Manual.pdf with headless Chrome (no window is shown).
#   build.sh [out.pdf]
# The figures in img/ come from the real editor; re-render them with figures.sh.
set -eu
here=$(cd "$(dirname "$0")" && pwd)
out="${1:-$here/../../dist/BQST-Manual.pdf}"
chrome="${CHROME:-/Applications/Google Chrome.app/Contents/MacOS/Google Chrome}"
profile=$(mktemp -d)
trap 'rm -rf "$profile"' EXIT
mkdir -p "$(dirname "$out")"
"$chrome" --headless=new --disable-gpu --no-pdf-header-footer --generate-pdf-document-outline --allow-file-access-from-files \
    --user-data-dir="$profile" --print-to-pdf="$out" "file://$here/manual.html" 2>/dev/null
echo "Wrote $out"
