#!/usr/bin/env bash
#
# package-lib.sh — the SDK library package, wolfSSL flavour (REQ-BUILD-009):
# the SHARED library only — no static library ships with wolfSSL (user decision
# 2026-10-14; static linking is the OpenSSL -static package). A shared-only
# Release build against the prefix from build-deps.sh (libcurl STATIC inside the
# library, wolfSSL DYNAMIC), installed, packaged as
# encedo-hem-<label>-<os>-x86_64.{tar.gz|zip} and smoke-tested exactly as a user
# unpacks it beside the wolfSSL companion asset: the REQ-BUILD-008 sample
# consumer, built against the unpacked package via find_package and pkg-config,
# runs (tests/consumer/check_consumer.cmake, package mode).
#
# implements: REQ-BUILD-009
#
#   scripts/release/package-lib.sh <prefix> <linux|windows> <dist> <label> <source ref>
#
# Called by package.sh once the companion asset is in <dist>; package.sh writes
# SHA256SUMS afterwards.
set -euo pipefail

PREFIX="$(cd "${1:?usage: package-lib.sh <prefix> <linux|windows> <dist> <label> <source ref>}" && pwd)"
OS="${2:?linux|windows}"
DIST="$(mkdir -p "${3:?dist}" && cd "$3" && pwd)"
LABEL="${4:?label}"
SRC_REF="${5:?source ref}"
BUILD="${LIB_BUILD_DIR:-build-lib}"
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$ROOT"

fail() { echo "package-lib.sh: $*"; exit 1; }
# Paths handed to native Windows tools in their own form.
native() { if [ "$OS" = "windows" ]; then cygpath -m "$1"; else printf '%s' "$1"; fi; }

VERSION="$(sed -n -E '/^project\(encedo-hem/,/\)/{s/^[[:space:]]*VERSION[[:space:]]+([0-9]+\.[0-9]+\.[0-9]+).*/\1/p}' CMakeLists.txt | head -1)"
[ -n "$VERSION" ] || fail "cannot read the project version from CMakeLists.txt"
export PKG_CONFIG_PATH="$PREFIX/lib/pkgconfig${PKG_CONFIG_PATH:+:$PKG_CONFIG_PATH}"
WOLFSSL_VERSION="$(pkg-config --modversion wolfssl)"
CURL_VERSION="$(pkg-config --modversion libcurl)"
WNAME="wolfssl-${WOLFSSL_VERSION}-${OS}-x86_64"
NAME="encedo-hem-${LABEL}-${OS}-x86_64"

EXTRA=()
if [ "$OS" = "windows" ]; then
  # -static folds libgcc + libwinpthread into the DLL, as build-deps.sh does for
  # libwolfssl.dll: the package's DLL imports system DLLs and libwolfssl.dll.
  EXTRA+=(-DCMAKE_SHARED_LINKER_FLAGS=-static)
  WLIB_NAME=libwolfssl.dll; EXT=zip; RUNTIME_DIR=bin
else
  # RUNPATH $ORIGIN: the library finds the companion libwolfssl.so.N put beside it.
  # shellcheck disable=SC2016  # $ORIGIN is a literal for the loader, not a shell variable
  EXTRA+=(-DCMAKE_BUILD_WITH_INSTALL_RPATH=ON '-DCMAKE_INSTALL_RPATH=$ORIGIN')
  WLIB_NAME="$(readelf -d "$PREFIX/lib/libwolfssl.so" | sed -n -E 's/.*\(SONAME\).*\[(.*)\].*/\1/p')"
  [ -n "$WLIB_NAME" ] || fail "no SONAME in $PREFIX/lib/libwolfssl.so"
  EXT=tar.gz; RUNTIME_DIR=lib
fi

# --- build + install: the shared library alone --------------------------------
cmake -B "$BUILD" -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="$PREFIX" \
  -DCMAKE_INSTALL_LIBDIR=lib -DEHEM_BUILD_STATIC=OFF -DEHEM_BUILD_TOOL=OFF \
  -DBUILD_TESTING=OFF "${EXTRA[@]}"
cmake --build "$BUILD"
STAGE="$DIST/$NAME"
rm -rf "$STAGE"
cmake --install "$BUILD" --prefix "$STAGE"
[ -z "$(find "$STAGE" -name '*.a' ! -name '*.dll.a')" ] || fail "a static library in the package"
[ -z "$(find "$STAGE" -name 'hem-tool*')" ] || fail "hem-tool in the library package"

