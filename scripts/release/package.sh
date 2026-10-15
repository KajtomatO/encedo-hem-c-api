#!/usr/bin/env bash
#
# package.sh — build hem-tool as a Release binary against the prefix from
# build-deps.sh (libcurl STATIC, wolfSSL DYNAMIC), run the unit suite on that
# build, assert the link mode, package hem-tool's archive, package the wolfSSL
# companion asset (package-wolfssl.sh) and smoke-test the two side by side —
# exactly as a user unpacks them (REQ-BUILD-005); then the SDK library package
# (package-lib.sh, REQ-BUILD-009).
#
# implements: REQ-BUILD-005, REQ-TOOL-026 (Windows: hem-tool delay-loads
#             libwolfssl.dll — asserted here and smoke-tested without the DLL),
#             REQ-BUILD-009 (the library package, through package-lib.sh; the
#             -static variants, through package-static.sh)
#
#   scripts/release/package.sh <prefix> <linux|windows> [dist-dir] [dynamic|static]
#
# Output in <dist>, `dynamic` (the default; <prefix> from build-deps.sh
# wolfssl): hem-tool-<version>[+g<sha>]-<os>-x86_64.{tar.gz|zip},
# wolfssl-<wv>-<os>-x86_64.{tar.gz|zip} (+ wolfssl-<wv>-stable-src.tar.gz on
# Linux) and encedo-hem-<version>[+g<sha>]-<os>-x86_64.{tar.gz|zip}; `static`
# (<prefix> from build-deps.sh openssl): the same two names with a -static
# suffix (package-static.sh). Either run then writes SHA256SUMS over every
# archive in <dist>. On a tag build (RELEASE_TAG=v…) the
# tag must be v<project version> or v<project version>-rc<N> (a pre-release) —
# anything else fails here, before publishing.
set -euo pipefail

PREFIX="$(cd "${1:?usage: package.sh <prefix> <linux|windows> [dist] [dynamic|static]}" && pwd)"
OS="${2:?linux|windows}"
DIST="${3:-dist}"
VARIANT="${4:-dynamic}"
BUILD="${BUILD_DIR:-build-release}"
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$ROOT"

VERSION="$(sed -n -E '/^project\(encedo-hem/,/\)/{s/^[[:space:]]*VERSION[[:space:]]+([0-9]+\.[0-9]+\.[0-9]+).*/\1/p}' CMakeLists.txt | head -1)"
[ -n "$VERSION" ] || { echo "cannot read the project version from CMakeLists.txt"; exit 1; }
SHA="$(git rev-parse --short HEAD 2>/dev/null || echo unknown)"
if [ -n "${RELEASE_TAG:-}" ]; then
  # v<version> = a release; v<version>-rc<N> = a pre-release of that version
  # (REQ-BUILD-005), published as a GitHub pre-release by release.yml.
  if [ "$RELEASE_TAG" != "v$VERSION" ] \
     && ! { [[ "$RELEASE_TAG" =~ -rc[1-9][0-9]*$ ]] && [ "${RELEASE_TAG%-rc*}" = "v$VERSION" ]; }; then
    echo "tag $RELEASE_TAG does not match the project version: expected v$VERSION or v$VERSION-rc<N>"
    exit 1
  fi
  LABEL="${RELEASE_TAG#v}"
  SRC_REF="tag ${RELEASE_TAG}"
else
  LABEL="${VERSION}+g${SHA}"
  SRC_REF="commit ${SHA}"
fi
NAME="hem-tool-${LABEL}-${OS}-x86_64"

write_sums() {
  ( cd "$DIST" && find . -maxdepth 1 \( -name '*.tar.gz' -o -name '*.zip' \) -printf '%f\n' \
      | sort | xargs -r sha256sum > SHA256SUMS )
  echo "SHA256SUMS:"; sed 's/^/  /' "$DIST/SHA256SUMS"
}

