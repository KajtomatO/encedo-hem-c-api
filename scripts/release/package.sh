#!/usr/bin/env bash
#
# package.sh — build hem-tool as a single-file Release binary against the
# static prefix, run the unit suite on that build, assert it is static, and
# package the release archive (REQ-BUILD-005).
#
# implements: REQ-BUILD-005
#
#   scripts/release/package.sh <prefix> <linux|windows> [dist-dir]
#
# Output: <dist>/hem-tool-<version>[+g<sha>]-<os>-x86_64.{tar.gz|zip} and the
# matching line in <dist>/SHA256SUMS. On a tag build (RELEASE_TAG=v…) the tag
# must equal the project version — the mismatch fails here, before publishing.
set -euo pipefail

PREFIX="$(cd "${1:?usage: package.sh <prefix> <linux|windows> [dist]}" && pwd)"
OS="${2:?linux|windows}"
DIST="${3:-dist}"
BUILD="${BUILD_DIR:-build-release}"
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$ROOT"

VERSION="$(sed -n -E '/^project\(encedo-hem/,/\)/{s/^[[:space:]]*VERSION[[:space:]]+([0-9]+\.[0-9]+\.[0-9]+).*/\1/p}' CMakeLists.txt | head -1)"
[ -n "$VERSION" ] || { echo "cannot read the project version from CMakeLists.txt"; exit 1; }
SHA="$(git rev-parse --short HEAD 2>/dev/null || echo unknown)"
if [ -n "${RELEASE_TAG:-}" ]; then
  [ "$RELEASE_TAG" = "v$VERSION" ] \
    || { echo "tag $RELEASE_TAG does not match the project version v$VERSION"; exit 1; }
  LABEL="$VERSION"
else
  LABEL="${VERSION}+g${SHA}"
fi
NAME="hem-tool-${LABEL}-${OS}-x86_64"

# --- build (Release, static deps) -------------------------------------------
export PKG_CONFIG_PATH="$PREFIX/lib/pkgconfig${PKG_CONFIG_PATH:+:$PKG_CONFIG_PATH}"
EXTRA=()
if [ "$OS" = "windows" ]; then
  EXTRA+=(-DCMAKE_EXE_LINKER_FLAGS=-static)   # libgcc / libwinpthread in, too
fi
# CMAKE_C_STANDARD_LIBRARIES appends -lm at the END of every link line: the
# static libwolfssl.a references pow()/log(), and pkg-config's non-static
# Libs line (what CMake's pkg_check_modules reads) does not carry its
# Libs.private, so -lm would otherwise precede the archive and go unresolved.
cmake -B "$BUILD" -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_PREFIX_PATH="$PREFIX" -DCURL_USE_STATIC_LIBS=ON \
  -DCMAKE_C_STANDARD_LIBRARIES="-lm" "${EXTRA[@]}"
cmake --build "$BUILD"
ctest --test-dir "$BUILD" -L unit --output-on-failure

# --- static check ------------------------------------------------------------
if [ "$OS" = "windows" ]; then
  BIN="$BUILD/hem-tool.exe"
  DLLS="$(objdump -p "$BIN" | awk '/DLL Name:/ {print tolower($3)}' | sort -u)"
  echo "imports:"; echo "$DLLS" | sed 's/^/  /'
  if echo "$DLLS" | grep -E -q 'curl|wolfssl|libgcc|winpthread|libssl|libcrypto|zlib'; then
    echo "NOT static: a non-system DLL is imported"; exit 1
  fi
else
  BIN="$BUILD/hem-tool"
  echo "shared objects:"; ldd "$BIN" | sed 's/^/  /'
  if ldd "$BIN" | grep -E -q 'curl|wolfssl|libssl|libcrypto'; then
    echo "NOT static: libcurl/wolfSSL are still dynamic"; exit 1
  fi
fi
"$BIN" help > /dev/null

# --- package -----------------------------------------------------------------
STAGE="$DIST/$NAME"
rm -rf "$STAGE"; mkdir -p "$STAGE/LICENSES"
cp "$BIN" "$STAGE/"
cp LICENSE "$STAGE/LICENSES/LICENSE.encedo-hem"
cp "$PREFIX/COPYING.curl" "$STAGE/LICENSES/COPYING.curl"
cp "$PREFIX/COPYING.wolfssl" "$STAGE/LICENSES/COPYING.wolfssl"
cp src/vendor/cjson/LICENSE "$STAGE/LICENSES/LICENSE.cjson"
cp src/vendor/bip39/LICENSE "$STAGE/LICENSES/LICENSE.bip39-wordlist"
cp src/tools/hem-tool/vendor/qrcodegen/LICENSE "$STAGE/LICENSES/LICENSE.qrcodegen" 2>/dev/null || true
cat > "$STAGE/README.txt" <<EOF
hem-tool ${LABEL} (${OS}, x86_64) — the Encedo HEM device CLI.

Single-file static binary: libcurl ${CURL_VERSION:-pinned} with wolfSSL
${WOLFSSL_VERSION:-pinned} as its TLS backend are linked in; only the C
runtime is dynamic. Verify the download against SHA256SUMS:
  sha256sum -c SHA256SUMS      (Linux)
  CertUtil -hashfile <file> SHA256   (Windows)

Usage: hem-tool help            (commands grouped by what they need)
       hem-tool help <command>  (per-command options)
Device URL via --url or EHEM_URL (default https://my.ence.do); passphrase via
--passphrase or EHEM_PASSPHRASE.

Licenses: see LICENSES/ — hem-tool and the Encedo HEM C SDK are MIT; libcurl
(curl license) and wolfSSL (GPLv3, or Encedo's commercial wolfSSL license —
see the project's ARCHITECTURE.md §12 risk 1) are distributed inside this
binary. Source: https://github.com/KajtomatO/encedo-hem-c-api (tag v${VERSION}).
EOF

mkdir -p "$DIST"
( cd "$DIST" && rm -f "$NAME.tar.gz" "$NAME.zip" )
if [ "$OS" = "windows" ]; then
  ( cd "$DIST" && zip -qr "$NAME.zip" "$NAME" ) && ARCHIVE="$NAME.zip"
else
  ( cd "$DIST" && tar czf "$NAME.tar.gz" "$NAME" ) && ARCHIVE="$NAME.tar.gz"
fi
( cd "$DIST" && sha256sum "$ARCHIVE" >> SHA256SUMS )
echo "packaged: $DIST/$ARCHIVE"
