#!/usr/bin/env bash
#
# package-wolfssl.sh — package the SHARED wolfSSL built by build-deps.sh as its
# own release asset (REQ-BUILD-005): hem-tool links it dynamically; the archive
# is published beside hem-tool's, never inside it (ARCHITECTURE.md §1 — wolfSSL
# is GPLv2-or-later / commercial, hem-tool and the SDK are MIT).
#
# implements: REQ-BUILD-005
#
#   scripts/release/package-wolfssl.sh <prefix> <linux|windows> [dist-dir]
#
# Output: <dist>/wolfssl-<v>-<os>-x86_64.{tar.gz|zip} = the runtime library as a
# real file (libwolfssl.so.N / libwolfssl.dll), COPYING, BUILD-CONFIG.txt (the
# exact configure line) and README.txt (upstream tag, source sha256, placement).
# On Linux also <dist>/wolfssl-<v>-stable-src.tar.gz — the unmodified bytes it
# was built from (GPLv2 §3 corresponding source), published once per release
# by the Linux job. SHA256SUMS is regenerated over every archive in <dist>.
set -euo pipefail

PREFIX="$(cd "${1:?usage: package-wolfssl.sh <prefix> <linux|windows> [dist]}" && pwd)"
OS="${2:?linux|windows}"
DIST="${3:-dist}"

V="$(PKG_CONFIG_PATH="$PREFIX/lib/pkgconfig" pkg-config --modversion wolfssl)"
SRC="wolfssl-${V}-stable.tar.gz"
for f in "src/$SRC" "src/$SRC.sha256" wolfssl-build-config.txt COPYING.wolfssl; do
  [ -f "$PREFIX/$f" ] || { echo "$PREFIX/$f missing — rebuild the prefix with build-deps.sh"; exit 1; }
done
SRC_SHA="$(cut -d' ' -f1 "$PREFIX/src/$SRC.sha256")"

if [ "$OS" = "windows" ]; then
  LIBNAME=libwolfssl.dll
  LIB="$PREFIX/bin/$LIBNAME"
  HOWTO="the directory of hem-tool.exe is first in the DLL search order"
else
  LIBNAME="$(readelf -d "$PREFIX/lib/libwolfssl.so" | sed -n -E 's/.*\(SONAME\).*\[(.*)\].*/\1/p')"
  [ -n "$LIBNAME" ] || { echo "no SONAME in $PREFIX/lib/libwolfssl.so"; exit 1; }
  LIB="$PREFIX/lib/$LIBNAME"
  HOWTO="hem-tool searches its own directory first (RUNPATH \$ORIGIN); the file name must stay ${LIBNAME}, the SONAME"
fi
[ -f "$LIB" ] || { echo "$LIB missing"; exit 1; }

NAME="wolfssl-${V}-${OS}-x86_64"
STAGE="$DIST/$NAME"
rm -rf "$STAGE"; mkdir -p "$STAGE"
cp -L "$LIB" "$STAGE/$LIBNAME"            # a real file, never a symlink
cp "$PREFIX/COPYING.wolfssl" "$STAGE/COPYING"
cp "$PREFIX/wolfssl-build-config.txt" "$STAGE/BUILD-CONFIG.txt"
cat > "$STAGE/README.txt" <<EOF
wolfSSL ${V} (${OS}, x86_64) — the TLS/crypto library hem-tool loads at run time.

Contents: ${LIBNAME}; COPYING (GNU GPL version 2 — wolfSSL is licensed "GPLv2
or, at your option, any later version", or commercially by wolfSSL Inc.);
BUILD-CONFIG.txt (the exact CMake configure line and toolchain this library was
built with); this file.

Use: put ${LIBNAME} in the same directory as hem-tool —
${HOWTO}.
hem-tool was compiled against the headers of THIS build: a substitute library
must be built with the same options (BUILD-CONFIG.txt) or its struct layouts
may differ.

Corresponding source (GPLv2 section 3): built UNMODIFIED from
  https://github.com/wolfSSL/wolfssl/releases/tag/v${V}-stable
  ${SRC}  sha256 ${SRC_SHA}
published in the same GitHub release as wolfssl-${V}-stable-src.tar.gz, with the
build script scripts/release/build-deps.sh of
https://github.com/KajtomatO/encedo-hem-c-api (same release tag) and
BUILD-CONFIG.txt above.
EOF

mkdir -p "$DIST"
( cd "$DIST" && rm -f "$NAME.tar.gz" "$NAME.zip" )
if [ "$OS" = "windows" ]; then
  ( cd "$DIST" && zip -qr "$NAME.zip" "$NAME" )
  echo "packaged: $DIST/$NAME.zip"
else
  ( cd "$DIST" && tar czf "$NAME.tar.gz" "$NAME" )
  cp "$PREFIX/src/$SRC" "$DIST/wolfssl-${V}-stable-src.tar.gz"
  echo "packaged: $DIST/$NAME.tar.gz + $DIST/wolfssl-${V}-stable-src.tar.gz"
fi
( cd "$DIST" && find . -maxdepth 1 \( -name '*.tar.gz' -o -name '*.zip' \) -printf '%f\n' \
    | sort | xargs -r sha256sum > SHA256SUMS )