case "$VARIANT" in
  dynamic) ;;
  static)
    # The -static variants (REQ-BUILD-009): OpenSSL and libcurl from the
    # deps-openssl prefix, linked in — hem-tool and the SDK library package.
    "$ROOT/scripts/release/package-static.sh" "$PREFIX" "$OS" "$DIST" "$LABEL" "$SRC_REF"
    write_sums
    exit 0
    ;;
  *) echo "package.sh: unknown variant '$VARIANT' (dynamic|static)"; exit 1 ;;
esac

# --- the prefix: shared wolfSSL + static libcurl from build-deps.sh ----------
export PKG_CONFIG_PATH="$PREFIX/lib/pkgconfig${PKG_CONFIG_PATH:+:$PKG_CONFIG_PATH}"
grep -q -- '-shared+' "$PREFIX/.stamp" 2>/dev/null \
  || { echo "$PREFIX was not built by scripts/release/build-deps.sh (shared wolfSSL)"; exit 1; }
WOLFSSL_VERSION="$(pkg-config --modversion wolfssl)"
CURL_VERSION="$(pkg-config --modversion libcurl)"
WNAME="wolfssl-${WOLFSSL_VERSION}-${OS}-x86_64"
EXTRA=()
if [ "$OS" = "windows" ]; then
  # -static folds libgcc + libwinpthread into the exe. It only restricts `-l`
  # searches to .a archives; CMake passes wolfSSL as the absolute path of its
  # import library (lib/libwolfssl.dll.a), so the exe still IMPORTS
  # libwolfssl.dll — the one non-system DLL, by design.
  EXTRA+=(-DCMAKE_EXE_LINKER_FLAGS=-static)
  # REQ-TOOL-026: hem-tool imports libwolfssl.dll through the delay-load import
  # library build-deps.sh made, so a missing DLL is reported by hem-tool itself
  # (exit 12) instead of by the Windows loader before main (0xC0000135).
  EXTRA+=("-DEHEM_WOLFSSL_DELAYLOAD_LIB=$PREFIX/lib/libwolfssl.delay.a")
  WLIB_NAME=libwolfssl.dll; WLIB="$PREFIX/bin/$WLIB_NAME"; EXT=zip
  LOOKUP="the directory of hem-tool.exe is first in the DLL search order"
  SAME="file name"
else
  # The binary resolves libwolfssl.so.N from its own directory first, then the
  # system: RUNPATH = $ORIGIN only, never the CI prefix path. With
  # BUILD_WITH_INSTALL_RPATH the build-tree binary IS the packaged one; the
  # unit tests (tests/unit/) then need LD_LIBRARY_PATH for the ctest run only.
  # The same choice hides the prefix from the LINKER: executables that link the
  # shared SDK (the integration tests) make ld resolve libencedo-hem.so's
  # NEEDED libwolfssl.so.N, so -rpath-link names the prefix — link-time only,
  # never recorded in a binary. Without it ld silently falls back to a system
  # libwolfssl.so.N of the same soname where one exists (a dev host) and fails
  # where none does (the CI runner) — the 2026-10-10 first-run failure.
  # shellcheck disable=SC2016  # $ORIGIN is a literal for the loader, not a shell variable
  EXTRA+=(-DCMAKE_BUILD_WITH_INSTALL_RPATH=ON '-DCMAKE_INSTALL_RPATH=$ORIGIN'
          "-DCMAKE_EXE_LINKER_FLAGS=-Wl,-rpath-link,$PREFIX/lib")
  WLIB_NAME="$(readelf -d "$PREFIX/lib/libwolfssl.so" | sed -n -E 's/.*\(SONAME\).*\[(.*)\].*/\1/p')"
  [ -n "$WLIB_NAME" ] || { echo "no SONAME in $PREFIX/lib/libwolfssl.so"; exit 1; }
  WLIB="$PREFIX/lib/$WLIB_NAME"; EXT=tar.gz
  LOOKUP="hem-tool searches its own directory first (RUNPATH \$ORIGIN), then the system library path"
  SAME="soname"
fi

