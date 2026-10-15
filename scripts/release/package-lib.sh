#!/usr/bin/env bash
#
# package-lib.sh — the SDK library packages (REQ-BUILD-009), built against a
# prefix from build-deps.sh, installed, packaged and smoke-tested exactly as a
# user unpacks them: the REQ-BUILD-008 sample consumer, built against the
# unpacked package via find_package and pkg-config, runs
# (tests/consumer/check_consumer.cmake, package mode).
#   shared (default) — the wolfSSL flavour, encedo-hem-<label>-<os>-x86_64:
#     the SHARED library only — no static library ships with wolfSSL (user
#     decision 2026-10-14; static linking is the OpenSSL -static package).
#     libcurl STATIC inside the library, wolfSSL DYNAMIC: the smoke runs it
#     beside the wolfSSL companion asset.
#   static — the OpenSSL flavour, encedo-hem-<label>-<os>-x86_64-static: the
#     STATIC library with the archives it was built against (libcurl.a,
#     libssl.a, libcrypto.a from the deps-openssl prefix), their headers, CMake
#     package config and .pc files, so `find_package` and `pkg-config --static`
#     resolve everything inside the package (REQ-BUILD-008 criterion 4).
#
# implements: REQ-BUILD-009
#
#   scripts/release/package-lib.sh <prefix> <linux|windows> <dist> <label> <source ref> [shared|static]
#
# Called by package.sh (shared) and package-static.sh (static) once the
# companion asset, if any, is in <dist>; package.sh writes SHA256SUMS afterwards.
set -euo pipefail

PREFIX="$(cd "${1:?usage: package-lib.sh <prefix> <linux|windows> <dist> <label> <source ref> [shared|static]}" && pwd)"
OS="${2:?linux|windows}"
DIST="$(mkdir -p "${3:?dist}" && cd "$3" && pwd)"
LABEL="${4:?label}"
SRC_REF="${5:?source ref}"
VARIANT="${6:-shared}"
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$ROOT"

fail() { echo "package-lib.sh: $*"; exit 1; }
# Paths handed to native Windows tools in their own form.
native() { if [ "$OS" = "windows" ]; then cygpath -m "$1"; else printf '%s' "$1"; fi; }

VERSION="$(sed -n -E '/^project\(encedo-hem/,/\)/{s/^[[:space:]]*VERSION[[:space:]]+([0-9]+\.[0-9]+\.[0-9]+).*/\1/p}' CMakeLists.txt | head -1)"
[ -n "$VERSION" ] || fail "cannot read the project version from CMakeLists.txt"
export PKG_CONFIG_PATH="$PREFIX/lib/pkgconfig${PKG_CONFIG_PATH:+:$PKG_CONFIG_PATH}"
CURL_VERSION="$(pkg-config --modversion libcurl)"
if [ "$OS" = "windows" ]; then EXT=zip; X=.exe; else EXT=tar.gz; X=; fi

EXTRA=()
case "$VARIANT" in
  shared)
    BUILD="${LIB_BUILD_DIR:-build-lib}"
    WOLFSSL_VERSION="$(pkg-config --modversion wolfssl)"
    WNAME="wolfssl-${WOLFSSL_VERSION}-${OS}-x86_64"
    NAME="encedo-hem-${LABEL}-${OS}-x86_64"
    if [ "$OS" = "windows" ]; then
      # -static folds libgcc + libwinpthread into the DLL, as build-deps.sh does
      # for libwolfssl.dll: the package's DLL imports system DLLs and
      # libwolfssl.dll.
      EXTRA+=(-DCMAKE_SHARED_LINKER_FLAGS=-static)
      WLIB_NAME=libwolfssl.dll; RUNTIME_DIR=bin
    else
      # RUNPATH $ORIGIN: the library finds the companion libwolfssl.so.N put beside it.
      # shellcheck disable=SC2016  # $ORIGIN is a literal for the loader, not a shell variable
      EXTRA+=(-DCMAKE_BUILD_WITH_INSTALL_RPATH=ON '-DCMAKE_INSTALL_RPATH=$ORIGIN')
      WLIB_NAME="$(readelf -d "$PREFIX/lib/libwolfssl.so" | sed -n -E 's/.*\(SONAME\).*\[(.*)\].*/\1/p')"
      [ -n "$WLIB_NAME" ] || fail "no SONAME in $PREFIX/lib/libwolfssl.so"
      RUNTIME_DIR=lib
    fi
    BUILD_OPTS=(-DEHEM_BUILD_STATIC=OFF)
    ;;
  static)
    BUILD="${LIB_BUILD_DIR:-build-lib-static}"
    grep -q -- '^openssl-.*-static+curl-.*-static+' "$PREFIX/.stamp" 2>/dev/null \
      || fail "$PREFIX was not built by scripts/release/build-deps.sh <prefix> openssl"
    OPENSSL_VERSION="$(pkg-config --modversion libcrypto)"
    NAME="encedo-hem-${LABEL}-${OS}-x86_64-static"
    # EHEM_DEPS_LINK=static: libcurl and OpenSSL resolve to archives or the
    # configure fails (REQ-BUILD-006).
    BUILD_OPTS=(-DEHEM_BUILD_SHARED=OFF -DEHEM_CRYPTO_BACKEND=openssl -DEHEM_DEPS_LINK=static)
    ;;
  *) fail "unknown variant '$VARIANT' (shared|static)" ;;
