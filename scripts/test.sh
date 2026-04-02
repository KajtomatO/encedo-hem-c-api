#!/bin/bash
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$SCRIPT_DIR/.."
BIN="$ROOT/build/hem_test"

if [ ! -f "$BIN" ]; then
    echo "hem_test not found -- run scripts/build.sh first"
    exit 1
fi

if [ $# -lt 1 ]; then
    echo "Usage: $0 <device_url> [passphrase]"
    echo "  device_url   e.g. https://my.ence.do"
    echo "  passphrase   required for authenticated operations"
    exit 1
fi

echo "=== Running HEM test against $1 ==="
"$BIN" "$@"