# --- build (Release, static libcurl, dynamic wolfSSL) ------------------------
# libcurl comes from the prefix's CURLConfig.cmake — a static-only install, so
# no FindCURL switch is needed (CURL_USE_STATIC_LIBS was unused and warned).
cmake -B "$BUILD" -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_PREFIX_PATH="$PREFIX" "${EXTRA[@]}"
cmake --build "$BUILD"
if [ "$OS" = "windows" ]; then
  PATH="$PREFIX/bin:$PATH" ctest --test-dir "$BUILD" -L unit --output-on-failure
else
  LD_LIBRARY_PATH="$PREFIX/lib" ctest --test-dir "$BUILD" -L unit --output-on-failure
fi

# --- link-mode check: wolfSSL dynamic (its own asset), everything else in ----
cp -L "$WLIB" "$BUILD/"      # the deployment layout: the library beside the binary
if [ "$OS" = "windows" ]; then
  BIN="$BUILD/hem-tool.exe"
  DLLS="$(objdump -p "$BIN" | awk '/DLL Name:/ {print tolower($3)}' | sort -u)"
  echo "imports:"
  # shellcheck disable=SC2001  # indenting a multi-line value
  echo "$DLLS" | sed 's/^/  /'
  # REQ-TOOL-026: libwolfssl.dll is a DELAY import — absent from the ordinary
  # import table (the loader must not demand it before main), present as a
  # delay-load descriptor. GNU ld leaves the PE delay-import directory entry
  # empty, so the evidence is the descriptor symbol and the DLL name string.
  if echo "$DLLS" | grep -q -x "$WLIB_NAME"; then
    echo "$WLIB_NAME is an ordinary import — the delay-load import library was not linked"; exit 1
  fi
  nm "$BIN" | grep -q '__DELAY_IMPORT_DESCRIPTOR_.*libwolfssl' \
    || { echo "no delay-load descriptor for $WLIB_NAME — wolfSSL got linked statically?"; exit 1; }
  strings -a "$BIN" | grep -q -x "$WLIB_NAME" \
    || { echo "the delay-load descriptor does not name $WLIB_NAME"; exit 1; }
  echo "delay imports: $WLIB_NAME"
  if echo "$DLLS" | grep -E -q 'curl|libgcc|winpthread|libssl|libcrypto|zlib'; then
    echo "a non-system DLL other than wolfSSL is imported"; exit 1
  fi
  # UCRT64 (ARCHITECTURE.md §1): the C runtime is the Universal CRT
  # (api-ms-win-crt-*.dll / ucrtbase.dll), never the MINGW64-era msvcrt.dll.
  echo "$DLLS" | grep -E -q '^(api-ms-win-crt-|ucrtbase\.dll)' \
    || { echo "hem-tool.exe imports no Universal CRT — not a UCRT64 build"; exit 1; }
  if echo "$DLLS" | grep -q -x 'msvcrt.dll'; then
    echo "hem-tool.exe imports msvcrt.dll — not a UCRT64 build"; exit 1
  fi
else
  BIN="$BUILD/hem-tool"
  RP="$(readelf -d "$BIN" | sed -n -E 's/.*\((RUNPATH|RPATH)\).*\[(.*)\].*/\2/p')"
  # shellcheck disable=SC2016  # comparing against the literal $ORIGIN
  [ "$RP" = '$ORIGIN' ] || { echo "RUNPATH is '$RP', expected '\$ORIGIN'"; exit 1; }
  echo "shared objects:"; ldd "$BIN" | sed 's/^/  /'
  ldd "$BIN" | grep -q "$WLIB_NAME => $(cd "$BUILD" && pwd)/$WLIB_NAME" \
    || { echo "$WLIB_NAME is not resolved from \$ORIGIN (the binary's own directory)"; exit 1; }
  if ldd "$BIN" | grep -E -q 'curl|libssl|libcrypto|not found'; then
    echo "unexpected dynamic dependency"; exit 1
  fi
fi
"$BIN" help > /dev/null
"$BIN" --version      # REQ-TOOL-025: the job log records built-against vs loaded

