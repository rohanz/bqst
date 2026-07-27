# Shared project metadata, derived from the single source of truth in CMakeLists.txt.
#
# Sourced by the packaging/install scripts so that no script restates the version or the bundle
# identifier. Previously the version was written out by hand in nine places, which meant a bump
# could half-land and produce an installer whose payload disagreed with its own filename.
#
# Usage:  . "$ROOT_DIR/scripts/lib/project-info.sh"
#         echo "$BQST_VERSION" "$BQST_IDENTIFIER"

bqst_read_version() {
    _root="$1"
    _line=$(grep -m1 -E '^[[:space:]]*project\([[:space:]]*BQST[[:space:]]+VERSION' "$_root/CMakeLists.txt" 2>/dev/null || true)
    # project(BQST VERSION 1.2.3 LANGUAGES C CXX) -> 1.2.3
    echo "$_line" | sed -E 's/.*VERSION[[:space:]]+([0-9]+\.[0-9]+\.[0-9]+).*/\1/'
}

bqst_read_identifier() {
    _root="$1"
    # set(BQST_IDENTIFIER "tech.ebraudio.bqst") -> tech.ebraudio.bqst
    # Read the set() line, not the BUNDLE_ID line: the latter now references this variable.
    _line=$(grep -m1 -E '^[[:space:]]*set\([[:space:]]*BQST_IDENTIFIER[[:space:]]' "$_root/CMakeLists.txt" 2>/dev/null || true)
    echo "$_line" | sed -E 's/.*set\([[:space:]]*BQST_IDENTIFIER[[:space:]]+"?([^")]*)"?[[:space:]]*\).*/\1/'
}

BQST_VERSION=$(bqst_read_version "$ROOT_DIR")
BQST_IDENTIFIER=$(bqst_read_identifier "$ROOT_DIR")

case "$BQST_VERSION" in
    [0-9]*.[0-9]*.[0-9]*) ;;
    *)
        echo "ERROR: could not read the project version from $ROOT_DIR/CMakeLists.txt" >&2
        echo "Expected a line like: project(BQST VERSION 1.2.3 LANGUAGES C CXX)" >&2
        exit 1
        ;;
esac

if [ -z "$BQST_IDENTIFIER" ]; then
    echo "ERROR: could not read BUNDLE_ID from $ROOT_DIR/CMakeLists.txt" >&2
    exit 1
fi