# --- link facts: libcurl inside, wolfSSL imported, nothing else ---------------
if [ "$OS" = "windows" ]; then
  LIB="$STAGE/bin/libencedo-hem.dll"
  [ -f "$STAGE/lib/libencedo-hem.dll.a" ] || fail "no import library lib/libencedo-hem.dll.a"
  DLLS="$(objdump -p "$LIB" | awk '/DLL Name:/ {print tolower($3)}' | sort -u)"
  echo "libencedo-hem.dll imports:"
  # shellcheck disable=SC2001  # indenting a multi-line value
  echo "$DLLS" | sed 's/^/  /'
  echo "$DLLS" | grep -q -x "$WLIB_NAME" || fail "libencedo-hem.dll does not import $WLIB_NAME"
  if echo "$DLLS" | grep -E -q 'curl|libgcc|winpthread|libssl|libcrypto|zlib'; then
    fail "libencedo-hem.dll imports a non-system DLL other than wolfSSL"
  fi
  echo "$DLLS" | grep -E -q '^(api-ms-win-crt-|ucrtbase\.dll)' || fail "no Universal CRT import — not a UCRT64 build"
  if echo "$DLLS" | grep -q -x 'msvcrt.dll'; then fail "libencedo-hem.dll imports msvcrt.dll — not a UCRT64 build"; fi
else
  LIB="$(find "$STAGE/lib" -name 'libencedo-hem.so.*.*' -type f)"
  [ -n "$LIB" ] || fail "no libencedo-hem.so.<version> in the package"
  DYN="$(readelf -d "$LIB")"
  echo "$(basename "$LIB"):"
  # shellcheck disable=SC2001  # indenting a multi-line value
  echo "$DYN" | grep -E 'SONAME|NEEDED|RUNPATH|RPATH' | sed 's/^/  /'
  # shellcheck disable=SC2016  # comparing against the literal $ORIGIN
  echo "$DYN" | grep -q 'RUNPATH.*\[\$ORIGIN\]' || fail "RUNPATH is not \$ORIGIN"
  echo "$DYN" | grep -q "NEEDED.*\[$WLIB_NAME\]" || fail "the library does not need $WLIB_NAME"
  if echo "$DYN" | grep -E -q 'NEEDED.*(curl|libssl|libcrypto)'; then fail "a dynamic libcurl/OpenSSL dependency"; fi
fi

# --- notices + README (no wolfSSL text: wolfSSL is not distributed in it) ------
mkdir -p "$STAGE/LICENSES"
cp LICENSE "$STAGE/LICENSES/LICENSE.encedo-hem"
cp "$PREFIX/COPYING.curl" "$STAGE/LICENSES/COPYING.curl"
cp src/vendor/cjson/LICENSE "$STAGE/LICENSES/LICENSE.cjson"
cp src/vendor/bip39/LICENSE "$STAGE/LICENSES/LICENSE.bip39-wordlist"
if [ "$OS" = "windows" ]; then
  FILES="  bin/libencedo-hem.dll       the library
  lib/libencedo-hem.dll.a     its import library"
  PLACE="next to your program's .exe (or in a directory on PATH), like
libencedo-hem.dll"
else
  FILES="  lib/libencedo-hem.so*       the library (+ its symlinks)"
  PLACE="into lib/, beside libencedo-hem.so - the library looks in its own
directory first (RUNPATH \$ORIGIN)"
fi
cat > "$STAGE/README.txt" <<EOF
Encedo HEM C SDK ${LABEL} (${OS}, x86_64) - the shared library.

Contents:
  include/ehem/               the public headers (they include no libcurl or
                              wolfSSL header)
${FILES}
  lib/cmake/encedo-hem/       find_package(encedo-hem CONFIG)
  lib/pkgconfig/encedo-hem.pc pkg-config
  LICENSES/

The library links libcurl ${CURL_VERSION} (wolfSSL as its TLS backend, HTTP/HTTPS
only) STATICALLY and wolfSSL ${WOLFSSL_VERSION} DYNAMICALLY. wolfSSL is NOT in
this archive: download ${WNAME}.${EXT} from the same release and put
${WLIB_NAME} ${PLACE}.
There is no static library here - no published package links wolfSSL
statically; the static SDK is the OpenSSL-based
encedo-hem-${LABEL}-${OS}-x86_64-static package of the same release.
Verify downloads against SHA256SUMS.

Use:
  CMake       find_package(encedo-hem CONFIG REQUIRED) with
              -DCMAKE_PREFIX_PATH=<this directory>, then
              target_link_libraries(<app> PRIVATE encedo-hem::encedo-hem)
  pkg-config  PKG_CONFIG_PATH=<this directory>/lib/pkgconfig
              pkg-config --cflags --libs encedo-hem

The SDK checks the wolfSSL it loaded before anything else: another version, or
the same version built with other options, makes ehem_global_init() return
EHEM_ERR_BACKEND_MISMATCH instead of crashing; ehem_build_info_get() reports
the versions built against and loaded.

API reference: docs/API-GUIDE.md and the headers of
https://github.com/KajtomatO/encedo-hem-c-api (${SRC_REF}).

Licenses: see LICENSES/ - the Encedo HEM C SDK is MIT; libcurl (curl license),
cJSON (MIT) and the BIP39 wordlist (MIT) are compiled in. wolfSSL (GPLv2 or
later, or a commercial license from wolfSSL Inc.) is a separate download; its
license text, build configuration and corresponding source are published with
that asset.
EOF
if [ "$OS" = "windows" ]; then
  cat >> "$STAGE/README.txt" <<'EOF'