# --- package hem-tool (MIT/permissive notices only: wolfSSL is NOT inside) ---
STAGE="$DIST/$NAME"
rm -rf "$STAGE"; mkdir -p "$STAGE/LICENSES"
cp "$BIN" "$STAGE/"
cp LICENSE "$STAGE/LICENSES/LICENSE.encedo-hem"
cp "$PREFIX/COPYING.curl" "$STAGE/LICENSES/COPYING.curl"
cp src/vendor/cjson/LICENSE "$STAGE/LICENSES/LICENSE.cjson"
cp src/vendor/bip39/LICENSE "$STAGE/LICENSES/LICENSE.bip39-wordlist"
# qrcodegen's MIT text exists only as the header block of qrcodegen.c (see its
# VENDORED.md); ship that block verbatim and FAIL if it is not there.
sed -n '1,/^ \*\/$/p' src/tools/hem-tool/vendor/qrcodegen/qrcodegen.c > "$STAGE/LICENSES/LICENSE.qrcodegen"
grep -q 'Permission is hereby granted' "$STAGE/LICENSES/LICENSE.qrcodegen" \
  || { echo "qrcodegen MIT header not found"; exit 1; }
cat > "$STAGE/README.txt" <<EOF
hem-tool ${LABEL} (${OS}, x86_64) - the Encedo HEM device CLI.

