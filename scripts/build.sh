#!/bin/bash
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$SCRIPT_DIR/.."
BUILD="$ROOT/build"

echo "=== Building Encedo HEM client library ==="
echo "Root: $ROOT"
echo "Build dir: $BUILD"

mkdir -p "$BUILD"
cd "$BUILD"

cmake "$ROOT" \
    -DCMAKE_BUILD_TYPE="${BUILD_TYPE:-Debug}" \
    "$@"

make -j"$(nproc)"

echo ""
echo "=== Build complete ==="
echo "Artifacts:"
echo "  Library : $BUILD/libhem.a"
echo "  Test bin: $BUILD/hem_test"