esac

# --- build + install: the library alone ---------------------------------------
cmake -B "$BUILD" -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="$PREFIX" \
  -DCMAKE_INSTALL_LIBDIR=lib -DEHEM_BUILD_TOOL=OFF -DBUILD_TESTING=OFF \
  "${BUILD_OPTS[@]}" "${EXTRA[@]}"
cmake --build "$BUILD"
STAGE="$DIST/$NAME"
rm -rf "$STAGE"
cmake --install "$BUILD" --prefix "$STAGE"
[ -z "$(find "$STAGE" -name 'hem-tool*')" ] || fail "hem-tool in the library package"

if [ "$VARIANT" = shared ]; then
  [ -z "$(find "$STAGE" -name '*.a' ! -name '*.dll.a')" ] || fail "a static library in the package"

  # --- link facts: libcurl inside, wolfSSL imported, nothing else -------------
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
else
  # --- the archives it was built against, with what resolves them -------------
  # Headers: curl's and OpenSSL's package lookups (curl's CMake package config,
  # FindOpenSSL) need them, and with them the archives stand on their own.
  cp "$PREFIX/lib/libcurl.a" "$PREFIX/lib/libssl.a" "$PREFIX/lib/libcrypto.a" "$STAGE/lib/"
  cp -R "$PREFIX/include/curl" "$PREFIX/include/openssl" "$STAGE/include/"
  mkdir -p "$STAGE/lib/cmake" "$STAGE/lib/pkgconfig"
  cp -R "$PREFIX/lib/cmake/CURL" "$STAGE/lib/cmake/"      # relocatable (_IMPORT_PREFIX)
  # The .pc files name the build prefix: relative to their own directory
  # instead, like encedo-hem.pc.
  for pc in libcurl libssl libcrypto openssl; do
    # shellcheck disable=SC2016  # ${pcfiledir} etc. are pkg-config's, not the shell's
    sed -E -e 's|^prefix=.*|prefix=${pcfiledir}/../..|' \
           -e 's|^exec_prefix=.*|exec_prefix=${prefix}|' \
           -e 's|^libdir=.*|libdir=${exec_prefix}/lib|' \
           -e 's|^includedir=.*|includedir=${prefix}/include|' \
      "$PREFIX/lib/pkgconfig/$pc.pc" > "$STAGE/lib/pkgconfig/$pc.pc"
  done
  if grep -l -F -e "$PREFIX" -e "$(native "$PREFIX")" -r "$STAGE/lib/pkgconfig" "$STAGE/lib/cmake"; then
    fail "a file above still names the build prefix $PREFIX"
  fi
  if find "$STAGE" \( -name '*.so' -o -name '*.so.*' -o -name '*.dll' -o -name '*.dll.a' \
                      -o -iname '*wolfssl*' \) | grep .; then
    fail "a shared library or a wolfSSL file (above) in the static package"
  fi
  # No wolfSSL in a -static asset (ARCHITECTURE.md §1): no wolfSSL or wolfCrypt
  # symbol, defined or referenced, in any archive.
  for a in "$STAGE"/lib/*.a; do
    if nm "$a" 2> /dev/null | grep -E -q ' (wolfSSL_|wolfCrypt_|wc_)[A-Za-z]'; then
      fail "wolfSSL symbols in $(basename "$a")"
    fi
  done
  echo "archives: $(cd "$STAGE/lib" && echo ./*.a) — no wolfSSL symbol"
fi

# --- notices + README ---------------------------------------------------------
mkdir -p "$STAGE/LICENSES"
cp LICENSE "$STAGE/LICENSES/LICENSE.encedo-hem"
cp "$PREFIX/COPYING.curl" "$STAGE/LICENSES/COPYING.curl"
cp src/vendor/cjson/LICENSE "$STAGE/LICENSES/LICENSE.cjson"
cp src/vendor/bip39/LICENSE "$STAGE/LICENSES/LICENSE.bip39-wordlist"
if [ "$VARIANT" = shared ]; then
  # No wolfSSL text: wolfSSL is not distributed in it.
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
else
  cp "$PREFIX/LICENSE.openssl" "$STAGE/LICENSES/LICENSE.openssl"
  if [ "$OS" = "windows" ]; then
    CONF="OpenSSL configuration: these archives load none - neither openssl.cnf
nor the file OPENSSL_CONF names (REQ-BUILD-009). An application that wants one
loads it itself, e.g. OPENSSL_init_crypto(OPENSSL_INIT_LOAD_CONFIG, settings).
Certificates: the SDK uses the Windows certificate store."
  else
    CONF="OpenSSL configuration: /etc/ssl/openssl.cnf (or the file OPENSSL_CONF
names), as for the system's OpenSSL. Certificates: libcurl looks in
/etc/ssl/certs/ca-certificates.crt and /etc/ssl/certs (the Debian/Ubuntu
locations); elsewhere set a CA file through the SDK's TLS options."
  fi
  cat > "$STAGE/README.txt" <<EOF
Encedo HEM C SDK ${LABEL} (${OS}, x86_64) - the static library, OpenSSL
flavour (-static).

Contents:
  include/ehem/               the public headers (they include no libcurl or
                              OpenSSL header)
  lib/libencedo-hem.a         the SDK
  lib/libcurl.a               libcurl ${CURL_VERSION} (OpenSSL as its TLS backend,
                              HTTP/HTTPS only)
  lib/libssl.a, libcrypto.a   OpenSSL ${OPENSSL_VERSION}
  include/curl/, openssl/     their headers
  lib/cmake/encedo-hem/       find_package(encedo-hem CONFIG) - finds libcurl
  lib/cmake/CURL/             and OpenSSL in this directory, as archives
  lib/pkgconfig/              encedo-hem.pc + libcurl, libssl, libcrypto,
                              openssl .pc files
  LICENSES/

Everything the SDK needs is here: a program linked against it needs no
encedo-hem, libcurl or OpenSSL library at run time. No wolfSSL code is in it.

Use:
  CMake       find_package(encedo-hem CONFIG REQUIRED) with
              -DCMAKE_PREFIX_PATH=<this directory>, then
              target_link_libraries(<app> PRIVATE encedo-hem::encedo-hem-static)
  pkg-config  PKG_CONFIG_PATH=<this directory>/lib/pkgconfig
              pkg-config --static --cflags --libs encedo-hem

${CONF}

The SDK checks its crypto library before anything else (ehem_global_init();
ehem_build_info_get() reports the versions).

API reference: docs/API-GUIDE.md and the headers of
https://github.com/KajtomatO/encedo-hem-c-api (${SRC_REF}).

Licenses: see LICENSES/ - the Encedo HEM C SDK is MIT; OpenSSL (Apache License
2.0), libcurl (curl license), cJSON (MIT) and the BIP39 wordlist (MIT). OpenSSL
${OPENSSL_VERSION} and curl ${CURL_VERSION} are their upstream releases.
Verify downloads against SHA256SUMS.
EOF
fi
if [ "$OS" = "windows" ]; then
  cat >> "$STAGE/README.txt" <<'EOF'

Windows: built in MSYS2 UCRT64 with MinGW-w64 GCC - link it with a MinGW-w64
toolchain (MSVC is not supported). It uses the Universal C Runtime, part of
Windows 10 / Server 2016 and later; older Windows needs update KB2999226.
EOF
  if [ "$VARIANT" = shared ]; then
    cat >> "$STAGE/README.txt" <<'EOF'
With pkg-config, compile with -DEHEM_USING_SHARED (the CMake target sets it) so
the headers declare the API dllimport.
EOF
  fi
fi

( cd "$DIST" && rm -f "$NAME.tar.gz" "$NAME.zip" )
if [ "$OS" = "windows" ]; then
  ( cd "$DIST" && zip -qr "$NAME.zip" "$NAME" )
else
  ( cd "$DIST" && tar czf "$NAME.tar.gz" "$NAME" )
fi
echo "packaged: $DIST/$NAME.$EXT"

# --- consumer smoke: the package (+ the companion asset), as a user has them ----
T="$(mktemp -d)"; trap 'rm -rf "$T"' EXIT
mkdir -p "$T/unpacked"
if [ "$OS" = "windows" ]; then
  unzip -q "$DIST/$NAME.zip" -d "$T/unpacked"
else
  tar xzf "$DIST/$NAME.tar.gz" -C "$T/unpacked"
fi
PKG="$(cd "$T/unpacked/$NAME" && pwd -P)"
if [ "$VARIANT" = shared ]; then
  if [ "$OS" = "windows" ]; then
    unzip -q "$DIST/$WNAME.zip" -d "$T/unpacked"
  else
    tar xzf "$DIST/$WNAME.tar.gz" -C "$T/unpacked"
  fi
  cp "$T/unpacked/$WNAME/$WLIB_NAME" "$PKG/$RUNTIME_DIR/"
  GATE=(-DEHEM_SHARED=ON -DEHEM_STATIC=OFF -DEHEM_CRYPTO_BACKEND=wolfssl)
  CONSUMERS="consumer_cmake_shared consumer_pc_shared"
else
  GATE=(-DEHEM_SHARED=OFF -DEHEM_STATIC=ON -DEHEM_CRYPTO_BACKEND=openssl)
  CONSUMERS="consumer_cmake_static consumer_pc_static"
fi
CC_PATH="$(sed -n 's/^CMAKE_C_COMPILER:[A-Z]*=//p' "$BUILD/CMakeCache.txt")"
# The package and nothing else: no deps prefix, no system .pc files
# (PKG_CONFIG_LIBDIR replaces the default search path), no LD_LIBRARY_PATH.
env -u LD_LIBRARY_PATH PKG_CONFIG_PATH= PKG_CONFIG_LIBDIR="$(native "$PKG/lib/pkgconfig")" \
  cmake "-DEHEM_PACKAGE=$(native "$PKG")" "-DEHEM_SCRATCH=$(native "$T/gate")" \
    "-DEHEM_CONSUMER_SRC=$(native "$ROOT/tests/consumer")" -DEHEM_VERSION="$VERSION" \
    -DEHEM_LIBDIR=lib "${GATE[@]}" -DEHEM_GENERATOR=Ninja \
    "-DEHEM_C_COMPILER=$CC_PATH" -DEHEM_BUILD_TYPE=Release \
    -P "$(native "$ROOT/tests/consumer/check_consumer.cmake")" 2>&1 | tee "$T/gate.log"
if [ "$VARIANT" = static ]; then
  # REQ-BUILD-008 criterion 4: the all-static `pkg-config --static` closure
  # linked — not the gate's fallback to shared dependencies.
  grep -q 'consumer_pc_static: pkg-config --static closure' "$T/gate.log" \
    || fail "consumer_pc_static did not link the pkg-config --static closure"
fi

# Each consumer resolves libencedo-hem and its dependencies from the package
# (shared) or carries them (static), and nothing but system libraries besides.
for c in $CONSUMERS; do
  if [ "$OS" = "windows" ]; then
    DLLS="$(objdump -p "$T/gate/build/$c$X" | awk '/DLL Name:/ {print tolower($3)}' | sort -u)"
    echo "$c$X imports:"
    # shellcheck disable=SC2001  # indenting a multi-line value
    echo "$DLLS" | sed 's/^/  /'
    if [ "$VARIANT" = shared ]; then
      echo "$DLLS" | grep -q -x 'libencedo-hem.dll' || fail "$c does not import libencedo-hem.dll"
      if echo "$DLLS" | grep -E -q 'curl|wolfssl|libssl|libcrypto|zlib'; then
        fail "$c imports what the package's DLL should hide"
      fi
    elif echo "$DLLS" | grep -E -q 'encedo|curl|wolfssl|libssl|libcrypto|zlib'; then
      fail "$c imports what the static package links in"
    fi
  else
    L="$(env -u LD_LIBRARY_PATH ldd "$T/gate/build/$c")"
    echo "$c:"
    # shellcheck disable=SC2001  # indenting a multi-line value
    echo "$L" | sed 's/^/  /'
    if echo "$L" | grep -q 'not found'; then fail "$c: a library is not found"; fi
    if [ "$VARIANT" = shared ]; then
      # Fixed strings: the label in the path carries a '+'.
      echo "$L" | grep -q -F "libencedo-hem.so.1 => $PKG/lib/" || fail "$c: libencedo-hem not from the package"
      echo "$L" | grep -q -F "$WLIB_NAME => $PKG/lib/$WLIB_NAME" \
        || fail "$c: $WLIB_NAME not from the package (a system copy?)"
    elif echo "$L" | grep -E -q 'libencedo-hem|libcurl|libssl|libcrypto|libwolfssl'; then
      fail "$c: a dynamic SDK, libcurl or crypto library — the static package links them in"
    fi
    OTHER="$(echo "$L" | awk -v pkg="$PKG/lib/" \
      '/=>/ && index($3, pkg) != 1 && $3 !~ "^/(usr/)?lib" {print $3}')"
    [ -z "$OTHER" ] || fail "$c: libraries from outside the package and the system: $OTHER"
  fi
done
echo "library package smoke OK: $NAME.$EXT${WNAME:+ + $WNAME.$EXT}"