hem-tool links libcurl ${CURL_VERSION} (wolfSSL as its TLS backend, HTTP/HTTPS
only) STATICALLY and wolfSSL ${WOLFSSL_VERSION} DYNAMICALLY. wolfSSL is NOT in
this archive: download ${WNAME}.${EXT} from the same release and put
${WLIB_NAME} next to hem-tool -
${LOOKUP}.
A system wolfSSL with the same ${SAME} and build configuration (see that
asset's BUILD-CONFIG.txt) works too. Verify downloads against SHA256SUMS:
  sha256sum -c SHA256SUMS      (Linux)
  CertUtil -hashfile <file> SHA256   (Windows)

hem-tool checks the wolfSSL it loaded before doing anything else: another
version, or the same version built with other options, is refused with one
message naming both versions and exit code 12 (instead of a crash).
"hem-tool --version" prints the versions it was built against and loaded.

Usage: hem-tool help            (commands grouped by what they need)
       hem-tool help <command>  (per-command options)
       hem-tool --version       (versions built against and loaded)
Device URL via --url or EHEM_URL (default https://my.ence.do); passphrase via
--passphrase or EHEM_PASSPHRASE.

Licenses: see LICENSES/ - hem-tool and the Encedo HEM C SDK are MIT; libcurl
(curl license), cJSON (MIT), the BIP39 wordlist (MIT) and qrcodegen (MIT) are
compiled in. wolfSSL (GPLv2 or later, or a commercial license from wolfSSL
Inc.) is a separate download; its license text, build configuration and
corresponding source (wolfssl-${WOLFSSL_VERSION}-stable-src.tar.gz) are
published with that asset. Source: https://github.com/KajtomatO/encedo-hem-c-api
(${SRC_REF}).
EOF
if [ "$OS" = "windows" ]; then
  cat >> "$STAGE/README.txt" <<'EOF'

Windows: hem-tool.exe and libwolfssl.dll use the Universal C Runtime (built in
MSYS2 UCRT64) - part of Windows 10 / Server 2016 and later; older Windows
needs update KB2999226.

hem-tool.exe loads libwolfssl.dll when it first needs it: if the DLL is
missing, hem-tool says so - naming the DLL and the asset it comes from - and
exits with code 12 (PowerShell: $LASTEXITCODE). "hem-tool help",
"hem-tool --version" (the loaded version then reads "not found") and
"hem-tool keys types" work without it. Put libwolfssl.dll next to
hem-tool.exe.
EOF
fi

mkdir -p "$DIST"
( cd "$DIST" && rm -f "$NAME.tar.gz" "$NAME.zip" )
if [ "$OS" = "windows" ]; then
  ( cd "$DIST" && zip -qr "$NAME.zip" "$NAME" )
else
  ( cd "$DIST" && tar czf "$NAME.tar.gz" "$NAME" )
fi
ARCHIVE="$NAME.$EXT"
echo "packaged: $DIST/$ARCHIVE"

# --- the wolfSSL companion asset: its own archive, never inside hem-tool's ---
"$ROOT/scripts/release/package-wolfssl.sh" "$PREFIX" "$OS" "$DIST"

# --- drop-in smoke test: both archives exactly as a user unpacks them --------
T="$(mktemp -d)"; trap 'rm -rf "$T"' EXIT
if [ "$OS" = "windows" ]; then
  unzip -q "$DIST/$ARCHIVE" -d "$T"; unzip -q "$DIST/$WNAME.zip" -d "$T"
  # Negative (REQ-TOOL-026): hem-tool alone — $PREFIX/bin is NOT on PATH — must
  # report the missing DLL itself with exit 12, not die in the loader with
  # 0xC0000135; the commands that need no wolfSSL still work.
  set +e
  NEG_ERR="$("$T/$NAME/hem-tool.exe" --url http://127.0.0.1:1 status 2>&1 > /dev/null)"
  NEG_RC=$?
  set -e
  echo "without $WLIB_NAME: exit $NEG_RC"
  # shellcheck disable=SC2001  # indenting a multi-line value
  echo "$NEG_ERR" | sed 's/^/  /'
  [ "$NEG_RC" -eq 12 ] || { echo "expected exit 12 without $WLIB_NAME"; exit 1; }
  echo "$NEG_ERR" | grep -q "$WLIB_NAME was not found" \
    || { echo "no missing-DLL message naming $WLIB_NAME"; exit 1; }
  "$T/$NAME/hem-tool.exe" help > /dev/null
  "$T/$NAME/hem-tool.exe" keys types > /dev/null
  VER="$("$T/$NAME/hem-tool.exe" --version)"
  # shellcheck disable=SC2001  # indenting a multi-line value
  echo "$VER" | sed 's/^/  /'
  echo "$VER" | grep -q "not found (loaded)" \
    || { echo "--version without $WLIB_NAME should read 'not found (loaded)'"; exit 1; }
  # Positive: the DLL beside the exe (the exe's directory is first in the DLL
  # search order), which proves the drop-in layout.
  cp "$T/$WNAME/$WLIB_NAME" "$T/$NAME/"
  "$T/$NAME/hem-tool.exe" help > /dev/null
  "$T/$NAME/hem-tool.exe" --version | grep -q "${WOLFSSL_VERSION} (loaded)" \
    || { echo "--version with $WLIB_NAME beside it does not report ${WOLFSSL_VERSION} loaded"; exit 1; }
else
  tar xzf "$DIST/$ARCHIVE" -C "$T"; tar xzf "$DIST/$WNAME.tar.gz" -C "$T"
  # Negative: without the library and with a clean environment the loader must
  # refuse — unless this host has a system copy with the same soname.
  if ! "$(command -v ldconfig || echo /sbin/ldconfig)" -p 2>/dev/null | grep -q "$WLIB_NAME ("; then
    if env -i "$T/$NAME/hem-tool" help > /dev/null 2>&1; then
      echo "hem-tool ran WITHOUT $WLIB_NAME — wolfSSL is not dynamic"; exit 1
    fi
  else
    echo "note: this host has a system $WLIB_NAME — the negative drop-in test is skipped"
  fi
  cp "$T/$WNAME/$WLIB_NAME" "$T/$NAME/"
  env -i "$T/$NAME/hem-tool" help > /dev/null      # $ORIGIN, nothing else
fi
echo "drop-in OK: $ARCHIVE + $WNAME.$EXT"

# --- the SDK library package: the shared library, wolfSSL flavour ------------
"$ROOT/scripts/release/package-lib.sh" "$PREFIX" "$OS" "$DIST" "$LABEL" "$SRC_REF"

write_sums