Windows: built in MSYS2 UCRT64 with MinGW-w64 GCC - link it with a MinGW-w64
toolchain (MSVC is not supported). The DLL uses the Universal C Runtime, part of
Windows 10 / Server 2016 and later; older Windows needs update KB2999226. With
pkg-config, compile with -DEHEM_USING_SHARED (the CMake target sets it) so the
headers declare the API dllimport.
EOF
fi

( cd "$DIST" && rm -f "$NAME.tar.gz" "$NAME.zip" )
if [ "$OS" = "windows" ]; then
  ( cd "$DIST" && zip -qr "$NAME.zip" "$NAME" )
else
  ( cd "$DIST" && tar czf "$NAME.tar.gz" "$NAME" )
fi
echo "packaged: $DIST/$NAME.$EXT"

# --- consumer smoke: the package + the companion asset, as a user has them -----
T="$(mktemp -d)"; trap 'rm -rf "$T"' EXIT
mkdir -p "$T/unpacked"
if [ "$OS" = "windows" ]; then
  unzip -q "$DIST/$NAME.zip" -d "$T/unpacked"; unzip -q "$DIST/$WNAME.zip" -d "$T/unpacked"
else
  tar xzf "$DIST/$NAME.tar.gz" -C "$T/unpacked"; tar xzf "$DIST/$WNAME.tar.gz" -C "$T/unpacked"
fi
PKG="$(cd "$T/unpacked/$NAME" && pwd -P)"
cp "$T/unpacked/$WNAME/$WLIB_NAME" "$PKG/$RUNTIME_DIR/"
CC_PATH="$(sed -n 's/^CMAKE_C_COMPILER:[A-Z]*=//p' "$BUILD/CMakeCache.txt")"
# The package and nothing else: no deps prefix, no system .pc files
# (PKG_CONFIG_LIBDIR replaces the default search path), no LD_LIBRARY_PATH.
env -u LD_LIBRARY_PATH PKG_CONFIG_PATH= PKG_CONFIG_LIBDIR="$(native "$PKG/lib/pkgconfig")" \
  cmake "-DEHEM_PACKAGE=$(native "$PKG")" "-DEHEM_SCRATCH=$(native "$T/gate")" \
    "-DEHEM_CONSUMER_SRC=$(native "$ROOT/tests/consumer")" -DEHEM_VERSION="$VERSION" \
    -DEHEM_LIBDIR=lib -DEHEM_SHARED=ON -DEHEM_STATIC=OFF -DEHEM_GENERATOR=Ninja \
    "-DEHEM_C_COMPILER=$CC_PATH" -DEHEM_BUILD_TYPE=Release -DEHEM_CRYPTO_BACKEND=wolfssl \
    -P "$(native "$ROOT/tests/consumer/check_consumer.cmake")"

# Each consumer resolves libencedo-hem and wolfSSL from the package, and nothing
# but system libraries besides.
for c in consumer_cmake_shared consumer_pc_shared; do
  if [ "$OS" = "windows" ]; then
    DLLS="$(objdump -p "$T/gate/build/$c.exe" | awk '/DLL Name:/ {print tolower($3)}' | sort -u)"
    echo "$c.exe imports:"
    # shellcheck disable=SC2001  # indenting a multi-line value
    echo "$DLLS" | sed 's/^/  /'
    echo "$DLLS" | grep -q -x 'libencedo-hem.dll' || fail "$c does not import libencedo-hem.dll"
    if echo "$DLLS" | grep -E -q 'curl|wolfssl|libssl|libcrypto|zlib'; then
      fail "$c imports what the package's DLL should hide"
    fi
  else
    L="$(env -u LD_LIBRARY_PATH ldd "$T/gate/build/$c")"
    echo "$c:"
    # shellcheck disable=SC2001  # indenting a multi-line value
    echo "$L" | sed 's/^/  /'
    # Fixed strings: the label in the path carries a '+'.
    echo "$L" | grep -q -F "libencedo-hem.so.1 => $PKG/lib/" || fail "$c: libencedo-hem not from the package"
    echo "$L" | grep -q -F "$WLIB_NAME => $PKG/lib/$WLIB_NAME" \
      || fail "$c: $WLIB_NAME not from the package (a system copy?)"
    if echo "$L" | grep -q 'not found'; then fail "$c: a library is not found"; fi
    OTHER="$(echo "$L" | awk -v pkg="$PKG/lib/" \
      '/=>/ && index($3, pkg) != 1 && $3 !~ "^/(usr/)?lib" {print $3}')"
    [ -z "$OTHER" ] || fail "$c: libraries from outside the package and the system: $OTHER"
  fi
done
echo "library package smoke OK: $NAME.$EXT + $WNAME.$EXT"
